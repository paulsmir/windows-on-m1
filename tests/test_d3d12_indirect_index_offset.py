"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP1976 RC43 probes bind an IB at +512. Recording rejected that legal view
before any stream ran; native indirect fetch must add the view base once,
independently of GPU StartIndexLocation and without changing another resource.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_d3d10_shader_fields_init import ROOT, project
from test_d3d12_owner_allocation_failure import function

WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys'


def run(test, directory, source, cpp=False):
    p = directory / ('test.cpp' if cpp else 'test.c')
    p.write_text(source)
    result = subprocess.run(['clang++' if cpp else 'clang',
        '-std=c++11' if cpp else '-std=c11', '-fsanitize=address,undefined',
        str(p), '-o', str(directory / 'test')], capture_output=True, text=True)
    test.assertEqual(result.returncode, 0, result.stderr)
    result = subprocess.run([str(directory / 'test')], capture_output=True, text=True)
    test.assertEqual(result.returncode, 0, result.stderr)


class IndirectIndexOffset(unittest.TestCase):
    def test_owner_scopes_native_view_even_when_draw_fails(self):
        owner = function((WINSYS / 'agx_d3d12_windows.cpp').read_text(),
                         'extern "C" HRESULT AgxD3d12OwnerDraw(')
        site = owner[owner.index('  AgxD3d12DrawHeapBindings heap_bindings'):owner.index('  if (SUCCEEDED(hr)) hr = heap_bindings.Result;')]
        setter = function((WINSYS / 'agx_win32_asahi_scene.c').read_text(),
                          'void AgxWin32AsahiSetIndirectIndexBuffer(')
        code = r'''
#include <cassert>
#include <cstddef>
using LONGLONG=long long;using HRESULT=int;
#define S_OK 0
#define SUCCEEDED(x) ((x)>=0)
struct pipe_resource {};
struct pipe_draw_info {struct {pipe_resource *resource;} index;};
struct pipe_draw_start_count_bias {};
struct pipe_draw_indirect_info {unsigned draw_count;};
struct pipe_context {void (*draw_vbo)(pipe_context*,const pipe_draw_info*,unsigned,const pipe_draw_indirect_info*,const pipe_draw_start_count_bias*,unsigned);};
struct agx_context {pipe_context base;pipe_resource *windows_indirect_index_resource;unsigned windows_indirect_index_offset;};
static agx_context *agx_context(pipe_context *p){return reinterpret_cast<struct agx_context*>(p);}
struct AGX_D3D12_OWNER {pipe_context *Context;};
struct AgxD3d12DrawHeapBindings {AGX_D3D12_OWNER *owner;HRESULT Result;};
static int bind_draw_heaps(void*){return 1;}
static LONGLONG qpc_now(){return 0;}
static void AgxWin32AsahiSetDrawBindings(pipe_context*,int (*)(void*),void*){}
''' + setter + r'''
static unsigned calls,expected_offset;static pipe_resource ib;
static void draw_cb(pipe_context *p,const pipe_draw_info *info,unsigned,const pipe_draw_indirect_info *indirect,const pipe_draw_start_count_bias*,unsigned){
 auto *c=agx_context(p);++calls;
 assert(c->windows_indirect_index_resource==(indirect?info->index.resource:nullptr));
 assert(c->windows_indirect_index_offset==(indirect?expected_offset:0));
}
static void invoke(AGX_D3D12_OWNER *owner,bool indirect_call,bool fail){
 struct {unsigned IndexSize,IndexOffset;} value={4,512};auto *draw=&value;
 struct {bool Buffer;} args={indirect_call};auto *in=&args;
 pipe_context *pipe=owner->Context;pipe_draw_info info={{&ib}};
 pipe_draw_start_count_bias range={};pipe_draw_indirect_info indirect={2};HRESULT hr=fail?-1:0;
''' + site + r'''
 (void)bound;(void)drawn;auto *ctx=agx_context(pipe);
 assert(!ctx->windows_indirect_index_resource&&!ctx->windows_indirect_index_offset);
}
int main(){struct agx_context c={};c.base.draw_vbo=draw_cb;AGX_D3D12_OWNER o={&c.base};expected_offset=512;
 invoke(&o,true,false);invoke(&o,false,false);invoke(&o,true,true);assert(calls==2);
}
'''
        with tempfile.TemporaryDirectory() as td:
            run(self, Path(td), code, True)

    def test_owner_keeps_view_offset_out_of_gpu_start_index(self):
        text = (WINSYS / 'agx_d3d12_windows.cpp').read_text()
        draw = function(text, 'extern "C" HRESULT AgxD3d12OwnerDraw(')
        setup = draw[draw.index('  struct pipe_draw_info info ='):draw.index('  /* C39: stream-output targets')]
        code = r'''
#include <cassert>
using uint8_t=unsigned char;using uint16_t=unsigned short;
#define E_INVALIDARG (-1)
#define MESA_SHADER_VERTEX 0
#define MESA_SHADER_FRAGMENT 1
#define unbind_stage(...) ((void)0)
#define release_samplers(...) ((void)0)
#define release_optional_stages(...) ((void)0)
struct pipe_resource {unsigned width0;};
struct AGX_D3D12_BUFFER {void *Resource;unsigned long long Bytes;};
struct AGX_D3D12_INDIRECT {AGX_D3D12_BUFFER *Buffer,*CountBuffer;unsigned MaxCount,Stride;unsigned long long Offset,CountOffset;};
struct Draw {unsigned StartInstance,InstanceCount,StartVertex,VertexCount,IndexSize,RestartIndex;int BaseVertex;unsigned long long IndexOffset;AGX_D3D12_BUFFER *IndexBuffer;AGX_D3D12_INDIRECT Indirect;};
struct pipe_draw_info {uint8_t mode;unsigned start_instance,instance_count;uint16_t index_size;struct {pipe_resource *resource;} index;bool primitive_restart;unsigned restart_index;};
struct pipe_draw_start_count_bias {unsigned start,count;int index_bias;};
struct pipe_draw_indirect_info {pipe_resource *buffer,*indirect_draw_count;unsigned offset,stride,draw_count,indirect_draw_count_offset;};
static int check(Draw *draw){unsigned mode=4;
''' + setup + r'''
 assert(range.start==(in->Buffer ? 3u : 3u+unsigned(draw->IndexOffset/draw->IndexSize)));
 assert(range.index_bias==-7);
 if(in->Buffer)assert(indirect.buffer==in->Buffer->Resource&&indirect.draw_count==2);
 return 0;}
int main(){pipe_resource resource={4096};AGX_D3D12_BUFFER ib={&resource,4096},args=ib;
 Draw d={};d.StartVertex=3;d.BaseVertex=-7;d.IndexOffset=512;d.IndexBuffer=&ib;
 d.Indirect={&args,&args,2,20,2048,2112};
 for(unsigned bytes=2;bytes<=4;bytes+=2){d.IndexSize=bytes;assert(check(&d)==0);}
 d.Indirect.Buffer=nullptr;assert(check(&d)==0);
 d.Indirect.Buffer=&args;d.IndexOffset=4096;assert(check(&d)==E_INVALIDARG);
 d.IndexOffset=513;assert(check(&d)==E_INVALIDARG);
}
'''
        with tempfile.TemporaryDirectory() as td:
            run(self, Path(td), code, True)

    def test_recorder_admits_nonzero_index_view(self):
        text = (ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp').read_text()
        record = function(text, 'static void RecordDraw(')
        refusal = record[record.index('    UINT refusal ='):record.index('    AgxD3d12Dispatch')]
        code = r'''
#include <cassert>
using UINT=unsigned;
const unsigned kListCommands=64,kDescriptorDsv=3;
struct Pso {unsigned vs_dwords=1,hs_dwords=0,ds_dwords=0,gs_dwords=0;struct {bool declared=false;} so;};
struct List {unsigned topology=4,rtvs=1,index_offset=512,commands=0;void *index_heap=(void*)1;struct {void *heap=(void*)1;} rtv[1];struct {unsigned kind=0;} dsv;};
static unsigned check(List *list,Pso *pso,bool indexed,bool indirect){
''' + refusal + r'''return refusal;}
int main(){List l;Pso p;
 for(unsigned bytes=2;bytes<=4;bytes+=2)for(unsigned count=0;count<=2;++count){
  (void)bytes;(void)count;assert(check(&l,&p,true,true)==0);
 }
 l.index_heap=nullptr;assert(check(&l,&p,true,true)==6);
 l.index_heap=(void*)1;l.commands=64;assert(check(&l,&p,true,true)==3);
}
'''
        with tempfile.TemporaryDirectory() as td:
            run(self, Path(td), code, True)

    def test_native_view_base_and_extent_follow_only_original_resource(self):
        with tempfile.TemporaryDirectory() as td:
            p = Path(td); out = project(p / 'native')
            state = (out / 'src/gallium/drivers/asahi/agx_state.c').read_text()
            helper = function(state, 'static uint64_t\nagx_index_buffer_rsrc_ptr(')
            code = r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#include <stdbool.h>
#define ALIGN_POT(x,a) (((x)+(a)-1)&~((a)-1))
struct pipe_resource {unsigned width0;};
struct agx_resource {struct pipe_resource base;struct {size_t size_B,level_offsets_B[1];} layout;uint64_t gpu;};
struct agx_context {struct pipe_resource *windows_indirect_index_resource;unsigned windows_indirect_index_offset;bool any_faults;};
struct agx_batch {struct agx_context *ctx;};
struct pipe_draw_info {bool has_user_indices;unsigned index_size;union {struct pipe_resource *resource;} index;};
static struct agx_resource *last_read;
static struct agx_resource *agx_resource(struct pipe_resource *p){return (void*)p;}
static void agx_batch_reads(struct agx_batch *b,struct agx_resource *r){(void)b;last_read=r;}
static uint64_t agx_map_gpu(struct agx_resource *r){return r->gpu;}
static void AgxWin32AsahiMarkContextFault(struct agx_context *c,unsigned s){assert(s);c->any_faults=true;}
''' + helper + r'''
int main(void){
 struct agx_resource r={{4096},{4096,{0}},0x1100040000ull};
 struct agx_resource replacement={{128},{128,{0}},0x1100080000ull};
 struct agx_context c={&r.base,512,false};struct agx_batch b={&c};
 struct pipe_draw_info info={false,2,{&r.base}};size_t extent;
 for(unsigned bytes=2;bytes<=4;bytes+=2)for(unsigned count=0;count<=2;++count){
  info.index_size=bytes;
  uint64_t base=agx_index_buffer_rsrc_ptr(&b,&info,&extent);
  assert(!c.any_faults&&base==0x1100040200ull&&extent==3584&&last_read==&r);
  /* StartIndexLocation is relative to the rebased view, including record 2. */
  for(unsigned i=0;i<count;++i)assert(base+(3+i)*bytes==0x1100040200ull+(3+i)*bytes);
 }
 info.index.resource=&replacement.base;
 assert(agx_index_buffer_rsrc_ptr(&b,&info,&extent)==replacement.gpu&&extent==128);
 info.index.resource=&r.base;c.windows_indirect_index_resource=NULL;c.windows_indirect_index_offset=0;
 assert(agx_index_buffer_rsrc_ptr(&b,&info,&extent)==r.gpu&&extent==4096);
 c.windows_indirect_index_resource=&r.base;c.windows_indirect_index_offset=4096;
 assert(!agx_index_buffer_rsrc_ptr(&b,&info,&extent)&&c.any_faults);
 c.any_faults=false;c.windows_indirect_index_offset=3;
 assert(!agx_index_buffer_rsrc_ptr(&b,&info,&extent)&&c.any_faults);
}
'''
            run(self, p, code)
