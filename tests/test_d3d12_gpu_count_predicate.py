"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP1965 fallback predicated and CPU-encoded MaxCount records (including zero
records) while statistics queries were active. Execute the production fallback:
map only the count, preserve the argument resource, and encode min(count, max).
No predicate scratch/kernel survives on this route, even for large MaxCount.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d10_shader_fields_init import project, ROOT
from test_d3d12_owner_allocation_failure import function

HELPER = ROOT/'drivers/apple-agx/mesa/winsys/agx_win32_counted_indirect.h'

PRELUDE = '#include "'+str(ROOT/'drivers/apple-agx/mesa/winsys/agx_win32_wait.h')+'"\n'+r'''
#include <assert.h>
#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>
#define MIN2(a,b) ((a)<(b)?(a):(b))
#define PIPE_MAP_READ 1
struct pipe_resource {unsigned width0;};
struct pipe_draw_info {unsigned index_size;};
struct pipe_draw_start_count_bias {unsigned unused;};
struct pipe_draw_indirect_info {struct pipe_resource *buffer,*indirect_draw_count;unsigned offset,stride,draw_count,indirect_draw_count_offset;};
struct pipe_context {void (*draw_vbo)(struct pipe_context *,const struct pipe_draw_info *,unsigned,const struct pipe_draw_indirect_info *,const struct pipe_draw_start_count_bias *,unsigned);};
struct agx_context {struct pipe_context base;bool any_faults;int (*windows_draw_bindings)(void *);unsigned windows_indirect_path,windows_indirect_reason,windows_indirect_encode_us;};
struct pipe_transfer {unsigned unused;};
static struct pipe_transfer transfer;
static struct pipe_resource arguments,counter;
static unsigned value,maps,unmaps,encoded,calls,fault,expected_stride,wait_begins,wait_ends;
static unsigned wait_begin(unsigned site){assert(site==26);++wait_begins;return 1;}
static void wait_end(unsigned token){assert(token==1);++wait_ends;}
unsigned (*AgxWin32WaitBeginHook)(unsigned)=wait_begin;
void (*AgxWin32WaitEndHook)(unsigned)=wait_end;
static int bind(void *p){(void)p;return 1;}
static void AgxWin32AsahiMarkContextFault(struct agx_context *c,unsigned site){assert(site);c->any_faults=true;}
static const uint32_t *pipe_buffer_map_range(struct pipe_context *p,struct pipe_resource *r,unsigned offset,unsigned bytes,unsigned flags,struct pipe_transfer **t){
 struct agx_context *c=(void *)p;assert(!c->windows_draw_bindings);
 assert(r==&counter&&offset==12&&bytes==4&&flags==PIPE_MAP_READ);++maps;
 *t=fault==1||fault==2?NULL:&transfer;
 if(fault==4)c->any_faults=true;
 return fault==1||fault==3?NULL:&value;
}
static void pipe_buffer_unmap(struct pipe_context *p,struct pipe_transfer *t){assert(!((struct agx_context *)p)->windows_draw_bindings&&t==&transfer);++unmaps;}
static void draw(struct pipe_context *p,const struct pipe_draw_info *info,unsigned id,const struct pipe_draw_indirect_info *in,const struct pipe_draw_start_count_bias *r,unsigned n){
 (void)info;(void)r;struct agx_context *c=(void *)p;
 assert(c->windows_draw_bindings==bind&&maps==1&&unmaps==1&&id==7&&n==1);
 assert(in->buffer==&arguments&&in->offset==28&&in->stride==expected_stride);
 assert(!in->indirect_draw_count&&!in->indirect_draw_count_offset);
 ++calls;encoded+=in->draw_count;
 /* Native recursion may overwrite metrics, including a stream for a smaller
  * native draw; the outer fallback must report the original reason. */
 c->windows_indirect_path=3;c->windows_indirect_reason=4;c->windows_indirect_encode_us=99;
 if(fault==5)c->any_faults=true;
}
'''
PROGRAM = r'''
int main(void){
 unsigned limits[]={1,33,2400,12000,1000000},values[]={0,1,3,33,UINT32_MAX};
 for(unsigned index=0;index<=4;index+=4)for(unsigned l=0;l<5;++l)for(unsigned v=0;v<5;++v)for(fault=0;fault<6;++fault){
  value=values[v];maps=unmaps=encoded=calls=wait_begins=wait_ends=0;expected_stride=index?20:16;
  arguments.width0=28+limits[l]*expected_stride;counter.width0=16;
  struct agx_context c={{draw},false,bind,0,3,0};struct pipe_draw_info info={index};
  struct pipe_draw_indirect_info in={&arguments,&counter,28,0,limits[l],12};
  agx_win32_draw_indirect_count(&c,&info,7,&in,NULL);
  assert(maps==1&&unmaps==(fault>=3||!fault));
  assert(wait_begins==1 && wait_ends==1);
  assert(c.windows_draw_bindings==bind&&c.windows_indirect_path==2&&c.windows_indirect_reason==3&&!c.windows_indirect_encode_us);
  unsigned expected=values[v]<limits[l]?values[v]:limits[l];
  assert(encoded==(fault>=1&&fault<=4?0:expected));
  assert(calls==!!encoded);
  assert(c.any_faults==((fault>=1&&fault<=4)||(fault==5&&expected)));
 }
 for(unsigned bad=0;bad<9;++bad){
  maps=unmaps=encoded=calls=0;fault=0;arguments.width0=1000;counter.width0=16;
  struct agx_context c={{draw},false,bind,0,1,0};struct pipe_draw_info info={4};
  struct pipe_draw_indirect_info in={&arguments,&counter,28,32,3,12};
  if(bad==0)in.offset=UINT32_MAX;if(bad==1)in.indirect_draw_count_offset=UINT32_MAX;
  if(bad==2)in.stride=19;if(bad==3)in.draw_count=UINT32_MAX;
  if(bad==4)in.stride=30;if(bad==5)in.offset=27;
  if(bad==6)in.indirect_draw_count_offset=11;if(bad==7)in.buffer=NULL;
  if(bad==8)in.indirect_draw_count=NULL;
  agx_win32_draw_indirect_count(&c,&info,7,&in,NULL);
  assert(c.any_faults&&!encoded&&!maps&&c.windows_draw_bindings==bind);
 }
 struct agx_context c={{draw},false,bind,0,0,0};struct pipe_draw_info info={4};
 struct pipe_draw_indirect_info empty={0};maps=encoded=0;
 agx_win32_draw_indirect_count(&c,&info,7,&empty,NULL);
 assert(!c.any_faults&&!encoded&&!maps&&c.windows_draw_bindings==bind);
}
'''

class GpuCountPredicate(unittest.TestCase):
    def test_count_bounded_fallback_and_failed_maps(self):
        helper='\n'.join(s for s in HELPER.read_text().splitlines() if not s.startswith('#include'))
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'test.c').write_text(PRELUDE+helper+PROGRAM)
            r=subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',str(p/'test.c'),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            r=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)

    def test_projection_routes_before_graphics_batch_entry(self):
        with tempfile.TemporaryDirectory() as td:
            out=project(Path(td)/'out')
            state=(out/'src/gallium/drivers/asahi/agx_state.c').read_text()
            wrapper=function(state[state.rindex('static void\nagx_draw_vbo('):], 'static void\nagx_draw_vbo(')
            # Execute the projected routing block, retaining its actual branches.
            routing=wrapper[wrapper.index('   if (indirect && indirect->indirect_draw_count) {'):wrapper.index('   /* EXP1189:')]
            code='''#include <assert.h>
#include <stddef.h>
#define MESA_PRIM_PATCHES 14
struct ctx {unsigned windows_indirect_path;} c;
struct info {unsigned mode;} i;
struct indirect {void *indirect_draw_count; unsigned draw_count;} in;
static unsigned native,entered;
#define perf_debug_ctx(...) ((void)0)
#define agx_win32_draw_indirect_count(...) (++native)
static void route(struct ctx *ctx,struct info *info,struct indirect *indirect){
'''+routing+'''++entered;}
int main(void){for(unsigned mode=0;mode<=MESA_PRIM_PATCHES;++mode){i.mode=mode;for(unsigned max=1;max<=12000;max+=11999){native=entered=0;in.indirect_draw_count=&in;in.draw_count=max;route(&c,&i,&in);assert(native==1&&!entered&&c.windows_indirect_path!=1);}}}
'''
            p=Path(td);(p/'route.c').write_text(code)
            r=subprocess.run(['clang','-std=c11',str(p/'route.c'),'-o',str(p/'route')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            r=subprocess.run([str(p/'route')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)

if __name__=='__main__':unittest.main()
