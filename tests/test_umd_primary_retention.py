"""PL-1: host-compiled state-machine test of the UMD's displayed-primary
retention (umd/include/umd_primary_retention.h), plus the wiring the
retention depends on in umd.c / umd_runtime_device.c."""
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
UMD = ROOT / "drivers/apple-agx/render-admission/umd"


class PrimaryRetentionTests(unittest.TestCase):
    def test_state_machine(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "primary_retention_test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-Wconversion", "-Wsign-conversion", "-pedantic",
                "-fsanitize=address,undefined",
                "-I", str(UMD / "include"),
                str(UMD / "tests/primary_retention_test.c"),
                "-o", str(binary),
            ], cwd=ROOT, check=True)
            run = subprocess.run([str(binary)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("primary_retention_test: OK", run.stdout)

    def test_header_is_neutral(self):
        text = (UMD / "include/umd_primary_retention.h").read_text()
        self.assertEqual(re.findall(r'^#include', text, re.M), [])

    def test_device_associated_primaries_are_released_by_handle_list(self):
        # EXP975: a retained allocation must never name the runtime resource.
        runtime = (UMD / "src/umd_runtime_device.c").read_text()
        self.assertIn("Retirement->DeviceAssociated", runtime)
        umd = (UMD / "src/umd.c").read_text()
        self.assertIn("AdmissionUmdRetentionDestroy(", umd)
        self.assertIn("AdmissionUmdRetentionSetDisplayMode(", umd)


if __name__ == "__main__":
    unittest.main()
