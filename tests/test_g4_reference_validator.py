"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
An address omitted from a native batch must be detected independently of its
residency list; wrong-owner/partial/overflow ranges and unwaited paging must
never pass the opt-in refusal gate. Execute the production walker and submit.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
W = ROOT / 'drivers/apple-agx/mesa/winsys'

class ReferenceValidator(unittest.TestCase):
    def test_ranges_and_submit_order(self):
        source = r'''
#include "agx_win32_gpuva.h"
#include <assert.h>
#include <string.h>
typedef struct { unsigned make,submit;int phase,bad_order;} FIXTURE;
static int reserve(void*c,uint64_t b,uint64_t l,uint64_t h,uint64_t*v){(void)c;(void)b;(void)h;*v=l;return 1;}
static int map(void*c,uint64_t a,uint64_t v,uint64_t p,unsigned x,uint64_t*f){(void)c;(void)a;(void)v;(void)p;(void)x;*f=0;return 1;}
static int release(void*c,uint64_t v,uint64_t b){(void)c;(void)v;(void)b;return 1;}
static int resident(void*c,const uint64_t*h,unsigned n,uint64_t*f){FIXTURE*q=c;(void)h;(void)n;q->make++;q->phase=1;*f=5;return 2;}
static int paging(void*c,uint64_t f){FIXTURE*q=c;assert(f==5 && q->phase==1);q->phase=2;return 1;}
static int submit(void*c,const uint64_t*w,unsigned n,uint64_t v,uint32_t b,const void*d,uint32_t z,uint64_t*f){FIXTURE*q=c;(void)w;(void)n;(void)v;(void)b;(void)d;(void)z;assert(q->phase==2);q->submit++;*f=q->submit;return 1;}
static int wait_render(void*c,uint64_t f){(void)c;(void)f;return 1;}
static int evict(void*c,const uint64_t*h,unsigned n){(void)c;(void)h;(void)n;return 1;}
static unsigned reports;
static AGX_WIN32_REF last;
static void report(void *ctx,const AGX_WIN32_REF *r,unsigned reason) {
 (void)ctx; assert(reason); ++reports; last=*r;
}
int main(void) {
 FIXTURE f; AGX_WIN32_GPUVA_SPACE s; AGX_WIN32_GPUVA_BO command={0}, data={0};
 memset(&f,0,sizeof(f)); AGX_WIN32_GPUVA_OPS ops={reserve,map,release,resident,paging,submit,wait_render,evict};
 assert(AgxWin32GpuvaInit(&s,&ops,&f));
 assert(AgxWin32GpuvaBind(&s,&command,11,65536,0,0));
 assert(AgxWin32GpuvaBind(&s,&data,12,65536,0,0));
 data.Va+=65536;
 const AGX_WIN32_GPUVA_BO *refs[]={&command,&data};
 AGX_WIN32_REF r={0};r.Va=data.Va+16;r.Bytes=32;r.Allocation=12;
 r.Kind=AgxRefDescriptor;r.Slot=71;r.Execute=9;r.Command=3;
 AGX_WIN32_REF_LIST list={0};
 assert(AgxWin32RefAppend(&list,&r));
 assert(!AgxWin32RefCheck(&r,refs,2));
 assert(AgxWin32RefCheck(&r,refs,1)==AgxRefMissing);
 r.Allocation=99;assert(AgxWin32RefCheck(&r,refs,2)==AgxRefOwner);
 r.Allocation=12;r.Bytes=data.Bytes;assert(AgxWin32RefCheck(&r,refs,2)==AgxRefRange);
 r.Va=UINT64_MAX-2;r.Bytes=8;assert(AgxWin32RefCheck(&r,refs,2)==AgxRefRange);
 r.Va=data.Va;r.Bytes=0;assert(AgxWin32RefCheck(&r,refs,2)==AgxRefRange);
 uint64_t done=0;unsigned private_data=0;
 s.Validation=&list;s.RefReport=report;s.RefRefuse=1;
 assert(!AgxWin32GpuvaSubmit(&s,refs,1,&command,64,NULL,0,&private_data,4,&done));
 assert(reports==1 && last.Slot==71 && last.Execute==9 && f.make==0 && f.submit==0);
 /* Repeated refusal reports the first violation only. */
 assert(!AgxWin32GpuvaSubmit(&s,refs,1,&command,64,NULL,0,&private_data,4,&done));
 assert(reports==1);
 s.RefRefuse=0;
 assert(AgxWin32GpuvaSubmit(&s,refs,1,&command,64,NULL,0,&private_data,4,&done));
 assert(f.submit==1);assert(AgxWin32GpuvaRetire(&s,done));
 s.RefRefuse=1;
 assert(AgxWin32GpuvaSubmit(&s,refs,2,&command,64,NULL,0,&private_data,4,&done));
 assert(!f.bad_order);assert(AgxWin32GpuvaRetire(&s,done));
 list.Incomplete=1;
 assert(!AgxWin32GpuvaSubmit(&s,refs,2,&command,64,NULL,0,&private_data,4,&done));
 s.Validation=NULL; /* default off ignores diagnostics entirely */
 assert(AgxWin32GpuvaSubmit(&s,refs,2,&command,64,NULL,0,&private_data,4,&done));
 assert(AgxWin32GpuvaRetire(&s,done));
 AgxWin32RefCleanup(&list);assert(!list.Count && !list.Items);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as t:
            p=Path(t);(p/'test.c').write_text(source)
            cmd=['clang','-std=c11','-Wall','-Werror','-fsanitize=address,undefined',
                 '-I'+str(W),'-I'+str(ROOT/'drivers/apple-agx/shared/include'),
                 str(p/'test.c'),str(W/'agx_win32_gpuva.c'),'-o',str(p/'test')]
            z=subprocess.run(cmd,capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
            z=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)

    def test_native_collectors_are_independent_and_gated(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Both native submits must install the independent ledger; collection
        must cover bindings even if bo_list registration disappears.
        """
        s=(W/'agx_win32_gpuva_batch.c').read_text()
        self.assertEqual(s.count('b->Gpuva.Validation=b->ValidateRefs ? &g->Expected : NULL;'),2)
        self.assertIn('AgxWin32RefCleanup(&g->Expected)',s)
        self.assertIn('capture_stage_refs(batch',s)
        self.assertIn('info->index.resource',s)
        self.assertIn('indirect->indirect_draw_count',s)
        self.assertIn('AgxRefScratch',s)
        self.assertIn('batch->sampler_heap.bo',s)
