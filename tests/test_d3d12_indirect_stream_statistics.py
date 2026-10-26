"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A stream with an active D3D12 statistics heap must count every executed record,
not just the CPU template, and must not count records beyond min(count, max).
Run the production kernel with native packing and real atomic host additions.
Literal topology expectations are independent of the production calculation.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d12_indirect_stream import PROGRAM, WINSYS, local_tree

STATS = r'''
static void stats(unsigned mode,unsigned vertices,unsigned instances,unsigned prims,unsigned index,unsigned limit,unsigned count,bool counted) {
 unsigned enabled=counted&&count<limit?count:limit;
 unsigned stride=32,slot=agx_win32_stream_record_bytes(index);
 /* Allocate arguments for executed records only. ASan catches any read of
  * the terminator/inactive record, including count=0 and rounded-up lanes. */
 uint32_t *args=calloc(enabled?enabled:1,stride);
 uint32_t *out=calloc(limit*slot+8,1);uint8_t *usc=calloc(limit?limit:1,64),tmpl[8]={0};
 for(unsigned i=0;i<enabled;++i){args[i*stride/4]=vertices;args[i*stride/4+1]=instances;}
 uint64_t counters[5]={11,13,17,19,23};
 struct agx_win32_stream_params p={.stream=(uintptr_t)out,.pipelines=(uintptr_t)usc,.usc_template=(uintptr_t)tmpl,
 .arguments=(uintptr_t)args,.count=counted?(uintptr_t)&count:0,.max_count=limit,.argument_stride=stride,
 .usc_bytes=8,.usc_stride=64,.params_offset=UINT32_MAX,.index_size=index,.topology=AGX_PRIMITIVE_TRIANGLES,
 .mode=mode,.ia_vertices=(uintptr_t)&counters[0],.ia_primitives=(uintptr_t)&counters[1],
 .vs_invocations=(uintptr_t)&counters[2],.c_primitives=(uintptr_t)&counters[3],.c_invocations=(uintptr_t)&counters[4]};
 for(unsigned repeat=0;repeat<2;++repeat){
  for(unsigned i=limit+32;i-- > 0;){cl_global_id.x=i;libagx_win32_indirect_stream(&p);}
  uint32_t v=vertices*instances*enabled*(repeat+1),q=prims*instances*enabled*(repeat+1);
  assert(counters[0]==(uint32_t)(11+v)&&counters[1]==(uint32_t)(13+q)&&counters[2]==(uint32_t)(17+v));
  assert(counters[3]==(uint32_t)(19+q)&&counters[4]==(uint32_t)(23+q));
 }
 /* Disabled counters must never dereference their addresses. No need to map
  * the arguments when only GPU VDM consumes them and there are no counters. */
 p.ia_vertices=p.ia_primitives=p.vs_invocations=p.c_primitives=p.c_invocations=0;
 free(args);p.arguments=0x12340000;
 for(unsigned i=0;i<=limit;++i){cl_global_id.x=i;libagx_win32_indirect_stream(&p);}
 free(out);free(usc);
}
int main(void){
 struct {unsigned mode,vertices,prims;} cases[]={
 {MESA_PRIM_POINTS,7,7},{MESA_PRIM_LINES,7,3},{MESA_PRIM_LINE_STRIP,7,6},
 {MESA_PRIM_TRIANGLES,7,2},{MESA_PRIM_TRIANGLE_STRIP,7,5},
 {MESA_PRIM_TRIANGLE_FAN,7,5},{MESA_PRIM_LINE_LOOP,7,7},
 {MESA_PRIM_QUADS,7,1},{MESA_PRIM_QUAD_STRIP,7,2},
 {MESA_PRIM_TRIANGLES,0,0},{MESA_PRIM_TRIANGLE_STRIP,1,0},
 {MESA_PRIM_TRIANGLE_STRIP,2,0},{MESA_PRIM_LINE_STRIP,1,0}};
 unsigned counts[]={0,1,3,5,UINT32_MAX},indices[]={0,2,4};
 for(unsigned k=0;k<sizeof(cases)/sizeof(cases[0]);++k)for(unsigned i=0;i<3;++i)for(unsigned c=0;c<5;++c)
  for(unsigned inst=0;inst<=3;inst+=3)stats(cases[k].mode,cases[k].vertices,inst,cases[k].prims,indices[i],5,counts[c],true);
 stats(MESA_PRIM_TRIANGLES,6,3,2,4,12000,11999,true);
 stats(MESA_PRIM_TRIANGLES,6,3,2,0,33,0,false);
 stats(MESA_PRIM_POINTS,3,UINT32_MAX,3,4,2,2,true);
 return 0;
}
'''

class IndirectStreamStatistics(unittest.TestCase):
    def test_real_kernel_accumulates_only_emitted_records(self):
        mesa=local_tree('reference','mesa');build=local_tree('accelerated-desktop-ad03','mesa-build')
        prefix=PROGRAM[:PROGRAM.index('static void run(')]
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'test.c').write_text(prefix+STATS)
            cmd=['clang','-std=c11','-Werror','-Wno-unused-function','-Wno-c23-extensions',
                 '-fsanitize=address,undefined','-DNDEBUG','-Dstatic_assert=_Static_assert',
                 '-DHAVE___BUILTIN_CLZ','-DHAVE_STRUCT_TIMESPEC','-DHAVE_PTHREAD']
            cmd+=['-I'+str(x) for x in (WINSYS,mesa/'src',mesa/'include',build/'src',mesa/'src/asahi/libagx',build/'src/asahi/genxml')]
            r=subprocess.run(cmd+[str(p/'test.c'),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            r=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)

if __name__=='__main__': unittest.main()
