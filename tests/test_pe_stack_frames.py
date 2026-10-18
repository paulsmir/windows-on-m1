"""EXP1230: the large-frame gate for driver images (tools/pe_stack_frames.py).

dwm.exe overflowed a worker thread's stack inside the AGX UMD at __chkstk:
a driver image must not reserve frames that a small-stack caller cannot
hold. The scanner recognises the compilers' fixed prologues for frames past
one page; these cases pin both encodings and the boundaries.
"""
from pathlib import Path
import struct
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "tools"))
import pe_stack_frames  # noqa: E402


def words(*values):
    return b"".join(struct.pack("<I", value) for value in values)


MOVZ_X15 = 0xD280000F
BL = 0x94000000
NOP = 0xD503201F
SUB_SP = pe_stack_frames.SUB_SP_X15_UXTX4


class StackFrameScanTest(unittest.TestCase):
    def test_arm64_prologue(self):
        frame = 89248 // 16
        code = words(NOP, MOVZ_X15 | (frame << 5), BL | 0x10, SUB_SP, NOP,
                     MOVZ_X15 | (0x100 << 5), BL | 0x20, NOP, SUB_SP,
                     MOVZ_X15 | (0x2 << 5) | (1 << 21), BL, SUB_SP)
        self.assertEqual(pe_stack_frames.scan_arm64(code, 0x1000),
                         [(0x1004, 89248), (0x1014, 4096), (0x1024, 0x20000 * 16)])

    def test_arm64_needs_the_stack_adjustment(self):
        # A movz to x15 that is not followed by the sub sp is not a frame.
        code = words(MOVZ_X15 | (0x400 << 5), BL, NOP, NOP, NOP, NOP, SUB_SP)
        self.assertEqual(pe_stack_frames.scan_arm64(code, 0), [])
        # x16 is not the __chkstk operand.
        self.assertEqual(pe_stack_frames.scan_arm64(words(0xD2800010 | (0x400 << 5), BL, SUB_SP), 0), [])

    def test_x86_prologue_uses_the_common_target(self):
        def call(at, value, target):
            return b"\xb8" + struct.pack("<I", value) + b"\xe8" + struct.pack("<i", target - (at + 10))
        code = bytearray(b"\x90" * 64)
        code[0:10] = call(0, 0x15ca0, 0x30)
        code[16:26] = call(16, 0x2000, 0x30)
        code[32:42] = call(32, 0x8000, 0x38)     # a different callee: not __chkstk
        code[48:58] = call(48, 0x100, 0x30)      # below one page: ignored
        self.assertEqual(pe_stack_frames.scan_x86(bytes(code), 0x400),
                         [(0x400, 0x15ca0), (0x410, 0x2000)])


if __name__ == "__main__":
    unittest.main()
