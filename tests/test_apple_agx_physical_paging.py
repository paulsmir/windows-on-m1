import pathlib
import subprocess
import tempfile
import unittest


ROOT = pathlib.Path(__file__).resolve().parents[1]


class AppleAgxPhysicalPagingTests(unittest.TestCase):
    def test_physical_paging_codec_and_executor(self):
        with tempfile.TemporaryDirectory() as directory:
            binary = pathlib.Path(directory) / "apple_agx_physical_paging_test"
            command = [
                "clang",
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-fsanitize=address,undefined",
                "-I",
                str(ROOT / "drivers/apple-agx/shared/include"),
                str(ROOT / "drivers/apple-agx/shared/src/apple_agx_physical_paging.c"),
                str(ROOT / "drivers/apple-agx/shared/tests/apple_agx_physical_paging_test.c"),
                "-o",
                str(binary),
            ]
            subprocess.run(command, check=True)
            subprocess.run([str(binary)], check=True)


if __name__ == "__main__":
    unittest.main()
