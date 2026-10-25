"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

GPU-only General allocations currently have no overflow placement when segment 3
is full. The explicit boot opt-in must add aperture fallback only for that class,
keep the native pool preferred/backed, and leave CPU-visible/default policy intact.
"""
import unittest
from test_g4_gpu_only import run_c
from test_g4_segment_placement_replay import SHIM, allocation_body


class SysmemSpill(unittest.TestCase):
    def test_only_dword_one_enables_spill(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Malformed or absent
        device switches must not enable system-memory placement."""
        import test_g4_pool_residency_limit as policy
        policy.PoolResidencyLimit.check_registry_reader(
            self, 'AdmissionSysmemSpillEnabled', 'Wom1SysmemSpill')

    def test_policy_is_read_at_passive_start(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A live registry
        change must not alter placement midway through an adapter lifetime."""
        from test_apple_agx_pool_headroom import function
        from test_g4_pool_residency_limit import RENDER
        source = (RENDER / 'src/pool_windows.c').read_text()
        start = function(source, 'AdmissionMemoryPoolStart')
        self.assertLess(start.index('KeGetCurrentIrql() != PASSIVE_LEVEL'),
                        start.index('Context->SysmemSpill = AdmissionSysmemSpillEnabled(Context)'))
        self.assertLess(start.index('Context->SysmemSpill ='),
                        start.index('AdmissionPoolWindowOpen'))
        self.assertNotIn('AdmissionSysmemSpillEnabled', allocation_body())

    def test_opt_in_placement_matrix(self):
        shim = SHIM.replace('unsigned ClassId, CpuVisible;', 'unsigned ClassId, CpuVisible, Flags;')
        shim = shim.replace('*flags = 0;', '*flags = input->Flags;')
        shim += '\n#define AgxWin32BufferClassGeneral 1u\n'
        run_c(self, shim + allocation_body() + r'''
int main(void) {
  const uint64_t sizes[]={65536, 16842752, 135266304, 268435456};
  for(unsigned enabled=0;enabled<2;++enabled)
  for(unsigned pool=0;pool<2;++pool)
  for(unsigned cls=0;cls<4;++cls)
  for(unsigned cpu=0;cpu<2;++cpu)
  for(unsigned explicit_gpu=0;explicit_gpu<2;++explicit_gpu)
  for(unsigned n=0;n<sizeof(sizes)/sizeof(sizes[0]);++n) {
    if(explicit_gpu && (cpu || cls!=1)) continue;
    ADMISSION_CONTEXT ctx={1,0,enabled};
    INPUT in={cls,cpu,explicit_gpu?0x2c:0xf,sizes[n]};
    DXGK_ALLOCATIONINFO info={0};info.pPrivateDriverData=&in;info.PrivateDriverDataSize=sizeof(in);
    DXGKARG_CREATEALLOCATION args={1,&info,0,0,0};
    replay_pool_present=pool;replay_reserved=0;
    assert(AdmissionCreateAllocationImpl(&ctx,&args)==STATUS_SUCCESS);
    unsigned spill=enabled && pool && explicit_gpu;
    unsigned expected=cls==0?(cpu?1u:2u):(pool?(cpu?5u:4u):(cpu?3u:2u));
    if(spill) expected |= 1u;
    assert(info.SupportedReadSegmentSet==expected);
    assert(info.SupportedWriteSegmentSet==expected);
    assert(info.PreferredSegment.SegmentId0==(cls==0?(cpu?1u:2u):(pool?3u:2u)));
    assert(info.PreferredSegment.SegmentId1==((cls && pool && cpu)||spill?1u:0u));
    assert(info.FlagsWddm2.CpuVisible==cpu);
    assert(info.FlagsWddm2.AccessedPhysically==(cls==0));
    assert(replay_reserved==(cls && pool?sizes[n]:0));
    assert(!info.EvictionSegmentSet);
    free(info.hAllocation);
  }
  ADMISSION_CONTEXT ctx={1,0,1};INPUT in={1,0,0x2c,65536};
  DXGK_ALLOCATIONINFO info={0};info.pPrivateDriverData=&in;info.PrivateDriverDataSize=sizeof(in);
  DXGKARG_CREATEALLOCATION args={1,&info,0,0,0};
  replay_pool_present=1;replay_reserve_fail=1;
  assert(AdmissionCreateAllocationImpl(&ctx,&args)==STATUS_NO_MEMORY && !info.hAllocation);
}
''')
