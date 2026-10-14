"""Exercise the production AGX4 private-envelope parser on the host."""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class G4SubmitContractTests(unittest.TestCase):
    def test_compute_envelope_and_fail_closed_inputs(self):
        """G2: AppleAgxG4ParseComputeEx accepts only a well-formed compute
        envelope whose stream, sampler heap and preemption buffer are mapped."""
        with tempfile.TemporaryDirectory(prefix="agx4-compute-") as tmp:
            binary = Path(tmp) / "agx4-compute-test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", "-I",
                str(ROOT / "drivers/apple-agx/shared/include"),
                str(ROOT / "tests/apple_agx_g4_compute_submit_test.c"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_g4_submit.c"),
                "-o", str(binary),
            ], check=True)
            ran = subprocess.run([str(binary)], check=True, timeout=10,
                                 capture_output=True, text=True)
            self.assertIn("agx4 compute envelope: PASS", ran.stdout)

    def test_render_with_compute_envelope(self):
        """D3D-G1: AppleAgxG4ParseSubmitComputeEx admits a v5 render whose
        submission starts with one compute command the render waits on
        (Asahi: COMPUTE {0,0}, attachments, RENDER {NONE,1}) and refuses any
        other barrier, order or extra command; v3 keeps its entry point."""
        with tempfile.TemporaryDirectory(prefix="agx4-render-compute-") as tmp:
            binary = Path(tmp) / "agx4-render-compute-test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                "-Werror", "-fsanitize=address,undefined", "-I",
                str(ROOT / "drivers/apple-agx/shared/include"),
                str(ROOT / "tests/apple_agx_g4_render_compute_submit_test.c"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_g4_submit.c"),
                "-o", str(binary),
            ], check=True)
            ran = subprocess.run([str(binary)], check=True, timeout=10,
                                 capture_output=True, text=True)
            self.assertIn("agx4 render+compute envelope: PASS", ran.stdout)

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
