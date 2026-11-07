"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A failed paging wait after accepted MakeResident must retain the allocation
holds and stop the space. Releasing holds lets trim/reuse race unfinished
paging, and a later submission can skip the wait on Resident=true.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
W=ROOT/'drivers/apple-agx/mesa/winsys'
class PagingOwnership(unittest.TestCase):
    def test_failed_or_missing_fence_retains_accepted_holds(self):
        code=r'''
#define main existing_main
#include "agx_win32_gpuva_test.c"
#undef main
static unsigned missing;
static int pending(void *ctx,const uint64_t *h,unsigned n,uint64_t *f) {
 (void)ctx;(void)h;(void)n;*f=missing?0:5;return 2;
}
static int failed_wait(void *ctx,uint64_t f){(void)ctx;(void)f;return 0;}
int main(void){
 for(missing=0;missing<2;++missing){
  FIXTURE f={0};AGX_WIN32_GPUVA_SPACE space={0};AGX_WIN32_GPUVA_OPS cb=ops;
  cb.MakeResident=pending;cb.WaitPaging=failed_wait;
  assert(AgxWin32GpuvaInit(&space,&cb,&f));
  AGX_WIN32_GPUVA_BO command={17,0x20000,0x10000,1};
  const AGX_WIN32_GPUVA_BO *refs[]={&command};uint64_t done=0;
  assert(!AgxWin32GpuvaSubmit(&space,refs,1,&command,64,NULL,0,"draw",4,&done));
  assert(space.Terminal && space.Held && space.HeldCount==1);
  assert(f.evict==0 && f.submit==0 && !done);
  assert(!AgxWin32GpuvaUnbind(&space,&command));
  assert(!AgxWin32GpuvaSubmit(&space,refs,1,&command,64,NULL,0,"draw",4,&done));
  free(space.Held);
 }
}
'''
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'test.c').write_text(code)
            z=subprocess.run(['clang','-std=c11','-Wall','-Werror','-fsanitize=address,undefined',
                '-I'+str(W),'-I'+str(ROOT/'drivers/apple-agx/shared/include'),
                str(p/'test.c'),str(W/'agx_win32_gpuva.c'),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
            z=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
