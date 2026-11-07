"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The Windows pool stop wrapper must retain its containing runtime when private
leases or unrevoked window aliases retain slabs. SlabDrain alone cannot prevent
the wrapper freeing that owner. Execute real stop/free and slab functions;
substitute only OS allocation, worker draining and broker acknowledgement.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_apple_agx_pool_headroom import function

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'


class PoolStopLifetime(unittest.TestCase):
    def test_retains_owner_until_all_lifetimes_retire(self):
        source = (RENDER / 'src/pool_windows.c').read_text()
        funcs = '\n'.join(function(source, name) for name in (
            'AdmissionPoolDemandFree', 'AdmissionPoolDemandStop'))
        with tempfile.TemporaryDirectory(prefix='pool-stop-') as tmp:
            tmp = Path(tmp)
            (tmp / 'stop.inc').write_text(funcs)
            exe = tmp / 'stop'
            build = subprocess.run([
                'clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                '-fsanitize=address,undefined', '-I', str(tmp),
                '-I', str(RENDER / 'include'),
                str(ROOT / 'tests/g4_pool_stop_lifetime.c'),
                str(RENDER / 'src/render_pool_slab.c'), '-o', str(exe),
            ], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stdout + build.stderr)
            for scenario in ('private', 'refused', 'absent', 'worker',
                             'drained_worker', 'normal', 'empty'):
                with self.subTest(scenario=scenario):
                    run = subprocess.run([str(exe), scenario],
                        capture_output=True, text=True, timeout=15)
                    self.assertEqual(run.returncode, 0, run.stdout + run.stderr)
