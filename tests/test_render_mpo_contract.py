"""S2: host-compiled table test of the pure MPO rule table (mpo_contract.c).

Catches a two-plane configuration the display slots cannot show being
answered "supported", and the one-plane answers drifting from the display.c
one-plane handler.  The table is not wired into the driver yet.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
DRIVER = ROOT / "drivers/apple-agx/render-admission"
SOURCE = DRIVER / "src/mpo_contract.c"
HEADER = DRIVER / "include/mpo_contract.h"


class RenderMpoContractTests(unittest.TestCase):
    def test_rule_table(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "mpo_contract_test"
            # -Wconversion/-Wsign-conversion stand in for MSVC C4242/C4244.
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-Wconversion", "-Wsign-conversion", "-pedantic",
                "-fsanitize=address,undefined",
                "-I", str(DRIVER / "include"),
                str(DRIVER / "tests/mpo_contract_test.c"),
                str(SOURCE),
                "-o", str(binary),
            ], cwd=ROOT, check=True)
            run = subprocess.run([str(binary)], cwd=ROOT, capture_output=True,
                                 text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
            self.assertIn("mpo_contract_test: OK", run.stdout)

    def test_pure_part_has_no_windows_dependency(self):
        for path in (SOURCE, HEADER):
            text = path.read_text()
            includes = re.findall(r'^#include\s+[<"]([^>"]+)', text, re.M)
            self.assertLessEqual(set(includes), {"mpo_contract.h"}, path)
            # MSVC C4201: no nameless unions/structs.
            self.assertNotRegex(text, r"\bunion\b", path)

    def test_table_is_not_wired_into_the_driver(self):
        src = DRIVER / "src"
        for path in sorted(src.glob("*.c")):
            if path.name == "mpo_contract.c":
                continue
            text = path.read_text(errors="replace")
            self.assertNotIn("AdmissionMpoEvaluate", text, path.name)
            self.assertNotIn("mpo_contract.h", text, path.name)

    def test_kernel_project_compiles_the_table(self):
        project = (DRIVER / "AppleAgxRenderAdmission.vcxproj").read_text()
        self.assertIn(r'<ClCompile Include="src\mpo_contract.c" />', project)


if __name__ == "__main__":
    unittest.main()
