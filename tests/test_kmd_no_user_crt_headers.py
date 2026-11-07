"""KMD sources must not include user-mode CRT headers.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? EXP1923: the T11b header
render_omitted_leaves.h included <stdint.h>, which pulls the user-mode
vcruntime.h into the WDK kernel build; C4083/C4005 became C2220 under /WX and
the package build failed, while every host test passed. Kernel headers use
plain C types (render_gpu_fault.h) or WDK types.
"""
import pathlib
import re
import unittest

ROOT = pathlib.Path(__file__).resolve().parents[1] / 'drivers' / 'apple-agx'
KMD_DIRS = [ROOT / 'render-admission' / 'src', ROOT / 'render-admission' / 'include',
            ROOT / 'shared' / 'include']
FORBIDDEN = re.compile(r'^\s*#\s*include\s*<(stdint|inttypes|stdio|stdlib)\.h>', re.M)


class KmdNoUserCrtHeaders(unittest.TestCase):
    def test_no_user_crt_includes(self):
        offenders = []
        for d in KMD_DIRS:
            for f in sorted(list(d.glob('*.h')) + list(d.glob('*.c'))):
                for m in FORBIDDEN.finditer(f.read_text(errors='replace')):
                    offenders.append(f'{f.relative_to(ROOT)}: {m.group(0).strip()}')
        self.assertEqual(offenders, [], 'user-mode CRT header in KMD sources')


if __name__ == '__main__':
    unittest.main()
