"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Counted draws unnecessarily download GPU argument records and replay direct
CPU draws. The owner must synchronize only the count, clamp it, and preserve
native indirect arguments/stride. Zero/failing counts must issue no draw;
patch topology keeps the native driver's CPU fallback. Execute the production
owner callsite and count helper with explicit Gallium I/O boundaries.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d12_owner_allocation_failure import function
from test_d3d12_native_owner import OWNER

HEADER = r'''
#include <cassert>
#include <cstdint>
#include <cstring>
using UINT=unsigned;using HRESULT=int;using LONGLONG=long long;
#define S_OK 0
#define E_INVALIDARG -1
#define DXGI_ERROR_DEVICE_HUNG -2
#define SUCCEEDED(x) ((x)>=0)
#define FAILED(x) ((x)<0)
#define PIPE_MAP_READ 1
#define MESA_PRIM_PATCHES 14
struct pipe_resource {unsigned tag;};
struct pipe_transfer {};
struct pipe_box {int x,y,z,width,height,depth;};
struct pipe_draw_info {unsigned mode,index_size,instance_count;};
struct pipe_draw_start_count_bias {unsigned start,count;int index_bias;};
struct pipe_draw_indirect_info {unsigned offset,stride,draw_count,indirect_draw_count_offset;pipe_resource *buffer,*indirect_draw_count;void *count_from_stream_output;};
struct pipe_context {
 void *(*buffer_map)(pipe_context *,pipe_resource *,unsigned,unsigned,const pipe_box *,pipe_transfer **);
 void (*buffer_unmap)(pipe_context *,pipe_transfer *);
 void (*draw_vbo)(pipe_context *,const pipe_draw_info *,unsigned,const pipe_draw_indirect_info *,const pipe_draw_start_count_bias *,unsigned);
};
struct AGX_D3D12_OWNER {pipe_context *Context;};
struct AGX_D3D12_BUFFER {void *Resource;unsigned long long Bytes;};
struct AGX_D3D12_INDIRECT {AGX_D3D12_BUFFER *Buffer,*CountBuffer;unsigned MaxCount,Stride;unsigned long long Offset,CountOffset;};
struct OwnerLock {explicit OwnerLock(AGX_D3D12_OWNER *){}};
struct AgxD3d12DrawHeapBindings {AGX_D3D12_OWNER *Owner;HRESULT Result;};
static bool callbacks,fault;static unsigned maps,unmaps,arg_maps,draws,errors,mode,count;
static unsigned native_draws;static bool unrolling;
static pipe_resource args{1},counter{2};static pipe_transfer transfer;
static void AgxD3d12OwnerRecordError(AGX_D3D12_OWNER *,HRESULT){++errors;}
static HRESULT owner_failure(AGX_D3D12_OWNER *,HRESULT hr){return hr;}
static bool AgxWin32AsahiContextFaulted(pipe_context *){return fault;}
static int bind_draw_heaps(void *){return 1;}
static void AgxWin32AsahiSetDrawBindings(pipe_context *,int (*fn)(void *),void *){callbacks=fn!=nullptr;}
static LONGLONG qpc_now(){return 0;}
static void *map_buffer(pipe_context *,pipe_resource *r,unsigned level,unsigned flags,const pipe_box *box,pipe_transfer **out){
 assert(!callbacks);assert(r==&counter && !level && flags==PIPE_MAP_READ);
 assert(box->x==12 && box->width==4 && box->height==1 && box->depth==1);
 ++maps;*out=mode==1?nullptr:&transfer;return mode==1 || mode==2?nullptr:&count;
}
static void unmap_buffer(pipe_context *,pipe_transfer *p){assert(p==&transfer);++unmaps;}
@@NATIVE_UNROLL@@
static void draw_buffer(pipe_context *pipe,const pipe_draw_info *,unsigned id,const pipe_draw_indirect_info *in,const pipe_draw_start_count_bias *,unsigned n){
 assert(callbacks && n==1);
 if(unrolling){
  assert(in && in->buffer==&args && !in->indirect_draw_count && in->draw_count==1);
  assert(id==native_draws && in->offset==28+32*native_draws);++native_draws;return;
 }
 assert(!id);++draws;
 if(!in)return;
 if(in->indirect_draw_count){++arg_maps;return;} // upstream util_draw_indirect path
 assert(in->buffer==&args && in->offset==28 && in->stride==32);
 assert(in->draw_count==(count<3?count:3));
 if(in->draw_count>1){unrolling=true;pipe_draw_info info={};pipe_draw_start_count_bias range={};
  util_draw_multi_unroll_indirect(pipe,&info,in,&range);unrolling=false;
 }else ++native_draws;
}
'''

class CountedIndirect(unittest.TestCase):
    def test_owner_count_only_replay(self):
        source=OWNER.read_text()
        begin=source.index('  AgxD3d12DrawHeapBindings heap_bindings =')
        end=source.index('\n  unbind_heaps(owner, false);',begin)
        helpers=function(source,'extern "C" HRESULT AgxD3d12OwnerReadBuffer(')
        if 'static HRESULT resolve_draw_count(' in source:
            helpers+=function(source,'static HRESULT resolve_draw_count(')
        # Compile the pinned primary implementation, retaining its notice in
        # the temporary harness; no external function body is copied into Git.
        native=Path(__file__).resolve().parents[1]/'.local/reference/mesa/src/gallium/drivers/asahi/agx_state.c'
        text=native.read_text()
        unroll=function(text,'static void\nutil_draw_multi_unroll_indirect(')
        header=HEADER.replace('@@NATIVE_UNROLL@@',text[:text.index('#include')]+unroll)
        code=header+helpers+r'''
static HRESULT run(AGX_D3D12_OWNER *owner,AGX_D3D12_INDIRECT *in,unsigned primitive){
 pipe_context *pipe=owner->Context;HRESULT hr=S_OK;
 pipe_draw_info info={primitive,4,0};pipe_draw_start_count_bias range={};
 pipe_draw_indirect_info indirect={28,32,3,12,&args,in->CountBuffer?&counter:nullptr,nullptr};
 const unsigned mode=primitive;(void)mode;
'''+source[begin:end]+r'''
 (void)bound;(void)drawn;return hr;
}
int main(){
 pipe_context pipe={map_buffer,unmap_buffer,draw_buffer};AGX_D3D12_OWNER owner{&pipe};
 AGX_D3D12_BUFFER a{&args,128},c{&counter,64};AGX_D3D12_INDIRECT in{&a,&c,3,32,28,12};
 for(unsigned value:{0u,1u,2u,3u,7u,UINT32_MAX}){
  count=value;native_draws=maps=unmaps=arg_maps=draws=errors=0;
  assert(run(&owner,&in,4)==S_OK);
  assert(maps==1 && unmaps==1 && arg_maps==0 && draws==(value?1u:0u));
  assert(!callbacks && !errors && native_draws==(value<3?value:3));
 }
 for(mode=1;mode<=2;++mode){maps=unmaps=arg_maps=draws=errors=0;
  assert(run(&owner,&in,4)==DXGI_ERROR_DEVICE_HUNG);
  assert(maps==1 && unmaps==(mode==2?1u:0u) && !draws && !callbacks && errors==1);
 }
 mode=0;count=3;maps=unmaps=arg_maps=draws=errors=0;
 assert(run(&owner,&in,MESA_PRIM_PATCHES)==S_OK);
 assert(!maps && draws==1 && arg_maps==1); // preserve tess fallback
 in.CountBuffer=nullptr;native_draws=maps=arg_maps=draws=0;
 assert(run(&owner,&in,4)==S_OK);assert(!maps && draws==1 && !arg_maps);
 in.Buffer=nullptr;draws=0;assert(run(&owner,&in,4)==S_OK);assert(draws==1 && !maps);
}
'''
        code=code.replace('#include <cassert>','#include <cassert>\n#include <initializer_list>')
        with tempfile.TemporaryDirectory() as directory:
            p=Path(directory);(p/'test.cpp').write_text(code)
            result=subprocess.run(['clang++','-std=c++17','-Wall','-Wextra','-Werror','-Wno-reorder-init-list','-fsanitize=address,undefined',str(p/'test.cpp'),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
