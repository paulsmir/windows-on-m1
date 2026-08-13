from pathlib import Path
import os
import subprocess
import tempfile
import unittest


ROOT = Path(__file__).resolve().parents[1]
PROTOCOL = ROOT / "drivers" / "apple-input" / "protocol"


class AppleSpiHidProtocolTests(unittest.TestCase):
    def test_portable_protocol_suite(self):
        sources = sorted((PROTOCOL / "src").glob("*.c"))
        self.assertTrue(sources, "portable Apple SPI HID sources are missing")
        with tempfile.TemporaryDirectory() as tmp:
            binary = Path(tmp) / "apple_spihid_test"
            command = [
                os.environ.get("CC", "clang"),
                "-std=c11",
                "-Wall",
                "-Wextra",
                "-Werror",
                "-I",
                str(PROTOCOL / "include"),
                str(PROTOCOL / "tests" / "apple_spihid_test.c"),
                *(str(path) for path in sources),
                "-o",
                str(binary),
            ]
            subprocess.run(command, check=True, cwd=ROOT)
            subprocess.run([str(binary)], check=True, cwd=ROOT)


if __name__ == "__main__":
    unittest.main()
