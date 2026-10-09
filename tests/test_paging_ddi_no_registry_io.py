"""EXP1073: DxgkDdiBuildPagingBuffer must not reach registry I/O.

EXP996 (0x119 dump) and EXP1073 (live dump, GPU stalled without a TDR) both
found a VidMm worker blocked in AdmissionDdiBuildPagingBuffer -> NtFlushKey.
In EXP1073 the paging DDI ran inside MmRotatePhysicalView (eviction), and
AdmissionRecordGpuvaG3UnpublishedGroups opened and flushed the device key
there. Invariant: every receipt function the G3 paging DDI calls only stages
its data; registry access lives in the work item (AdmissionPagingReceiptWorker),
which the DDI never calls directly.
"""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "drivers/apple-agx/render-admission/src"
REGISTRY = ("IoOpenDeviceRegistryKey", "ZwSetValueKey", "ZwFlushKey", "WriteBinary(",
            "WriteDword(", "WriteQword(", "AdmissionPagingReceiptWrite(")


def function(text, name):
    m = re.search(r"(?m)^(?:_Use_decl_annotations_\s+)?(?:static\s+)?[A-Za-z_][A-Za-z0-9_ *]*\b"
                  + re.escape(name) + r"\s*\([^;{]*\)\s*\{", text)
    if not m:
        return None
    start = text.index("{", m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == "{") - (text[end] == "}"); end += 1
    return text[m.start():end]


class PagingDdiNoRegistryIo(unittest.TestCase):
    def test_receipts_called_by_the_paging_ddi_only_stage(self):
        paging = (SRC / "gpuva_g3_paging_windows.c").read_text()
        receipts = (SRC / "receipts.c").read_text()
        ddi = function(paging, "AdmissionGpuvaG3BuildPagingBuffer")
        self.assertIsNotNone(ddi)
        called = sorted(set(re.findall(r"\b(AdmissionRecord[A-Za-z0-9_]*)\s*\(", ddi)))
        self.assertTrue(called, "no receipt calls found")
        for name in called:
            body = function(receipts, name)
            self.assertIsNotNone(body, name)
            for token in REGISTRY:
                self.assertNotIn(token, body, f"{name} performs registry I/O ({token})")
            self.assertIn("AdmissionPagingReceiptStage(", body, name)

    def test_worker_is_the_only_writer_and_is_queued_not_called(self):
        receipts = (SRC / "receipts.c").read_text()
        stage = function(receipts, "AdmissionPagingReceiptStage")
        self.assertIn("IoQueueWorkItem(", stage)
        for token in REGISTRY:
            self.assertNotIn(token, stage)
        worker = function(receipts, "AdmissionPagingReceiptWorker")
        self.assertIn("AdmissionPagingReceiptWrite(", worker)
        # The idle transition is made under the lock that Stage sets Dirty under.
        self.assertLess(worker.index("KeAcquireSpinLock(&receipts->Lock"),
                        worker.index("InterlockedExchange(&receipts->Queued, 0)"))


if __name__ == "__main__":
    unittest.main()
