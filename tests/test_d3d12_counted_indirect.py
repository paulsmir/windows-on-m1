"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The owner must pass GPU-produced counts to native predication without mapping
count or arguments, while preserving direct/uncounted/patch draws, heap callback
scope and native failure propagation. Execute the actual production callsite;
the kernel and batch lifetime are tested in test_d3d12_gpu_count_predicate.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d12_owner_allocation_failure import function
from test_d3d12_native_owner import OWNER

class CountedIndirect(unittest.TestCase):
    def test_owner_does_not_read_count(self):
        source=OWNER.read_text()
        draw=function(source,'extern "C" HRESULT AgxD3d12OwnerDraw(')
        self.assertNotIn('resolve_draw_count(', draw)
        self.assertNotIn('AgxD3d12OwnerReadBuffer(', draw)
        self.assertIn('receipt("measure-d3d12-counted-path"', draw)

    def test_gpu_argument_offsets_do_not_truncate_or_wrap(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        The GPU path uses 32-bit Gallium offsets; reject truncation, wrapping
        64-bit placements and non-word strides before installing them.
        """
        source=OWNER.read_text()
        start=source.index('    const bool bad =', source.index('/* C14: indirect draws'))
        end=source.index('    if (bad)',start)
        code=r'''#include <cassert>
#include <cstdint>
struct Buffer {void *Resource;uint64_t Bytes;};
struct Indirect {Buffer *Buffer,*CountBuffer;unsigned MaxCount,Stride;uint64_t Offset,CountOffset;};
struct Draw {unsigned IndexSize;uint64_t IndexOffset;};
static bool invalid(Indirect *in,Draw *draw){unsigned args=draw->IndexSize?20:16;
'''+source[start:end]+r'''return bad;}
int main(){
 struct Buffer a={(void *)1,4096},c={(void *)1,4096};Draw d={4,0};
 Indirect in={&a,&c,3,32,28,12};assert(!invalid(&in,&d));
 in.CountOffset=0x10000000cull;c.Bytes=0x200000000ull;assert(invalid(&in,&d));
 c.Bytes=4096;in.CountOffset=UINT64_MAX-2;assert(invalid(&in,&d));
 in.CountOffset=12;in.Offset=UINT64_MAX-3;assert(invalid(&in,&d));
 in.Offset=28;in.Stride=30;assert(invalid(&in,&d));
 in.Stride=32;in.CountOffset=11;assert(invalid(&in,&d));
 in.CountOffset=12;in.Offset=27;assert(invalid(&in,&d));
 in.Offset=28;in.MaxCount=0;assert(invalid(&in,&d));
}
'''
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'test.cpp').write_text(code)
            result=subprocess.run(['clang++','-std=c++17','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)

    def test_owner_passes_gpu_count_and_unwinds(self):
        source=OWNER.read_text()
        begin=source.index('  AgxD3d12DrawHeapBindings heap_bindings =')
        end=source.index('\n  unbind_heaps(owner, false);',begin)
        code=r'''
#include <cassert>
#include <cstddef>
using HRESULT=int;using LONGLONG=long long;
#define S_OK 0
#define SUCCEEDED(x) ((x)>=0)
#define DXGI_ERROR_DEVICE_HUNG -2
struct pipe_draw_info {struct {void *resource;} index;};
struct pipe_draw_start_count_bias {};
struct pipe_draw_indirect_info {unsigned draw_count;void *indirect_draw_count;};
struct pipe_context {void (*draw_vbo)(pipe_context *,const pipe_draw_info *,unsigned,const pipe_draw_indirect_info *,const pipe_draw_start_count_bias *,unsigned);};
struct AGX_D3D12_OWNER {pipe_context *Context;};
struct AGX_D3D12_INDIRECT {bool Buffer;};
struct AgxD3d12DrawHeapBindings {AGX_D3D12_OWNER *Owner;HRESULT Result;};
static unsigned draws;static bool callbacks,fault;static int counter;
static HRESULT owner_failure(AGX_D3D12_OWNER *,HRESULT hr){return hr;}
static bool AgxWin32AsahiContextFaulted(pipe_context *){return fault;}
static int bind_draw_heaps(void *){return 1;}
static void AgxWin32AsahiSetDrawBindings(pipe_context *,int (*fn)(void *),void *){callbacks=fn!=nullptr;}
static void AgxWin32AsahiSetIndirectIndexBuffer(pipe_context *,void *r,unsigned offset){assert(!r&&!offset);}
static LONGLONG qpc_now(){return 0;}
static void draw_buffer(pipe_context *,const pipe_draw_info *,unsigned id,const pipe_draw_indirect_info *in,const pipe_draw_start_count_bias *,unsigned n){
 assert(callbacks&&!id&&n==1);++draws;
 if(in)assert(in->draw_count==12000&&(!in->indirect_draw_count||in->indirect_draw_count==&counter));
}
static HRESULT run(AGX_D3D12_OWNER *owner,AGX_D3D12_INDIRECT *in,bool counted){
 pipe_context *pipe=owner->Context;HRESULT hr=S_OK;
 struct {unsigned IndexSize,IndexOffset;} nonindexed={};auto *draw=&nonindexed;
 pipe_draw_info info={};pipe_draw_start_count_bias range={};
 pipe_draw_indirect_info indirect={12000,counted?&counter:nullptr};
'''+source[begin:end]+r'''
 (void)bound;(void)drawn;return hr;
}
int main(){
 pipe_context pipe={draw_buffer};AGX_D3D12_OWNER owner={&pipe};
 for(unsigned b=0;b<2;++b)for(unsigned c=0;c<2;++c)for(unsigned f=0;f<2;++f){
  fault=f;draws=0;AGX_D3D12_INDIRECT in={b!=0};
  assert(run(&owner,&in,c)==(f?DXGI_ERROR_DEVICE_HUNG:S_OK));
  assert(draws==1&&!callbacks);
 }
}
'''
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'test.cpp').write_text(code)
            result=subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
