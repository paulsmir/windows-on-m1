from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from tests.test_apple_agx_render_correlation_decoder import load_decoder


ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / "drivers" / "apple-agx" / "render-admission"


class RenderCallCorrelationTests(unittest.TestCase):
    def test_two_slot_state_machine_and_persistence_states(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = Path(directory) / "render_call_correlation_test"
            subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall",
                "-Wextra", "-Werror", "-fsanitize=address,undefined",
                "-I", str(RENDER / "include"),
                str(RENDER / "tests" / "render_call_correlation_test.c"),
                str(RENDER / "src" / "render_call_correlation.c"),
                "-o", str(binary),
            ], check=True, cwd=ROOT)
            fixture = Path(directory) / "correlation.bin"
            subprocess.run([str(binary), str(fixture)], check=True, cwd=ROOT)
            state = load_decoder().decode_bytes(fixture.read_bytes())
            self.assertEqual(state["version"], 3)
            first, second = state["slots"]
            self.assertEqual(first["fence"], 256)
            self.assertEqual(second["fence"], 257)
            self.assertEqual(first["output"]["entry"]["processor"], 4)
            self.assertEqual(second["output"]["entry"]["processor"], 6)
            self.assertEqual(first["output"]["verified"]["status"], 0xc000003e)
            self.assertEqual(second["output"]["verified"]["valid"], 0)


if __name__ == "__main__":
    unittest.main()
