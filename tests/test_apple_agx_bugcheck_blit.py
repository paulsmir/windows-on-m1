"""A2: the pure bugcheck-time blit (DxgkDdiSystemDisplayWrite body)."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers/apple-agx/shared"


class BugcheckBlitTests(unittest.TestCase):
    def test_blit_clips_rejects_and_forces_alpha(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        An out-of-bounds write into reserved memory at HIGH_LEVEL during a
        bugcheck (a nested bugcheck and a lost dump): a rectangle not clipped at
        the right/bottom edge, X/Y >= the surface or X+W wrapping 32 bits,
        pitch padding overwritten, a source stride below width*4 accepted, or
        the alpha byte left 0.
        """
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "bugcheck_blit_test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-Wconversion", "-fsanitize=address,undefined",
                "-I", str(SHARED / "include"),
                str(SHARED / "tests" / "apple_agx_bugcheck_blit_test.c"),
                "-o", str(binary),
            ], check=True)
            run = subprocess.run([str(binary)], capture_output=True, text=True,
                                 timeout=10)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("apple_agx_bugcheck_blit_test: OK", run.stdout)

    def test_header_is_freestanding(self):
        header = (SHARED / "include/apple_agx_bugcheck_blit.h").read_text()
        for hosted in ("stdbool.h", "stddef.h", "stdint.h", "string.h",
                       "memcpy", "memset"):
            self.assertNotIn(hosted, header)
        # No kernel call: the DSB belongs in the Windows caller.
        self.assertNotRegex(header, r"\b(Rtl|Ke|Mm|Ex)[A-Z]\w*\s*\(")
        self.assertNotIn("__dsb", header)
        self.assertNotRegex(header, r"\bunion\b")  # MSVC C4201


if __name__ == "__main__":
    unittest.main()
