#!/usr/bin/env python3
"""Minimal sufficient checks for a change: one entry point for an iteration.

Usage:
  agx_iterate.py plan [--base REV] [--head REV] [--hypothesis correctness|perf|default] [--mode FAST|PERF|FULL]
                      [--run-offline] [--json OUT]

Reads the files changed since --base (default HEAD~1, plus the uncommitted
working tree), maps them to components and prints, machine-readably, which
checks are needed, which are skipped and why, and which hardware path (if
any) the change needs. --run-offline runs the selected unit tests and
records each stage's time. Hardware is never started from here: the plan
names the existing pipeline (umd-quick/umd-hotswap/gm-quick for a UMD-only
hot swap, mkexp/launch-exp for a KMD package, the m1n1 build only with the
user's explicit permission, AGENTS.md).

Mode rules (tools/agx_gm_perf.py documents the windows):
  correctness -> FAST (errors, TDR, fences, image; no speed claim)
  perf        -> PERF (10 + 60 s window, compare BEFORE/AFTER; a verdict
                 inside the 1 % noise is no-significant-change)
  default     -> FULL (HD and FHD score; required before a default flip,
                 a scheduling change, or any claim of a final gain)
A requested mode below the rule is refused for KMD/firmware-facing changes.
"""
import argparse
import json
import re
import subprocess
import sys
import tempfile
import time
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]

# (component, path prefixes): first match wins.
COMPONENTS = (
    ('m1n1', ('m1n1_windows', 'm1n1/', 'drivers/apple-agx/shared/include/j313_agx_g2')),
    ('umd', ('drivers/apple-agx/mesa/', 'drivers/apple-agx/d3d12/', 'drivers/apple-agx/windows/icd/',
             'drivers/apple-agx/umd/')),
    ('kmd', ('drivers/apple-agx/render-admission/', 'drivers/apple-agx/shared/',
             'drivers/apple-agx/windows/', 'drivers/apple-agx/admission/')),
    ('tests', ('tests/',)),
    ('tools', ('tools/', 'scripts/')),
    ('docs', ('investigation/', 'docs/', 'README', 'AGENTS.md')),
)
MODE_RANK = {'FAST': 0, 'PERF': 1, 'FULL': 2}
RULE = {'correctness': 'FAST', 'perf': 'PERF', 'default': 'FULL'}

HARDWARE = {
    'umd': dict(path='umd-hotswap', reboot=False,
                steps=['umd-quick.sh <baseN> <baseDir> <tag>  (ARM64 UMD only, version 30.0.<baseN>)',
                       'umd-hotswap.sh <dll>  (needs the base package installed; restore after)',
                       'agx_gravitymark_run.ps1 -Mode <mode>  then agx_gm_perf.py summary/compare']),
    'kmd': dict(path='package', reboot=True,
                steps=['mkexp.sh (snapshot, builder package) in parallel with the previous recovery',
                       'launch-exp.sh (stage, ordered restart; BEFORE/ACTUAL in EXPERIMENTS.md)',
                       'probe with agx_gravitymark_run.ps1 -Mode <mode>; recovery recNh/recover.sh']),
    'm1n1': dict(path='m1n1', reboot=True,
                 steps=['explicit user permission (AGENTS.md: G2 m1n1 implementation and runs)',
                        'separate m1n1 build with recorded build-info and SHA-256']),
}


def git(*args):
    return subprocess.run(['git', *args], cwd=ROOT, capture_output=True, text=True,
                          check=True).stdout


SUBMODULES = ('mu', 'm1n1_windows')


def changed_files(base, head=None):
    if head:                                                       # one commit range
        names = set(git('diff', '--name-only', base, head).split())
    else:                                                          # base..worktree
        names = set(git('diff', '--name-only', base).split())
        names |= set(git('ls-files', '--others', '--exclude-standard').split())
    # Submodule gitlinks (mu is dirty in this checkout and never ours to commit).
    return sorted(n for n in names if n and not n.startswith('.local/')
                  and (n not in SUBMODULES or n == 'm1n1_windows'))


def component(path):
    for name, prefixes in COMPONENTS:
        if any(path.startswith(p) for p in prefixes):
            return name
    return 'other'


def select_tests(paths):
    """Test modules that name a changed file (its basename or stem), plus
    changed test files themselves."""
    tests = sorted((ROOT / 'tests').glob('test_*.py'))
    texts = {t: t.read_text(errors='replace') for t in tests}
    chosen = {}
    for path in paths:
        p = Path(path)
        if p.parent.name == 'tests' and p.name.startswith('test_') and p.suffix == '.py':
            chosen.setdefault(p.stem, []).append('changed test')
            continue
        if component(path) in ('docs', 'other'):
            continue
        # A file name with its suffix, or a long stem: short keys match everything.
        keys = ({p.name} if p.suffix and len(p.name) >= 6 else set()) | \
               ({p.stem} if len(p.stem) >= 8 else set())
        for t, text in texts.items():
            if any(k in text for k in keys):
                chosen.setdefault(t.stem, []).append(p.name)
    return chosen


def plan(base='HEAD~1', hypothesis='correctness', mode=None, files=None, head=None):
    files = changed_files(base, head) if files is None else files
    comps = sorted({component(f) for f in files})
    code = [c for c in comps if c in ('umd', 'kmd', 'm1n1')]
    rule = RULE[hypothesis]
    chosen_mode = mode or rule
    refused = None
    if code and (set(code) & {'kmd', 'm1n1'}) and MODE_RANK[chosen_mode] < MODE_RANK[rule]:
        refused = '%s is below the %s rule for a %s change' % (chosen_mode, rule, '/'.join(code))
        chosen_mode = rule
    tests = select_tests(files)
    skipped = []
    if not code:
        skipped.append(dict(check='hardware', reason='no UMD/KMD/m1n1 source changed'))
        skipped.append(dict(check='builder package', reason='no driver source changed'))
    if 'm1n1' not in code:
        skipped.append(dict(check='m1n1 build', reason='hypervisor unchanged'))
    if code == ['umd']:
        skipped.append(dict(check='KMD package + reboot',
                            reason='UMD only: hot swap onto the installed base package'))
    hw = [dict(component=c, mode=chosen_mode, **HARDWARE[c]) for c in code]
    # A speed claim, a default flip and any KMD/m1n1 change are confirmed only
    # by a FULL HD+FHD run before they are called a gain or made default.
    full_required = bool(code) and (hypothesis != 'correctness' or bool(set(code) & {'kmd', 'm1n1'}))
    return dict(version=1, base=base, files=files, components=comps,
                offline_tests=sorted(tests), offline_reasons=tests,
                baseline_suite=bool(code) or 'tests' in comps,
                hardware=hw, gravitymark_mode=chosen_mode if code else None,
                mode_refused=refused, full_benchmark_required=full_required,
                skipped=skipped)


FAILURE = re.compile(r'^(FAIL|ERROR): (\S+) \(([^)]+)\)', re.M)


def failing(cwd, modules):
    """Failing test ids of the modules run from cwd (tests/), as
    tests/run_against_baseline.py names them."""
    run = subprocess.run([sys.executable, '-m', 'unittest', *modules], cwd=cwd,
                         capture_output=True, text=True)
    return {name if where.startswith('unittest.loader._FailedTest') else where
            for _, name, where in FAILURE.findall(run.stderr)}


def run_offline(result):
    """Selected modules on the working tree; failing modules again at --base
    (a temporary worktree): failures there too are pre-existing, the rest NEW."""
    stages = []
    t = time.time()
    failed = failing(ROOT / 'tests', result['offline_tests']) if result['offline_tests'] else set()
    stages.append(dict(stage='selected tests (working tree)', seconds=round(time.time() - t, 2),
                       modules=len(result['offline_tests']), failing=sorted(failed)))
    pre = set()
    if failed:
        t = time.time()
        with tempfile.TemporaryDirectory(prefix='agx-iterate-') as tmp:
            tree = Path(tmp) / 'base'
            git('worktree', 'add', '-q', '--detach', str(tree), result['base'])
            try:
                mods = sorted({f.split('.')[0] for f in failed})
                pre = failed & failing(tree / 'tests', mods)
            finally:
                git('worktree', 'remove', '--force', str(tree))
        stages.append(dict(stage='failing modules at base', seconds=round(time.time() - t, 2),
                           pre_existing=sorted(pre)))
    result['stages'] = stages
    result['new_failures'] = sorted(failed - pre)
    result['offline_ok'] = not result['new_failures']
    return result


def main(argv=None):
    ap = argparse.ArgumentParser(description=__doc__.split('\n')[0])
    sub = ap.add_subparsers(dest='cmd', required=True)
    p = sub.add_parser('plan')
    p.add_argument('--base', default='HEAD~1')
    p.add_argument('--head', help='end of a commit range (default: the working tree)')
    p.add_argument('--hypothesis', choices=sorted(RULE), default='correctness')
    p.add_argument('--mode', choices=sorted(MODE_RANK))
    p.add_argument('--run-offline', action='store_true')
    p.add_argument('--json')
    args = ap.parse_args(argv)
    t = time.time()
    result = plan(args.base, args.hypothesis, args.mode, head=args.head)
    result['plan_seconds'] = round(time.time() - t, 2)
    if args.run_offline:
        run_offline(result)
    text = json.dumps(result, indent=1)
    if args.json:
        Path(args.json).write_text(text + '\n')
    print(text)
    return 0 if result.get('offline_ok', True) else 1


if __name__ == '__main__':
    sys.exit(main())
