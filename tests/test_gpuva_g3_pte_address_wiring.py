"""VidMm PTE page numbers must be converted at every G3 paging boundary."""

import unittest
from pathlib import Path


SOURCE = (Path(__file__).resolve().parents[1] /
          "drivers/apple-agx/render-admission/src/gpuva_g3_paging_windows.c")


class G3PteAddressWiringTests(unittest.TestCase):
    def test_parent_and_leaf_use_checked_byte_offsets(self):
        body = SOURCE.read_text()
        self.assertEqual(body.count(
            "AppleAgxGpuvaG3PteAddressBytes(pte->PageTableAddress,"), 2)
        self.assertEqual(body.count(
            "AppleAgxGpuvaG3PteAddressBytes(pte->PageAddress,"), 1)
        self.assertEqual(body.count(
            "address.GpuPhysical.SegmentId = (UINT)pte->Segment;"), 2)
        self.assertNotIn("SegmentOffset = pte->PageTableAddress;", body)
        self.assertNotIn("(pte->PageAddress & 0xffffu)", body)
        self.assertNotIn("pte->PageAddress > view.Bytes", body)


if __name__ == "__main__":
    unittest.main()
