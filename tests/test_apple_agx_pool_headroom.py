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

        RC53: neutral memory must admit backed creation; LowMemory must stop
        growth. Loss of HighMemory must still return idle surplus now,
        preserving promised backing and every ownership/execution safety gate.
        After a broker Busy response, the periodic worker must eventually
        release idle backing even when no further paging record executes.
        The fixture executes the actual Windows wrappers and portable pools.
        """
        self.run_policy()

    def test_worker_cpp_with_c_allocator_linkage(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

        The extracted Windows worker and policy must compile as C++ and link
        to the C allocators; this catches /TP syntax and linkage regressions.
        Host compilation is not a native MSVC/WDK x86 build.
        """
        self.run_policy(cpp=True)

    def run_policy(self, cpp=False):
        source = (RENDER / 'src/pool_windows.c').read_text()
        names = ['AdmissionPoolMayGrow', 'AdmissionPoolShouldReclaim']
        if 'static VOID AdmissionPoolReclaimLocked(' in source:
            names.append('AdmissionPoolReclaimLocked')
        names += ['AdmissionMemoryPoolExecuted', 'AdmissionMemoryPoolReserve',
                  'AdmissionMemoryPoolUnreserve',
                  'AdmissionMemoryPoolReclaim', 'AdmissionPoolReclaimWork',
                  'AdmissionPoolZeroLeaseTimed', 'AdmissionMemoryPoolTvbTake',
                  'AdmissionMemoryPoolTvbGive']
        with tempfile.TemporaryDirectory(prefix='pool-headroom-') as tmp:
            tmp = Path(tmp)
            defines = re.findall(r'^#define ADMISSION_POOL_(?:SPARE_SLABS|CACHE_SLABS|'
                                 r'RELEASE_DELAY_MS|PREFILL_MIN_BYTES) .*$', source, re.M)
            (tmp / 'pool_policy.inc').write_text('\n'.join(defines) + '\n\n' +
                '\n\n'.join(function(source, n) for n in names))
            exe = tmp / 'headroom'
            flags = ['-Wall', '-Wextra', '-Werror', '-fsanitize=address,undefined',
                     '-I', str(tmp), '-I', str(RENDER / 'include')]
            objects = []
            for name in ('render_pool_demand', 'render_pool_slab'):
                obj = tmp / (name + '.o')
                subprocess.run([os.environ.get('CC', 'clang'), '-std=c11',
                                *flags, '-c', str(RENDER / 'src' / (name + '.c')),
                                '-o', str(obj)], check=True)
                objects.append(str(obj))
            source = RENDER / 'tests/pool_headroom_test.c'
            if cpp:
                cpp_source = tmp / 'policy.cpp'
                cpp_source.write_text(source.read_text())
                source = cpp_source
            subprocess.run([
                os.environ.get('CXX', 'clang++') if cpp else os.environ.get('CC', 'clang'),
                '-std=c++17' if cpp else '-std=c11', *flags,
                *(['-Wno-missing-field-initializers'] if cpp else []),
                str(source), *objects, '-o', str(exe),
            ], check=True)
            subprocess.run([str(exe)], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
