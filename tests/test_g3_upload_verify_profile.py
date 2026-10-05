"""Check the compiled BeginJob paths for the R162 upload diagnostic."""

import importlib.util
from pathlib import Path
import re
import subprocess
import unittest


ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / "drivers/apple-agx/render-admission/src/gpuva_g3_windows.c"
HEADER = ROOT / "drivers/apple-agx/render-admission/src/gpuva_g3_private.h"


class UploadVerificationProfileTests(unittest.TestCase):
    def test_production_omits_rehash_and_diagnostic_retains_it(self):
        spec = importlib.util.spec_from_file_location(
            "g3_vidmm_replay", ROOT / "tests/g3_vidmm_replay.py")
        replay = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(replay)
        body = replay.body(SOURCE.read_text(), "AdmissionGpuvaG3BeginJob")
        header = HEADER.read_text()
        self.assertRegex(header, r"(?m)^#define ADMISSION_G3_VERIFY_UPLOADS_ON_BEGIN_JOB 0$")
        for enabled in (0, 1):
            with self.subTest(diagnostic=enabled):
                unit = f"#define ADMISSION_G3_VERIFY_UPLOADS_ON_BEGIN_JOB {enabled}\n{body}\n"
                compiled = subprocess.run(
                    ["clang", "-E", "-P", "-x", "c", "-"],
                    input=unit, text=True, capture_output=True, check=True).stdout
                calls = re.findall(r"\bAdmissionG3VerifyUploads\s*\(", compiled)
                self.assertEqual(len(calls), enabled)


if __name__ == "__main__":
    unittest.main()
