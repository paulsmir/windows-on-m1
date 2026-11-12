"""S2: host-compiled table test of the pure MPO rule table (mpo_contract.c).

Catches a two-plane configuration the display slots cannot show being
answered "supported", and the one-plane answers drifting from the display.c
one-plane handler.  Contract version 2 (S2 step 4) adds the scanout broker's
surface rules; tests/test_render_mpo_wiring.py checks the wiring.
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

    def test_table_is_wired_only_through_the_mpo_files(self):
        # S2 step 4: only the effective-only MPO path evaluates the table.
        src = DRIVER / "src"
        users = sorted(path.name for path in src.glob("*.c")
                       if "AdmissionMpoEvaluate" in
                       path.read_text(errors="replace"))
        self.assertEqual(users, ["mpo_contract.c", "mpo_windows.c"])

    def test_kernel_project_compiles_the_table(self):
        project = (DRIVER / "AppleAgxRenderAdmission.vcxproj").read_text()
        self.assertIn(r'<ClCompile Include="src\mpo_contract.c" />', project)


if __name__ == "__main__":
    unittest.main()
