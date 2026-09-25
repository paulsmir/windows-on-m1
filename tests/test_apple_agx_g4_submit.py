"""Exercise the production AGX4 private-envelope parser on the host."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class G4SubmitContractTests(unittest.TestCase):
    def test_native_render_envelope_and_fail_closed_inputs(self):
        with tempfile.TemporaryDirectory(prefix="agx4-submit-") as tmp:
            binary = Path(tmp) / "agx4-submit-test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", "-I",
                str(ROOT / "drivers/apple-agx/shared/include"),
                str(ROOT / "tests/apple_agx_g4_submit_test.c"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_g4_submit.c"),
                "-o", str(binary),
            ], check=True)
            subprocess.run([str(binary)], check=True, timeout=10)
