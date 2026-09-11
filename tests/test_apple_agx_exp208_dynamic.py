import subprocess
import tempfile
import unittest
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]


class AppleAgxExp208DynamicTest(unittest.TestCase):
    def test_exp208_dynamic_scalar_contract(self):
        with tempfile.TemporaryDirectory() as temporary:
            output = Path(temporary) / "apple_agx_exp208_dynamic_test"
            subprocess.run(
                [
                    "clang",
                    "-std=c11",
                    "-Wall",
                    "-Wextra",
                    "-Werror",
                    "-pedantic",
                    "-fsanitize=address,undefined",
                    "-fno-omit-frame-pointer",
                    "-Idrivers/apple-agx/shared/include",
                    "drivers/apple-agx/shared/src/apple_agx_exp208_dynamic.c",
                    "drivers/apple-agx/shared/tests/apple_agx_exp208_dynamic_test.c",
                    "-o",
                    str(output),
                ],
                cwd=ROOT,
                check=True,
            )
            subprocess.run([str(output)], cwd=ROOT, check=True)


if __name__ == "__main__":
    unittest.main()
