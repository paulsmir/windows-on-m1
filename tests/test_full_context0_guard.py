from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
SHARED=ROOT/'drivers/apple-agx/shared'
class FullContext0Guard(unittest.TestCase):
    def test_full_profile_cannot_replace_context0_but_context63_unchanged(self):
        with tempfile.TemporaryDirectory() as temp:
            exe=Path(temp)/'test'
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',
                '-DAPPLE_AGX_FULL_CONTEXT0_BROKER=1','-fsanitize=address,undefined',
                '-I',str(SHARED/'include'),str(ROOT/'tests/full_context0_guard_test.c'),
                str(SHARED/'src/apple_agx_uat.c'),str(SHARED/'src/apple_agx_uat_table.c'),
                str(SHARED/'src/apple_agx_uat_publication.c'),'-o',str(exe)],check=True)
            subprocess.run([str(exe)],check=True)
