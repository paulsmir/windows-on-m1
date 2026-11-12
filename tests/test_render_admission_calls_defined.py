"""Every Admission* function the KMD sources call has a definition in the KMD tree.

Real bug (EXP2379 package build, 2026-11-12): HOLD-1 (arch/hold1) called
AdmissionScanoutMpoGateFree, which existed only on arch/cur1.  The host tests
stub the scan-out runtime, so they passed; MSVC /W4 /WX stopped the package with
C4013 after the ARM64 runtime build.  This check runs on the host in well under a
second and fails before a package build is started.
"""
import glob
import os
import re
import unittest

ROOT = os.path.join(os.path.dirname(os.path.abspath(__file__)), '..', 'drivers', 'apple-agx')
KMD = os.path.join(ROOT, 'render-admission')
SHARED = os.path.join(ROOT, 'shared')


def _read(paths):
    return ''.join(open(p, encoding='utf-8', errors='ignore').read() for p in sorted(set(paths)))


class RenderAdmissionCallsDefinedTests(unittest.TestCase):
    def test_every_called_admission_function_is_defined(self):
        definitions = _read(
            glob.glob(os.path.join(KMD, 'src', '*.c')) + glob.glob(os.path.join(KMD, 'src', '*.h')) +
            glob.glob(os.path.join(KMD, 'src', '*.inc')) + glob.glob(os.path.join(KMD, 'include', '*.h')) +
            glob.glob(os.path.join(SHARED, 'src', '*.c')) + glob.glob(os.path.join(SHARED, 'include', '*.h')))
        assembly = _read(glob.glob(os.path.join(KMD, 'src', '*.asm')))
        calls = set()
        for path in glob.glob(os.path.join(KMD, 'src', '*.c')):
            calls |= set(re.findall(r'\b(Admission[A-Z]\w*)\s*\(', open(path, encoding='utf-8', errors='ignore').read()))
        self.assertGreater(len(calls), 1000)
        missing = sorted(
            name for name in calls
            if not re.search(r'^[ \t]*(?:(?:__declspec\([^)]*\)|[A-Za-z_]\w*)[ \t\*]+)*' + name + r'\s*\([^;{}]*\)\s*\{',
                             definitions, re.MULTILINE)
            and not re.search(r'#define\s+' + name + r'\b', definitions)
            and not re.search(r'\b(?:LEAF_ENTRY|NESTED_ENTRY|EXPORT)\s+' + name + r'\b|^\s*' + name + r'\b',
                              assembly, re.MULTILINE))
        self.assertEqual(missing, [], 'called but never defined in the KMD tree (MSVC C4013 at package time)')


if __name__ == '__main__':
    unittest.main()
