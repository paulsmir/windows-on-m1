from pathlib import Path
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
WINDOWS = ROOT / "drivers" / "apple-input" / "windows"


class AppleInputHardwareHelpersTests(unittest.TestCase):
    def test_portable_register_helpers(self):
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "apple_input_hw_test"
            command = [
                "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-I", str(WINDOWS / "include"),
                str(WINDOWS / "tests" / "apple_input_hw_test.c"),
                str(WINDOWS / "src" / "apple_input_hw.c"),
                "-o", str(output),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            result = subprocess.run([str(output)], check=True, text=True,
                                    capture_output=True)
            self.assertIn("apple_input_hw_test: ok", result.stdout)


if __name__ == "__main__":
    unittest.main()
