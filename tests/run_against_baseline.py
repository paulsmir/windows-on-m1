#!/usr/bin/env python3
"""Run test modules and report only failures that are not in the baseline.

Usage: python3 run_against_baseline.py [module-or-glob ...]   (from tests/)
Default modules: every test_*.py except those named in EXCLUDED (with a reason).

tests/known_failures.txt lists one failing test id per line
(module.Class.method, or the module name for an import error), each with a
'#' reason. A known failure that now passes is reported as FIXED, so the list
can be shrunk. Exit status 1 only for NEW failures.
"""
from pathlib import Path
import fnmatch
import re
import subprocess
import sys

HERE = Path(__file__).resolve().parent
BASELINE = HERE / 'known_failures.txt'
# 2026-11-13: the pattern list matched only 430 of 854 modules; 424 (every
# test_rcNNN_*, 174 test_apple_*, test_agx_*, test_g3_*, the ANS suites, ...)
# never ran in the gate, and 87 of them had rotted (139 failing tests, listed in
# known_failures.txt as "rotted outside the gate").  The default is now every
# module; a module left out must be named in EXCLUDED with its reason.
DEFAULT = ['test_*']
EXCLUDED = {
    # 'test_module_name': 'reason it cannot run in the gate',
}


RUNNER = r"""
import importlib, sys, traceback, unittest
loader, suite = unittest.TestLoader(), unittest.TestSuite()
import_failed = False
for name in sys.argv[1:]:
    try:
        module = importlib.import_module(name)
    except Exception:
        print('ERROR: %s (unittest.loader._FailedTest.%s)' % (name, name), file=sys.stderr)
        traceback.print_exc()
        import_failed = True
        continue
    suite.addTests(loader.loadTestsFromModule(module))
result = unittest.TextTestRunner(stream=sys.stderr, verbosity=1).run(suite)
sys.exit(0 if result.wasSuccessful() and not import_failed else 1)
"""


def modules(patterns):
    names = sorted(p.stem for p in HERE.glob('test_*.py'))
    chosen = []
    for pattern in patterns:
        chosen += [n for n in names if fnmatch.fnmatch(n, pattern) and n not in chosen
                   and n not in EXCLUDED]
    return chosen


def known():
    if not BASELINE.exists():
        return {}
    entries = {}
    for line in BASELINE.read_text().splitlines():
        test, _, reason = line.partition('#')
        if test.strip():
            entries[test.strip()] = reason.strip()
    return entries


def main():
    chosen = modules(sys.argv[1:] or DEFAULT)
    # 2026-11-13: `python -m unittest a b` turns only an ImportError into a failed
    # test; any other exception at import aborted the whole run, no FAIL/ERROR
    # line and no "Ran" line were printed, and the gate still said BASELINE_OK.
    # Each module is now imported on its own: an import failure is reported as
    # ERROR for that module (known_failures.txt lists import errors by module
    # name) and the other modules still run.
    run = subprocess.run([sys.executable, '-c', RUNNER, *chosen], cwd=HERE, capture_output=True, text=True)
    failed = set()
    for kind, name, where in re.findall(r'^(FAIL|ERROR): (\S+) \(([^)]+)\)', run.stderr, re.M):
        if where.startswith('unittest.loader._FailedTest'):
            failed.add(name)
        else:
            failed.add(where)
    baseline = known()
    ran = set(chosen)
    new = sorted(f for f in failed if f not in baseline)
    fixed = sorted(k for k in baseline if k.split('.')[0] in ran and k not in failed)
    total = re.search(r'^Ran (\d+) tests?\b', run.stderr, re.M)
    print(f'ran {total.group(1) if total else "?"} tests in {len(chosen)} modules; '
          f'{len(failed)} failing, {len(failed) - len(new)} known, {len(new)} NEW, {len(fixed)} FIXED')
    for f in new:
        print('NEW', f)
    for f in fixed:
        print('FIXED', f, '#', baseline[f])
    if total is None or run.returncode not in (0, 1):
        # The runner died (crash, os._exit, exit at import outside the loader):
        # the counts above are not a result.
        print('GATE_INCOMPLETE returncode %d; stderr tail:' % run.returncode)
        print('\n'.join(run.stderr.splitlines()[-8:]))
        return 1
    print('BASELINE_OK' if not new else 'BASELINE_NEW_FAILURES')
    return 1 if new else 0


if __name__ == '__main__':
    sys.exit(main())
