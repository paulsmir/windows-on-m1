"""Execute the Windows pool admission/reclaim policy with host event inputs."""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'


def function(source, name):
    match = re.search(r'^(?:static | _Use_decl_annotations_ )?.*\b' + name +
                      r'\([^;]*?\)\s*\{', source, re.M)
    if not match:
        raise AssertionError(f'function missing: {name}')
    start = source.index('{', match.start())
    depth = 1
    end = start + 1
    while depth:
        depth += (source[end] == '{') - (source[end] == '}')
        end += 1
    return source[match.start():end]


class PoolHeadroomTest(unittest.TestCase):
    def test_windows_admission_and_pressure_reclaim(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        C73 admitted new locked slabs with high-memory clear and low-memory
        clear. Pressure must refuse that growth and return idle surplus now,
        preserving promised backing and every ownership/execution safety gate.
        The fixture executes the actual Windows wrappers and portable pools.
        """
        source = (RENDER / 'src/pool_windows.c').read_text()
        names = ['AdmissionPoolMayGrow']
        if 'static VOID AdmissionPoolReclaimLocked(' in source:
            names.append('AdmissionPoolReclaimLocked')
        names += ['AdmissionMemoryPoolExecuted', 'AdmissionMemoryPoolReserve',
                  'AdmissionMemoryPoolReclaim']
        with tempfile.TemporaryDirectory(prefix='pool-headroom-') as tmp:
            tmp = Path(tmp)
            (tmp / 'pool_policy.inc').write_text('\n\n'.join(function(source, n) for n in names))
            exe = tmp / 'headroom'
            subprocess.run([
                os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                '-Werror', '-fsanitize=address,undefined', '-I', str(tmp),
                '-I', str(RENDER / 'include'),
                str(RENDER / 'tests/pool_headroom_test.c'),
                str(RENDER / 'src/render_pool_demand.c'),
                str(RENDER / 'src/render_pool_slab.c'), '-o', str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
