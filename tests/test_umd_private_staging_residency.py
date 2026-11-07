"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

T13 exposes process-owned CPU storage, but the old trim policy pins its idle
canonical allocation as though the pointer were a VidMm mapping. Execute the
production trim/retry/submission transaction: only these independent CPU maps
can survive canonical eviction, with ownership holds and paging fences intact.
"""
import unittest
import test_umd_persistent_residency as residency


REPLAY = r'''
static ADMISSION_UMD_DEVICE *device;
static unsigned char cpu[64];
static void concurrent_submit() {
  auto &s=device->ScreenBuffers[0];
  assert(!device->ScreenBufferLock && s.Transition && s.Mapped);
  uint64_t token=10, fence=0;
  assert(!make_resident(device,&token,1,&fence));
  assert(!s.CopyHeld && !s.SubmissionHolds);
  UINT trimmed=99;
  assert(!trim_residency(device,1,&trimmed) && !trimmed);
  // Application stores through its existing pointer while VidMm evicts.
  s.PrivateStaging[17]=0x71;
}
int main() {
  CB cb={mk,ev}; ADMISSION_UMD_DEVICE d={}; device=&d;
  d.Magic=ADMISSION_UMD_DEVICE_MAGIC; d.PagingQueue=(HANDLE)1; d.KernelCallbacks=&cb;
  auto &s=d.ScreenBuffers[0];
  s.Active=TRUE; s.Resident=TRUE; s.Token=10; s.KernelAllocation=0x100;
  s.CanonicalGpuVa=0x10000; s.Bytes=32u<<20;
  s.HeapStaging=TRUE; s.PrivateStaging=cpu; s.Mapped=TRUE;
  s.NativeBo=&s; s.NativeMapRelease=&s;
  cpu[1]=0x37;
  UINT count=0; uint64_t token=10, fence=0;
  // No real canonical user may be removed, even with a private CPU map.
  int *blocked[]={&s.SystemDirect,&s.Direct,&s.Borrowed,&s.KmtResidencyHeld,
                 &s.Transition,&s.CopyHeld};
  for(auto p:blocked) {
    *p=TRUE; assert(!trim_residency(&d,1,&count) && !count); *p=FALSE;
  }
  s.SubmissionHolds=1; assert(!trim_residency(&d,1,&count)); s.SubmissionHolds=0;
  s.SourceHolds=1; assert(!trim_residency(&d,1,&count)); s.SourceHolds=0;
  s.StagingAllocation=0x200; assert(!trim_residency(&d,1,&count)); s.StagingAllocation=0;
  s.HeapStaging=FALSE; assert(!trim_residency(&d,1,&count)); s.HeapStaging=TRUE;
  s.PrivateStaging=nullptr; assert(!trim_residency(&d,1,&count)); s.PrivateStaging=cpu;
  assert(!evicted);
  // Failed callback retains the reference, pointer and map, with no stuck hold.
  before_evict=concurrent_submit; evict_result=E_FAIL;
  assert(!trim_residency(&d,1,&count) && !count);
  assert(evicted==1 && s.Resident && s.Mapped && !s.Transition);
  assert(s.PrivateStaging==cpu && cpu[1]==0x37 && cpu[17]==0x71);
  // A successful callback releases only the canonical residency reference.
  evict_result=S_OK;
  assert(trim_residency(&d,1,&count)==(32ull<<20) && count==1);
  assert(evicted==2 && !s.Resident && s.Mapped && !s.Transition);
  assert(s.KernelAllocation==0x100 && s.CanonicalGpuVa==0x10000 && s.PrivateStaging==cpu);
  cpu[2]=0x38;
  // Submission restores exactly that canonical handle before copies/use.
  before_evict=nullptr;
  assert(make_resident(&d,&token,1,&fence)==2 && fence==7 && made==1);
  assert(s.Resident && s.CopyHeld && s.SubmissionHolds==1 && s.Mapped);
  assert(!trim_residency(&d,1,&count) && !count);
  assert(cpu[1]==0x37 && cpu[2]==0x38 && cpu[17]==0x71);
  assert(evict(&d,&token,1) && s.Resident && s.Mapped);
  // Budget refusal for another BO can now reclaim this idle private-map BO.
  auto &other=d.ScreenBuffers[1];
  other.Active=TRUE;other.Token=11;other.KernelAllocation=0x101;
  other.StagingAllocation=0x201;other.CanonicalGpuVa=0x20000;other.Bytes=16u<<20;
  token=11;refusals=1;made=evicted=0;
  assert(make_resident(&d,&token,1,&fence)==2 && fence==7 && made==2 && evicted==1);
  assert(last_evicted_count==1 && last_evicted[0]==0x100);
  assert(!s.Resident && s.Mapped && s.PrivateStaging==cpu && cpu[2]==0x38);
  assert(other.Resident && other.CopyHeld && other.SubmissionHolds==1);
  assert(evict(&d,&token,1));
  puts("private staging residency: PASS");
}
'''


class PrivateStagingResidency(unittest.TestCase):
    def test_idle_private_cpu_map_survives_trim_and_restore(self):
        residency.PersistentResidency.run_replay(
            self, residency.BODY[:residency.BODY.index('int main(){')] + REPLAY,
            'private staging residency: PASS')


if __name__ == '__main__':
    unittest.main()
