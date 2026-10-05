"""GpuMmu page-table segment has a truthful CPU physical translation."""

import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers/apple-agx/render-admission"
SHARED = ROOT / "drivers/apple-agx/shared"


class G3CpuVisibleSegmentTests(unittest.TestCase):
    def test_portable_cpu_view_validator(self):
        sources = [
            RENDER / "tests/render_memory_test.c",
            RENDER / "src/render_memory.c",
            *(SHARED / "src" / name for name in (
                "apple_agx_software_aperture.c", "apple_agx_local_segment.c",
                "apple_agx_physical_topology.c", "apple_agx_physical_paging.c",
                "apple_agx_aperture.c",
            )),
        ]
        with tempfile.TemporaryDirectory() as temp:
            bitcode = []
            for index, source in enumerate(sources):
                output = Path(temp) / f"{index}.bc"
                subprocess.run([
                    os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                    "-Wextra", "-Werror", "-DAPPLE_AGX_GPUVA_G3_QUALIFICATION",
                    "-I", str(RENDER / "include"), "-I", str(SHARED / "include"),
                    "-emit-llvm", "-c", str(source), "-o", str(output),
                ], check=True, cwd=ROOT)
                bitcode.append(output)
            linked = Path(temp) / "render_memory_g3.bc"
            subprocess.run([
                os.environ.get("LLVM_LINK", "llvm-link"),
                *(str(path) for path in bitcode), "-o", str(linked),
            ], check=True, cwd=ROOT)
            subprocess.run([os.environ.get("LLI", "lli"), str(linked)],
                           check=True, cwd=ROOT)

    def test_both_segment_queries_publish_cpu_translation(self):
        source = (RENDER / "src/memory_windows.c").read_text()
        self.assertIn("AdmissionMemoryCpuVisibleLocalBase(", source)
        self.assertEqual(source.count("CpuTranslatedAddress.QuadPart ="), 2)
        self.assertIn("local.CpuVisible = APPLE_AGX_TRUE", source)


if __name__ == "__main__":
    unittest.main()
