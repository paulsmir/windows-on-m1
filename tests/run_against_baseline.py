#!/usr/bin/env python3
"""Run test modules and report only failures that are not in the baseline.

Usage: python3 run_against_baseline.py [module-or-glob ...]   (from tests/)
Default modules: test_d3d12_* test_umd* test_g4* test_dxil_to_nir test_dxbc_to_nir.

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
DEFAULT = ['test_d3d12_*', 'test_umd*', 'test_g4*', 'test_dxil_to_nir', 'test_dxbc_to_nir']


def modules(patterns):
    names = sorted(p.stem for p in HERE.glob('test_*.py'))
    chosen = []
    for pattern in patterns:
        chosen += [n for n in names if fnmatch.fnmatch(n, pattern) and n not in chosen]
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
    run = subprocess.run([sys.executable, '-m', 'unittest', *chosen], cwd=HERE, capture_output=True, text=True)
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
    total = re.search(r'^Ran (\d+) tests', run.stderr, re.M)
    print(f'ran {total.group(1) if total else "?"} tests in {len(chosen)} modules; '
          f'{len(failed)} failing, {len(failed) - len(new)} known, {len(new)} NEW, {len(fixed)} FIXED')
    for f in new:
        print('NEW', f)
    for f in fixed:
        print('FIXED', f, '#', baseline[f])
    print('BASELINE_OK' if not new else 'BASELINE_NEW_FAILURES')
    return 1 if new else 0


if __name__ == '__main__':
    sys.exit(main())
