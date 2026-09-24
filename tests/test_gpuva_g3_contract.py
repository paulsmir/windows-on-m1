"""Run the production GPUVA G3 address and leaf planner under sanitizers."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]


class G3ContractTests(unittest.TestCase):
    def test_real_c_vidmm_address_and_leaf_planner(self):
        with tempfile.TemporaryDirectory(prefix="gpuva-g3-") as tmp:
            binary = Path(tmp) / "g3-test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", "-I",
                str(ROOT / "drivers/apple-agx/shared/include"),
                str(ROOT / "tests/apple_agx_gpuva_g3_translation_test.c"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_gpuva_g3_translation.c"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
