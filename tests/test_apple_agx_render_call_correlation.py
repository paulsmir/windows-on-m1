from pathlib import Path
import os
import subprocess
import tempfile
import unittest


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
            subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
