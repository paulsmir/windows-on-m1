"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
RC32 encodes MaxCount draws on the CPU. The replacement must generate bounded
VDM records on the GPU, link back at the clamped count without a return stack, and select a distinct
USC parameter address for every draw (including negative base vertex and
nonzero first instance). rc79: a grid smaller than MaxCount + 1 (threads
striding over the records) must produce byte-identical records, terminator
and untouched tails, so the dispatch need not scale with MaxCount. Execute the production kernel with the real Mesa
pack helpers under ASan/UBSan; no substitute stream encoder is used.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_d3d10_shader_fields_init import ROOT, local_tree

WINSYS = ROOT/'drivers/apple-agx/mesa/winsys'
KERNEL = WINSYS/'agx_win32_indirect_stream.cl'

PROGRAM = r'''
#include <assert.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
typedef unsigned uint;
#define global
#define constant const
static struct { unsigned x; } cl_global_id;
#include "compiler/libcl/libcl.h"
#undef KERNEL
#define KERNEL(n) void
#define atomic_add(p,v) __atomic_fetch_add((p),(v),__ATOMIC_RELAXED)
#include "agx_win32_indirect_stream.cl"
#undef NDEBUG
#include <assert.h>

static void run_threads(unsigned indexed, unsigned limit, unsigned value, bool counted,
                unsigned param_uniform, unsigned threads) {
   const unsigned stride=36, usc_stride=128, usc_bytes=80, patch=16;
   unsigned enabled=counted && value<limit ? value : limit;
   unsigned slot=agx_win32_stream_record_bytes(indexed);
   unsigned bytes=limit*slot+AGX_VDM_STREAM_LINK_LENGTH;
   uint32_t *out=malloc(bytes+32), *args=malloc(stride*limit+64);
   uint8_t *usc=malloc(usc_stride*limit+32), tmpl[80];
   memset(out,0xcd,bytes+32); memset(usc,0xab,usc_stride*limit+32);
   memset(tmpl,0x12,sizeof(tmpl));
   for(unsigned i=0;i<limit;++i) {
      uint32_t *a=(void *)((uint8_t *)args+i*stride+12);
      a[0]=3+i; a[1]=1+i%7; a[2]=5+i;
      a[3]=indexed ? (uint32_t)(-17+(int)i) : 23+i;
      a[4]=61+i;
   }
   struct agx_win32_stream_params p={
      .stream=(uintptr_t)out, .pipelines=(uintptr_t)usc,
      .continuation=0x123456789cull,
      .usc_template=(uintptr_t)tmpl, .arguments=(uintptr_t)args+12,
      .count=counted ? (uintptr_t)&value : 0,
      .index_buffer=0x1234567800ull, .max_count=limit,
      .argument_stride=stride, .usc_bytes=usc_bytes, .usc_stride=usc_stride,
      .params_offset=param_uniform==UINT32_MAX ? UINT32_MAX : patch,
      .params_uniform=param_uniform,
      .params_data=agx_win32_stream_params_data(usc_bytes),
      .pipeline_base=0x10000, .index_range=4096,
      .index_size=indexed, .restart=1, .topology=AGX_PRIMITIVE_TRIANGLE_STRIP,
      /* RC69: the template's native VS block; word 1 is replaced per record. */
      .vs_state={0x4000002eu, 0x1111u, 0xdeadbeefu, 0x0808u, 0x2u, 0},
      /* rc79: 0 = one record per work-item (grid MaxCount + 1); otherwise a
       * grid of `threads` work-items strides over the records. */
      .threads=threads,
   };
   /* Reverse work-item order also proves the terminator has no race with slots. */
   for(unsigned i=limit+32;i-- > 0;) {
      cl_global_id.x=i; libagx_win32_indirect_stream(&p);
   }
   for(unsigned i=0;i<enabled;++i) {
      uint32_t *s=(void *)((uint8_t *)out+i*slot);
      assert(s[0]==p.vs_state[0] && s[1]==p.vs_state[1] &&
             s[2]==p.pipeline_base+i*usc_stride && s[3]==p.vs_state[3] &&
             s[4]==p.vs_state[4] && s[5]==p.vs_state[5]);
      uint64_t arg=p.arguments+(uint64_t)i*stride;
      uint32_t expected[8]={0};
      struct agx_draw d=indexed ?
         agx_draw_indexed_indirect(arg,p.index_buffer,p.index_range,
                                  agx_translate_index_size(indexed),true) :
         agx_draw_indirect(arg);
      uint32_t *end=agx_vdm_draw(expected,0,d,p.topology);
      assert((end-expected)*4+AGX_WIN32_STREAM_VS_STATE_WORDS*4==slot);
      assert(!memcmp(s+AGX_WIN32_STREAM_VS_STATE_WORDS,expected,
                     slot-AGX_WIN32_STREAM_VS_STATE_WORDS*4));
      uint8_t check[80];memcpy(check,tmpl,80);
      unsigned untouched=usc_bytes;
      if(param_uniform!=UINT32_MAX) {
         /* RC69: the load reads this record's private copy of its last two
          * argument words, stored right after the USC words. */
         uint64_t copy=p.pipelines+(uint64_t)i*usc_stride+p.params_data;
         struct agx_usc_builder b=agx_usc_builder(check+patch,8);
         agx_usc_uniform(&b,param_uniform,4,copy);
         assert(p.params_data>=usc_bytes&&p.params_data+8<=usc_stride);
         assert(!memcmp((uint8_t *)(uintptr_t)copy,(uint8_t *)(uintptr_t)(arg+(indexed?12:8)),8));
         untouched=p.params_data+8;
      }
      assert(!memcmp(usc+i*usc_stride,check,usc_bytes));
      for(unsigned k=untouched;k<usc_stride;++k)
         assert(usc[i*usc_stride+k]==0xab);
   }
   assert(out[enabled*slot/4]==0x80000012u);
   assert(out[enabled*slot/4+1]==0x3456789cu);
   for(unsigned k=enabled*slot+8;k<bytes+32;++k)
      assert(((uint8_t *)out)[k]==0xcd);
   for(unsigned k=enabled*usc_stride;k<limit*usc_stride+32;++k)
      assert(usc[k]==0xab);
   free(out);free(args);free(usc);
}
static void run(unsigned indexed, unsigned limit, unsigned value, bool counted,
                unsigned param_uniform) {
   run_threads(indexed, limit, value, counted, param_uniform, 0);
   /* rc79: a bounded grid writes exactly the same records and terminator. */
   run_threads(indexed, limit, value, counted, param_uniform, 1);
   run_threads(indexed, limit, value, counted, param_uniform, 3);
   run_threads(indexed, limit, value, counted, param_uniform, 32);
}
int main(void) {
   /* RC43 hardware-probe matrix: MaxCount2, both index sizes, count0/1/2. */
   for (unsigned index = 2; index <= 4; index += 2)
      for (unsigned count = 0; count <= 2; ++count)
         run(index, 2, count, true, 12);
   unsigned values[]={0,1,16,17,18,UINT32_MAX};
   unsigned indices[]={0,2,4};
   for(unsigned i=0;i<3;++i)for(unsigned n=0;n<6;++n) {
      run(indices[i],17,values[n],true,12);
      run(indices[i],17,values[n],false,268);
   }
   run(4,12000,11999,true,12);
   run(2,17,3,true,UINT32_MAX);
   run(0,0,0,true,12);
   return 0;
}
'''

class IndirectStream(unittest.TestCase):
    def test_production_kernel_bounds_and_per_draw_uniforms(self):
        self.assertTrue(KERNEL.exists(), 'GPU VDM stream kernel is missing')
        mesa=local_tree('reference','mesa')
        build=local_tree('accelerated-desktop-ad03','mesa-build')
        self.assertIsNotNone(mesa)
        self.assertIsNotNone(build)
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'test.c').write_text(PROGRAM)
            cmd=['clang','-std=c11','-Werror','-Wno-unused-function','-Wno-c23-extensions',
                 '-fsanitize=address,undefined','-DNDEBUG',
                 '-Dstatic_assert=_Static_assert','-DHAVE___BUILTIN_CLZ',
                 '-DHAVE_STRUCT_TIMESPEC','-DHAVE_PTHREAD',
                 '-I'+str(WINSYS),'-I'+str(mesa/'src'),
                 '-I'+str(mesa/'include'),'-I'+str(build/'src'),
                 '-I'+str(mesa/'src/asahi/libagx'),
                 '-I'+str(build/'src/asahi/genxml'),
                 str(p/'test.c'),'-o',str(p/'test')]
            # Keep harness assertions active even though the shared builder's
            # debug fields are unavailable to OpenCL and not part of its ABI.
            (p/'test.c').write_text(PROGRAM.replace('#include "agx_win32_indirect_stream.cl"',
                 '#include "agx_win32_indirect_stream.cl"\n#undef NDEBUG\n#include <assert.h>'))
            result=subprocess.run(cmd,capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)

if __name__=='__main__': unittest.main()
