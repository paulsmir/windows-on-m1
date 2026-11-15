"""PD-1c (KMD-D2) T3/T4: post-display surface validation and release info."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers/apple-agx/shared"


class PostDisplayReleaseTests(unittest.TestCase):
    def test_surface_and_release_info(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        A non-J313 panel rejected by a literal compare; TargetId 0xFFFFFFFF or a
        mismatched AcpiId returned from the release DDI; NOT_SUPPORTED used for
        a missing surface instead of the documented no-display case.
        """
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "post_display_release_test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined",
                "-I", str(SHARED / "include"),
                str(SHARED / "tests" / "apple_agx_post_display_release_test.c"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)


if __name__ == "__main__":
    unittest.main()
