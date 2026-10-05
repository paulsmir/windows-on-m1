from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
class RetainedBackingTests(unittest.TestCase):
    def test_reserved_aliases_denied(self):
        with tempfile.TemporaryDirectory() as temp:
            exe=Path(temp)/'test'
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-I',str(ROOT/'m1n1_windows/src'),
                str(ROOT/'tests/agx_retained_backing_test.c'),
                str(ROOT/'m1n1_windows/src/hv_agx_retained_backing.c'),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
