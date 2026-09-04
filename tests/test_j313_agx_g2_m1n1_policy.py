from pathlib import Path
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
M1N1 = ROOT / "m1n1_windows"
SOURCE = M1N1 / "src" / "hv_agx_g2_policy.c"
HEADER = M1N1 / "src" / "hv_agx_g2_policy.h"
HARNESS = ROOT / "tests" / "hv_agx_g2_policy_test.c"
MAKEFILE = M1N1 / "Makefile"


class J313AgxG2M1n1PolicyTests(unittest.TestCase):
    def test_policy_matrix_under_sanitizers(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "hv_agx_g2_policy_test"
            command = [
                "/usr/bin/clang",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-DHV_AGX_G2_HOST_TEST",
                "-I",
                str(M1N1 / "src"),
                str(HARNESS),
                str(SOURCE),
                "-o",
                str(binary),
            ]
            subprocess.run(command, check=True, capture_output=True, text=True)
            subprocess.run([str(binary)], check=True, capture_output=True, text=True)

    def test_policy_unit_is_comparison_only(self):
        source = SOURCE.read_text()
        forbidden = (
            r"\bagx_start\b",
            r"\b(?:write32|writel|write64)\b",
            r"\bproxy\b",
            r"\bsubmit\b",
            r"\bcompletion\b",
            r"\bmalloc\b",
            r"\bcalloc\b",
        )
        for pattern in forbidden:
            self.assertIsNone(re.search(pattern, source, re.IGNORECASE), pattern)

    def test_policy_api_is_fail_closed_and_built_by_m1n1(self):
        header = HEADER.read_text()
        self.assertIn("bool hv_agx_g2_policy_validate", header)
        self.assertIn("const struct hv_agx_g2_policy *policy", header)
        self.assertIn("hv_agx_g2_policy.o", MAKEFILE.read_text())


if __name__ == "__main__":
    unittest.main()
