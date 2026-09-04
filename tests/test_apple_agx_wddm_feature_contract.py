from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers" / "apple-agx" / "shared"
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxWddmFeatureContractTests(unittest.TestCase):
    def test_portable_atomic_feature_contract(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "apple_agx_wddm_feature_contract_test"
            command = [
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(SHARED / "include"),
                str(SHARED / "tests" / "apple_agx_wddm_feature_contract_test.c"),
                str(SHARED / "src" / "apple_agx_wddm_feature_contract.c"),
                "-o", str(binary),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)

    def test_render_project_links_the_contract(self):
        project = (RENDER / "AppleAgxRenderAdmission.vcxproj").read_text()
        self.assertIn(r"..\shared\include", project)
        self.assertIn(
            r"..\shared\src\apple_agx_wddm_feature_contract.c", project
        )
        self.assertIn(
            r"..\shared\include\apple_agx_wddm_feature_contract.h", project
        )


if __name__ == "__main__":
    unittest.main()
