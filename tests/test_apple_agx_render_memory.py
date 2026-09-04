from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers" / "apple-agx" / "shared"
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class AppleAgxRenderMemoryTests(unittest.TestCase):
    def test_portable_two_segment_memory_contract(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "render_memory_test"
            command = [
                os.environ.get("CC", "clang"),
                "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-fsanitize=address,undefined",
                "-I", str(RENDER / "include"),
                "-I", str(SHARED / "include"),
                str(RENDER / "tests" / "render_memory_test.c"),
                str(RENDER / "src" / "render_memory.c"),
                str(SHARED / "src" / "apple_agx_software_aperture.c"),
                str(SHARED / "src" / "apple_agx_local_segment.c"),
                str(SHARED / "src" / "apple_agx_physical_topology.c"),
                str(SHARED / "src" / "apple_agx_physical_paging.c"),
                str(SHARED / "src" / "apple_agx_aperture.c"),
                "-o", str(binary),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)

    def test_render_project_links_only_reviewed_memory_primitives(self):
        project = (RENDER / "AppleAgxRenderAdmission.vcxproj").read_text()
        for source in (
            r"src\render_memory.c",
            r"..\shared\src\apple_agx_software_aperture.c",
            r"..\shared\src\apple_agx_local_segment.c",
            r"..\shared\src\apple_agx_physical_topology.c",
            r"..\shared\src\apple_agx_physical_paging.c",
            r"..\shared\src\apple_agx_aperture.c",
        ):
            self.assertIn(source, project)

    def test_querysegment4_is_gated_on_complete_memory_contract(self):
        lifecycle = (RENDER / "src" / "lifecycle.c").read_text()
        windows = (RENDER / "src" / "memory_windows.c").read_text()
        header = (RENDER / "include" / "render_admission.h").read_text()
        self.assertIn("DXGKQAITYPE_QUERYSEGMENT4", lifecycle)
        self.assertIn("AdmissionDdiQuerySegment4", lifecycle)
        self.assertIn("AdmissionMemoryReady(&Context->Memory)", windows)
        self.assertIn("output->SegmentDescriptorStride", windows)
        self.assertIn("Flags.PopulatedFromSystemMemory", windows)
        self.assertIn("ADMISSION_MEMORY_CONTRACT Memory", header)


if __name__ == "__main__":
    unittest.main()
