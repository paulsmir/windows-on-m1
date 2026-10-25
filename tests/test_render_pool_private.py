"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Private native slots must not consume VidMm's promised backing, escape their
cap, or be recycled by a copied lease, normal give, trim or premature drain.
Runs the production allocator; only Windows slab acquisition is substituted.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'

class PrivatePool(unittest.TestCase):
    def test_private_storage(self):
        with tempfile.TemporaryDirectory() as tmp:
            out = Path(tmp) / 'private'
            subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-I'+str(RENDER/'include'),
                str(RENDER/'tests/render_pool_private_test.c'),
                str(RENDER/'src/render_pool_slab.c'), '-o', str(out)], check=True)
            subprocess.run([str(out)], check=True, timeout=10)
