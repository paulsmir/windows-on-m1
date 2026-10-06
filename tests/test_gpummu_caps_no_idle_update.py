"""EXP986: page-table updates must not require an idle address space.

DXGK_GPUMMUCAPS.PageTableUpdateRequireAddressSpaceIdle=1 made VidMm idle the
whole process before every page-table update (EXP983R ETW: 7707
DXGK_PERFORMANCE_PROCESS_IDLE_TO_FLUSH_TLB warnings in 90 s, 10-s paging-fence
stalls) and left freshly mapped / made-resident allocations without PTEs until
the process was next scheduled (EXP985: Map + MakeResident fence completed, no
UpdatePageTable or FILL reached the KMD, CPU copy QUERY predicate57). The KMD
already serialises leaf updates and TLB flushes with a process's in-flight job
(R155 UpdatePageTable wait, R165 FlushTlb wait), so the cap must stay clear.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/src'


class GpuMmuCaps(unittest.TestCase):
    def test_page_table_updates_do_not_require_idle_address_space(self):
        text = (SRC / 'lifecycle.c').read_text()
        caps = text[text.index('case DXGKQAITYPE_GPUMMUCAPS:'):text.index('case DXGKQAITYPE_PAGETABLELEVELDESC:')]
        self.assertNotRegex(caps, r'PageTableUpdateRequireAddressSpaceIdle\s*=\s*1')
        self.assertIn('RtlZeroMemory(caps, sizeof(*caps));', caps)

    def test_kmd_serialises_updates_with_in_flight_jobs(self):
        paging = (SRC / 'gpuva_g3_paging_windows.c').read_text()
        # R155 (UpdatePageTable) and R165 (FlushTlb) both wait for JobInFlight.
        self.assertGreaterEqual(len(re.findall(
            r'!process->Graph\.JobInFlight && !process->Graph\.LeaseToken', paging)), 2)


if __name__ == '__main__':
    unittest.main()
