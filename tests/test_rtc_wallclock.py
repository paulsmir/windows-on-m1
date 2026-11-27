# SPDX-License-Identifier: MIT
"""Regression test for the Mu wall-clock conversion (tools/rtc_wallclock): Apple RTC counter + offset -> Unix time
-> EFI-style calendar time, leap years, 48-bit wrap, format span, generic-timer anchor. Compiles the C test with the
host compiler (CC, default clang) under UBSan and requires its PASS line."""
import os
import shutil
import subprocess
import tempfile
import unittest

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SRC = os.path.join(ROOT, "tools", "rtc_wallclock", "rtc_wallclock_test.c")


class RtcWallclockTest(unittest.TestCase):
    def test_conversion(self):
        cc = os.environ.get("CC", "clang")
        if not shutil.which(cc):
            self.skipTest(f"no host C compiler {cc!r}")
        with tempfile.TemporaryDirectory() as d:
            exe = os.path.join(d, "rtcwc")
            subprocess.run([cc, "-std=c11", "-Wall", "-Wextra", "-Werror", "-O1", "-fsanitize=undefined",
                            "-fno-sanitize-recover=all", "-o", exe, SRC], check=True)
            r = subprocess.run([exe], capture_output=True, text=True)
        self.assertEqual(r.returncode, 0, r.stdout + r.stderr)
        self.assertIn("RTC_WALLCLOCK_TEST PASS fails=0", r.stdout)


if __name__ == "__main__":
    unittest.main()
