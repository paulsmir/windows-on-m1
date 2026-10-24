"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Pre-draw hazard legalization can submit the batch holding D3D12 heap resources
and sampler handles. Execute the real owner call site, heap binder, native
sampler allocator and projected draw wrapper; encoding must use the new batch's
references/handles. Flush/encoding callbacks simulate the Gallium boundary.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from g3_vidmm_replay import body
from test_d3d12_owner_allocation_failure import function
from test_d3d12_heap_batch_rollover import HEADER, PROGRAM, WINSYS, ROOT
from test_d3d10_shader_fields_init import project

DRAW = r'''
using LONGLONG=long long;
struct pipe_draw_info {unsigned instance_count;};
struct pipe_draw_start_count_bias {unsigned count;};
struct pipe_draw_indirect_info {bool count_from_stream_output;};
static unsigned encoded,meta_encoded,mode,depth;
static bool legalizing,legalized,condition=true;
static long long qpc_now(){return 0;}
#define unlikely(x) (x)
#define AGX_WIN32_DRAW_PHASE(x) ((void)(x))
static void AgxWin32AsahiPublishPages(struct agx_device *){}
static void AgxWin32AsahiBatchTraceDraw(agx_context *,agx_batch *,unsigned){}
static bool AgxWin32AsahiBatchDrawAllowed(agx_context *c,const pipe_draw_info *,unsigned,const pipe_draw_indirect_info *,const pipe_draw_start_count_bias *,unsigned){return !c->any_faults;}
static bool AgxWin32AsahiBatchDrawRefused(agx_context *,const pipe_draw_info *,const pipe_draw_indirect_info *,const pipe_draw_start_count_bias *,unsigned){return false;}
static bool AgxWin32AsahiBatchPrepareDraw(agx_batch *,const pipe_draw_info *,const pipe_draw_indirect_info *,const pipe_draw_start_count_bias *){return true;}
static bool AgxWin32AsahiBatchEnter(agx_batch *){return true;}
static bool AgxWin32AsahiBatchLeave(agx_batch *){return true;}
static agx_batch *agx_get_batch(agx_context *){return &batches[current];}
static bool agx_render_condition_check(agx_context *){return condition;}
static void agx_legalize_feedback_loops(agx_context *);
static void agx_legalize_xfb(agx_context *c){
 // XFB legalization can flush any new readers registered since the last call.
 if(!legalizing && batches[current].reads)
  agx_flush_batch_for_reason(c,&batches[current],"simulated XFB readers");
 agx_legalize_feedback_loops(c);
}
static void agx_draw_vbo_windows_body(pipe_context *,const pipe_draw_info *,unsigned,const pipe_draw_indirect_info *,const pipe_draw_start_count_bias *,unsigned);
@@WRAPPER@@
static void agx_legalize_feedback_loops(agx_context *c){
 if(legalized || legalizing)return;
 legalized=true;
 if(mode==6){c->any_faults=true;return;}
 if(mode==0 || mode==1 || mode==2 || mode==7){
  legalizing=true;
  if(mode==1){
   pipe_draw_info info={1};pipe_draw_start_count_bias range={3};
   agx_draw_vbo(&c->base,&info,0,nullptr,&range,1);
  }
  agx_flush_batch_for_reason(c,&batches[current],"simulated hazard");
  legalizing=false;
 }
}
static void agx_draw_vbo_windows_body(pipe_context *p,const pipe_draw_info *info,unsigned id,const pipe_draw_indirect_info *indirect,const pipe_draw_start_count_bias *draws,unsigned n){
 if(legalizing){assert(!batches[current].reads && !batches[current].writes);++meta_encoded;return;}
 if(mode==7 && !depth){++depth;legalized=false;agx_draw_vbo(p,info,id,indirect,draws,n);--depth;return;}
 struct agx_context *ctx=agx_context(p);
@@BODY_PREPARE@@
 (void)batch;
 if(mode!=5){
  assert(batches[current].reads==3u && batches[current].writes==3u);
  for(unsigned s=0;s<2;++s)for(unsigned i=0;i<3;++i){
   unsigned handle=uploaded[s][4+i]&2047u;
   assert(handle>=28 && handle-28<batches[current].sampler_heap.count);
   assert(bos[current].entries[handle-28].bytes[0]==20+i);
  }
 }
 ++encoded;
}
@@CALLBACK@@
@@SETTER@@
static HRESULT owner_draw(AGX_D3D12_OWNER *owner,pipe_draw_info &info,pipe_draw_start_count_bias &range){
 pipe_context *pipe=owner->Context;HRESULT hr=S_OK;
 struct {bool Buffer;} in_value={false};auto *in=&in_value;
 pipe_draw_indirect_info indirect={};
@@CALLSITE@@
 (void)bound;(void)drawn;return hr;
}
int main(int argc,char **argv){
 assert(argc==2);mode=atoi(argv[1]);
 agx_context ctx={};ctx.base.set_constant_buffer=set_cb;ctx.base.draw_vbo=agx_draw_vbo;
 AGX_D3D12_OWNER owner={};owner.Context=&ctx.base;
 pipe_resource rr[2]={{0},{1}};AGX_D3D12_BUFFER rb[2]={{&rr[0],65536},{&rr[1],65536}};
 AGX_D3D12_HEAP_SAMPLER samplers[3]={};
 for(unsigned i=0;i<3;++i){samplers[i].Packed[0]=20+i;samplers[i].Valid=1;}
 for(unsigned s=0;s<2;++s){auto &h=owner.PendingHeaps[s];h.Heap=&rb[s];h.Textures=&rb[s];h.TextureCount=1;h.Writes=&rb[s];h.WriteCount=1;h.Samplers=samplers;h.SamplerCount=3;}
 if(mode==2)fail_alloc=1;
 if(mode==3)condition=false;
 pipe_draw_info info={1};pipe_draw_start_count_bias range={mode==4?0u:3u};
 HRESULT hr;
 if(mode==5){agx_draw_vbo(&ctx.base,&info,0,nullptr,&range,1);hr=S_OK;}
 else hr=owner_draw(&owner,info,range);
 if(mode==2 || mode==6){assert(FAILED(hr));assert(!encoded);}
 else {assert(hr==S_OK);assert(encoded==(mode==3 || mode==4?0u:1u));}
 assert(meta_encoded==(mode==1?1u:0u));
 if(mode==3 || mode==4)assert(!sets);
 assert(!ctx.windows_draw_bindings && !ctx.windows_draw_bindings_data);
 assert(!live);
}
'''

class HeapDrawHazards(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp=tempfile.TemporaryDirectory(prefix='heap-draw-projection-')
        cls.addClassCleanup(cls.tmp.cleanup)
        cls.out=project(Path(cls.tmp.name)/'out')
        if cls.out is None:
            raise RuntimeError('Pinned Mesa projection inputs required')

    def test_projected_context_preserves_pipe_downcast(self):
        """The real projected header must keep Gallium's base at offset zero."""
        with tempfile.TemporaryDirectory(prefix='heap-context-layout-') as directory:
            p=Path(directory)
            h=(self.out/'src/gallium/drivers/asahi/agx_state.h').read_text()
            start=h.index('struct agx_context {')
            # Compile the actual layout prefix, including base and callbacks.
            prefix=h[start:h.index('   struct agx_compiled_shader *vs',start)]+'};\n'
            source='#include <stddef.h>\nstruct pipe_context {void *screen;};\n'+prefix
            source+='_Static_assert(offsetof(struct agx_context,base)==0,"pipe downcast ABI");\n'
            source+='int main(void){return 0;}\n'
            (p/'layout.c').write_text(source)
            run=subprocess.run(['clang','-std=c11','-Wall','-Werror',str(p/'layout.c'),'-o',str(p/'layout')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_actual_draw_rebinds_after_hazards(self):
        state=(self.out/'src/gallium/drivers/asahi/agx_state.c').read_text()
        wrapper=body(state,'agx_draw_vbo')
        native_body=body(state,'agx_draw_vbo_windows_body')
        start=native_body.index('   /* We must legalize feedback loops')
        end=native_body.index('   uint64_t ib = 0;',start)
        preparation=native_body[start:end]
        owner=(WINSYS/'agx_d3d12_windows.cpp').read_text()
        scene=(WINSYS/'agx_win32_asahi_scene.c').read_text()
        start=owner.index('pipe->set_stream_output_targets(pipe, so_count, so, append, mode);')
        start=owner.index('\n  }',start)+4
        end=owner.index('\n  unbind_heaps(owner, false);',start)
        callback=''
        if 'struct AgxD3d12DrawHeapBindings' in owner:
            a=owner.index('struct AgxD3d12DrawHeapBindings')
            callback=function(owner,'static void validate_heap_refs(')+owner[a:owner.index('static void unbind_heaps',a)]
        setter=body(scene,'AgxWin32AsahiSetDrawBindings') if 'AgxWin32AsahiSetDrawBindings(' in scene else ''
        header=HEADER.replace('struct pipe_context {','struct pipe_draw_info;struct pipe_draw_indirect_info;struct pipe_draw_start_count_bias;\nstruct pipe_context {void (*draw_vbo)(struct pipe_context *,const struct pipe_draw_info *,unsigned,const struct pipe_draw_indirect_info *,const struct pipe_draw_start_count_bias *,unsigned);')
        header=header.replace('bool any_faults;','bool any_faults;struct agx_batch *batch;int (*windows_draw_bindings)(void *);void *windows_draw_bindings_data;')
        prelude=PROGRAM[:PROGRAM.index('int main(')].replace('@@OWNER@@',function(owner,'static HRESULT bind_heap_samplers(')+function(owner,'static HRESULT bind_heaps('))
        native=function(scene,'static unsigned heap_sampler_headroom(')+body(scene,'AgxWin32AsahiBatchSamplerHandles')
        draw=DRAW.replace('@@WRAPPER@@',wrapper).replace('@@CALLBACK@@',callback).replace('@@SETTER@@',setter).replace('@@CALLSITE@@',owner[start:end]).replace('@@BODY_PREPARE@@',preparation)
        with tempfile.TemporaryDirectory(prefix='heap-draw-hazards-') as directory:
            p=Path(directory)
            (p/'fixture.h').write_text(header)
            (p/'native.c').write_text('#include "fixture.h"\n'+native)
            (p/'draw.cpp').write_text(prelude+draw)
            flags=['-Wall','-Wextra','-Werror','-fsanitize=address,undefined']
            for command in [
                ['clang','-std=c11',*flags,'-c',str(p/'native.c'),'-o',str(p/'native.o')],
                ['clang++','-std=c++17',*flags,'-Wno-unused-function','-Wno-unused-variable',str(p/'draw.cpp'),str(p/'native.o'),'-o',str(p/'replay')]]:
                run=subprocess.run(command,capture_output=True,text=True)
                self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            for mode in range(8):
                with self.subTest(mode=mode):
                    run=subprocess.run([str(p/'replay'),str(mode)],capture_output=True,text=True)
                    self.assertEqual(run.returncode,0,run.stdout+run.stderr)

if __name__=='__main__':unittest.main()
