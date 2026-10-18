"""Real KMD initialization primitives: zeroing, list protocol and rollback."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class PrivateStorageTests(unittest.TestCase):
    def test_initialization_and_construction_rollback(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(Path(tmp) / 'storage')
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                '-Wextra', '-Werror', '-fsanitize=address,undefined', '-I',
                str(ROOT / 'drivers/apple-agx/shared/include'),
                str(ROOT / 'tests/g3_private_storage_test.c'), '-o', exe], check=True)
            subprocess.run([exe], check=True)
