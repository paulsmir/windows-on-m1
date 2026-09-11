from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"
UMD = RENDER / "umd"


class AppleAgxRenderUmdDirectFlipTests(unittest.TestCase):
    def test_portable_exact_resource_contract(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "direct_flip_contract_test"
            command = [
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(UMD / "include"),
                "-I", str(RENDER / "include"),
                str(UMD / "tests" / "direct_flip_contract_test.c"),
                str(UMD / "src" / "direct_flip_contract.c"),
                str(RENDER / "src" / "render_allocation.c"),
                "-o", str(binary),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)

    def test_umd_wires_the_exact_predicate_into_a_real_present_device(self):
        source = (UMD / "src" / "umd.c").read_text()
        project = (UMD / "AppleAgxRenderAdmissionUmd.vcxproj").read_text()
        open_start = source.index("OpenAdapter10_2(")
        open_adapter = source[
            open_start:source.index(
                "static SIZE_T APIENTRY AdmissionUmdCalcPrivateDeviceSize(",
                open_start,
            )
        ]

        self.assertIn(r"src\direct_flip_contract.c", project)
        self.assertIn(r"..\src\render_allocation.c", project)
        self.assertIn("pfnGetSupportedVersions", source)
        self.assertIn("pfnCreateDevice", source)
        self.assertIn("pfnCreateResource", source)
        self.assertIn("pfnOpenResource", source)
        self.assertIn("pfnCheckDirectFlipSupport", source)
        self.assertIn("AdmissionUmdDirectFlipCompatible(", source)
        self.assertIn("pfnPresentCb", source)
        self.assertIn("pfnPresent", source)
        self.assertIn("return S_OK", open_adapter)
        self.assertNotIn("return E_NOTIMPL", open_adapter)


if __name__ == "__main__":
    unittest.main()
