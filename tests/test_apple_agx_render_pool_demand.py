"""M2-f3: segment-3 granules commit on first reference and release only when
nothing can still reach them."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers/apple-agx/render-admission"


class RenderPoolDemandTest(unittest.TestCase):
    def test_granule_state_machine(self):
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "render_pool_demand_test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined",
                "-I", str(RENDER / "include"),
                str(RENDER / "tests" / "render_pool_demand_test.c"),
                str(RENDER / "src" / "render_pool_demand.c"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)

    def test_broker_capacity_matches_m1n1(self):
        """Every published segment-3 page is one m1n1 broker system record."""
        import re
        header = (RENDER / "include" / "render_pool_demand.h").read_text()
        broker = (ROOT / "m1n1_windows/src/hv_agx_gpuva_v5.h").read_text()
        kmd = int(re.search(r"ADMISSION_POOL_DEMAND_BROKER_BYTES (0x[0-9a-fA-F]+)ULL", header).group(1), 16)
        backings = int(re.search(r"HV_AGX_GPUVA_V5_BACKINGS (\d+)u", broker).group(1))
        page = int(re.search(r"HV_AGX_GPUVA_V5_PAGE UINT64_C\((0x[0-9a-fA-F]+)\)", broker).group(1), 16)
        self.assertEqual(kmd, backings * page)

    def test_render_project_compiles_the_pool(self):
        project = (RENDER / "AppleAgxRenderAdmission.vcxproj").read_text()
        self.assertIn(r"src\render_pool_demand.c", project)


if __name__ == "__main__":
    unittest.main()
