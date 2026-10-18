import struct
import sys
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "m1n1_windows" / "proxyclient"))

from m1n1.tgtypes import BootArgs_r1, BootArgs_r2, BootArgs_r3


class BootArgsLayoutTests(unittest.TestCase):
    def test_cmdline_starts_at_c_union_alignment_offset(self):
        expected_offset = 112
        marker = b"m1n1.nodisplay\0"

        for revision, layout in ((1, BootArgs_r1), (2, BootArgs_r2), (3, BootArgs_r3)):
            with self.subTest(revision=revision):
                raw = bytearray(layout.sizeof())
                struct.pack_into("<HH", raw, 0, revision, revision)
                raw[expected_offset : expected_offset + len(marker)] = marker
                parsed = layout.parse(bytes(raw))
                self.assertEqual(parsed.cmdline, "m1n1.nodisplay")


if __name__ == "__main__":
    unittest.main()
