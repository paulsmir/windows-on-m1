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
        self.assertLess(
            paging.rindex("ExReleaseFastMutex(&state->Lock);"),
            paging.rindex("AdmissionRecordGpuvaG3PagingFailure(adapter, &failure)"))
        self.assertIn('L"Wom1G3PagingFailure"', receipts)
        # EXP1073: staged in the paging DDI, written and flushed by the worker.
        worker = receipts.split("static VOID AdmissionPagingReceiptWorker(", 1)[1].split("\n}\n", 1)[0]
        self.assertIn("L\"Wom1G3PagingFailure\", &failure,\n                                  sizeof(failure), TRUE)", worker)


if __name__ == "__main__":
    unittest.main()
