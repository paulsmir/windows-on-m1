import pathlib
import subprocess
import tempfile
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers" / "apple-agx" / "shared"

class AppleAgxG13QueueProviderHostTest(unittest.TestCase):
    def test_provider_contract(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = pathlib.Path(directory) / "provider_test"
            subprocess.run([
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(SHARED / "include"),
                str(SHARED / "src" / "apple_agx_g13_codec.c"),
                str(SHARED / "src" / "apple_agx_g13_queue_runtime.c"),
                str(SHARED / "src" / "apple_agx_g13_queue_provider.c"),
                str(SHARED / "tests" / "apple_agx_g13_queue_provider_test.c"),
                "-o", str(executable),
            ], check=True, cwd=ROOT)
            completed = subprocess.run([str(executable)], check=True,
                                       capture_output=True, text=True)
            self.assertIn("apple_agx_g13_queue_provider_test: ok", completed.stdout)

if __name__ == "__main__":
    unittest.main()

