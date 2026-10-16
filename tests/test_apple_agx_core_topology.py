"""EXP1317: the SGX core topology the KMD reports to the UMD.

Invariant caught: Mesa sizes and fills its scratch block list per present
core (agx_scratch.c: params.num_cores_per_cluster, params.core_masks); the
Windows adapter reported no core, so a fragment shader using scratch
(dwmcore.dll_003d8800, a 96-entry indexable temporary) walked an empty list
and removed the device on both shader paths. The derivation follows Asahi
regs.rs get_gpu_id for the one-cluster G13 model; every other shape is
refused and reported as 0/0, and the UMD accepts only a mask that fits.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]


class CoreTopology(unittest.TestCase):
    def test_asahi_derivation(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = Path(tmp) / "topology"
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-fsanitize=address,undefined",
                            "-I", str(ROOT / "drivers/apple-agx/shared/include"),
                            str(ROOT / "tests/apple_agx_core_topology_test.c"), "-o", str(exe)], check=True)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_the_layers_carry_the_topology(self):
        kmd = (ROOT / "drivers/apple-agx/render-admission/src/backend_platform_windows.c").read_text()
        self.assertIn("AppleAgxG13CoreTopology(id[0], id[1], id[2], id[3], id[4],", kmd)
        lifecycle = (ROOT / "drivers/apple-agx/render-admission/src/lifecycle.c").read_text()
        self.assertIn("info->CoreMask = context->GpuCoreMask;", lifecycle)
        scene = (ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_asahi_scene.c").read_text()
        self.assertIn("params.num_cores_per_cluster=windows->Info.CoresPerCluster;", scene)
        self.assertIn("params.core_masks[0]=windows->Info.CoreMask;", scene)


if __name__ == "__main__":
    unittest.main()
