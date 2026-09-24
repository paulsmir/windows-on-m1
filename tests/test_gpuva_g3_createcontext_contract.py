"""WDDM 3.2 GPUVA paging SystemContext accepts its VA flag."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers/apple-agx/render-admission"


class G3CreateContextContractTests(unittest.TestCase):
    def test_gpuva_system_context_object_contract(self):
        with tempfile.TemporaryDirectory() as temp:
            bitcode = []
            for source in ("tests/render_objects_test.c", "src/objects.c"):
                output = Path(temp) / (Path(source).stem + ".bc")
                subprocess.run([
                    os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                    "-Wextra", "-Werror", "-DAPPLE_AGX_GPUVA_G3_QUALIFICATION",
                    "-I", str(RENDER / "include"), "-emit-llvm", "-c",
                    str(RENDER / source), "-o", str(output),
                ], check=True, cwd=ROOT)
                bitcode.append(output)
            linked = Path(temp) / "render_objects_g3.bc"
            subprocess.run([
                os.environ.get("LLVM_LINK", "llvm-link"),
                *(str(path) for path in bitcode), "-o", str(linked),
            ], check=True, cwd=ROOT)
            subprocess.run([
                os.environ.get("LLI", "lli"), str(linked),
            ], check=True, cwd=ROOT)

    def test_callback_records_os_context_flags(self):
        callback = (RENDER / "src/callbacks.c").read_text().split(
            "NTSTATUS AdmissionDdiCreateContext(", 1)[1].split(
            "NTSTATUS AdmissionDdiDestroyContext(", 1)[0]
        self.assertIn("AdmissionRecordGpuvaG3ContextInput(", callback)
        self.assertIn("ADMISSION_CONTEXT_VALID_FLAGS", callback)


if __name__ == "__main__":
    unittest.main()
