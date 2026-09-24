"""The first rejected parent PTE must survive a VidMm bugcheck."""

import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "drivers/apple-agx/render-admission/src"


class LevelOneFailureReceiptTests(unittest.TestCase):
    def test_parent_failures_keep_the_offending_entry_and_branch(self):
        source = (SRC / "gpuva_g3_paging_windows.c").read_text()
        self.assertIn("AdmissionG3PagingFailureChildAddress", source)
        self.assertIn("AdmissionG3PagingFailureChildGraph", source)
        self.assertIn("AdmissionG3PagingFailureParentLink", source)
        self.assertIn("AdmissionG3PagingFailureTableAddress", source)
        self.assertIn("AdmissionG3PagingFailureTableGraph", source)
        self.assertIn("AdmissionRecordGpuvaG3PagingFailure(adapter, &failure)", source)

    def test_receipt_captures_pte_and_flushes_after_graph_lock(self):
        paging = (SRC / "gpuva_g3_paging_windows.c").read_text()
        receipts = (SRC / "receipts.c").read_text()
        self.assertIn("pte->PageTablePageSize", paging)
        self.assertIn("pte->PageTableAddress", paging)
        self.assertIn("ExReleaseFastMutex(&state->Lock);\n  AdmissionRecordGpuvaG3PagingFailure", paging)
        self.assertIn('L"Wom1G3PagingFailure"', receipts)
        self.assertIn("ZwFlushKey(key)", receipts.split("void AdmissionRecordGpuvaG3PagingFailure(", 1)[1])


if __name__ == "__main__":
    unittest.main()
