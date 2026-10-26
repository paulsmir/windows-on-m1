"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
RC32 encodes MaxCount draws on the CPU. The replacement must generate bounded
VDM records on the GPU, terminate at the clamped count, and select a distinct
USC parameter address for every draw (including negative base vertex and
nonzero first instance). Execute the production kernel with the real Mesa
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

static void run(unsigned indexed, unsigned limit, unsigned value, bool counted,
                unsigned param_uniform) {
   const unsigned stride=36, usc_stride=128, usc_bytes=80, patch=16;
   unsigned enabled=counted && value<limit ? value : limit;
   unsigned slot=agx_win32_stream_record_bytes(indexed);
   unsigned bytes=limit*slot+AGX_VDM_BARRIER_LENGTH;
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
      .usc_template=(uintptr_t)tmpl, .arguments=(uintptr_t)args+12,
      .count=counted ? (uintptr_t)&value : 0,
      .index_buffer=0x1234567800ull, .max_count=limit,
      .argument_stride=stride, .usc_bytes=usc_bytes, .usc_stride=usc_stride,
      .params_offset=param_uniform==UINT32_MAX ? UINT32_MAX : patch,
      .params_uniform=param_uniform,
      .pipeline_base=0x10000, .index_range=4096,
      .index_size=indexed, .restart=1, .topology=AGX_PRIMITIVE_TRIANGLE_STRIP,
   };
   /* Reverse work-item order also proves the terminator has no race with slots. */
   for(unsigned i=limit+32;i-- > 0;) {
      cl_global_id.x=i; libagx_win32_indirect_stream(&p);
   }
   for(unsigned i=0;i<enabled;++i) {
      uint32_t *s=(void *)((uint8_t *)out+i*slot);
      assert(s[0]==0x40000004u && s[1]==p.pipeline_base+i*usc_stride);
      uint64_t arg=p.arguments+(uint64_t)i*stride;
      uint32_t expected[8]={0};
      struct agx_draw d=indexed ?
         agx_draw_indexed_indirect(arg,p.index_buffer,p.index_range,
                                  agx_translate_index_size(indexed),true) :
         agx_draw_indirect(arg);
      uint32_t *end=agx_vdm_draw(expected,0,d,p.topology);
      assert((end-expected)*4+8==slot);
      assert(!memcmp(s+2,expected,slot-8));
      uint8_t check[80];memcpy(check,tmpl,80);
      if(param_uniform!=UINT32_MAX) {
         struct agx_usc_builder b=agx_usc_builder(check+patch,8);
         agx_usc_uniform(&b,param_uniform,4,arg+(indexed?12:8));
      }
      assert(!memcmp(usc+i*usc_stride,check,usc_bytes));
      for(unsigned k=usc_bytes;k<usc_stride;++k)
         assert(usc[i*usc_stride+k]==0xab);
   }
   assert(out[enabled*slot/4]==0x28000000u);
   for(unsigned k=enabled*slot+4;k<bytes+32;++k)
      assert(((uint8_t *)out)[k]==0xcd);
   for(unsigned k=enabled*usc_stride;k<limit*usc_stride+32;++k)
      assert(usc[k]==0xab);
   free(out);free(args);free(usc);
}
int main(void) {
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
