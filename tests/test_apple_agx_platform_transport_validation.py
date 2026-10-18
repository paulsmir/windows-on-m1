import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]
SHARED = ROOT / "drivers" / "apple-agx" / "shared"


class AppleAgxPlatformTransportValidationHostTest(unittest.TestCase):
    def test_exact_transport_memory_authorization(self):
        with tempfile.TemporaryDirectory() as directory:
            executable = pathlib.Path(directory) / "transport_validation_test"
            subprocess.run(
                [
                    "cc", "-std=c11", "-Wall", "-Wextra", "-Werror",
                    "-fsanitize=address,undefined", "-fno-omit-frame-pointer",
                    "-I", str(SHARED / "include"),
                    str(SHARED / "src" / "apple_agx_platform_transport_validation.c"),
                    str(SHARED / "tests" / "apple_agx_platform_transport_validation_test.c"),
                    "-o", str(executable),
                ],
                check=True,
                cwd=ROOT,
            )
            completed = subprocess.run(
                [str(executable)], check=True, capture_output=True, text=True,
                cwd=ROOT,
            )
            self.assertIn(
                "apple_agx_platform_transport_validation_test: ok",
                completed.stdout,
            )


if __name__ == "__main__":
    unittest.main()
