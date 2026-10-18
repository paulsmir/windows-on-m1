from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]

class FirmwarePrefixTests(unittest.TestCase):
    def test_wire_and_read_only_live_transport(self):
        with tempfile.TemporaryDirectory() as temp:
            binary = Path(temp) / 'prefix'
            subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-I', str(ROOT / 'm1n1_windows/src'),
                str(ROOT / 'tests/agx_firmware_prefix_test.c'),
                str(ROOT / 'm1n1_windows/src/hv_agx_firmware_prefix.c'),
                '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)
