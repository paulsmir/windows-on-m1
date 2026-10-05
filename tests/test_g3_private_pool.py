"""Private R64 budget: real allocator, ownership, stale tokens and pressure."""
import os
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
class PrivatePoolTests(unittest.TestCase):
    def test_quota_extent_ownership_and_generation(self):
        with tempfile.TemporaryDirectory() as tmp:
            exe = str(Path(tmp) / 'pool')
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                '-Wextra', '-Werror', '-fsanitize=address,undefined', '-I',
                str(ROOT / 'drivers/apple-agx/shared/include'),
                str(ROOT / 'tests/g3_private_pool_test.c'), '-o', exe], check=True)
            subprocess.run([exe], check=True)
