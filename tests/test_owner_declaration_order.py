"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A static function used in the D3D12 owner/DDI translation units before its
first declaration. The host suites compile single function bodies (slicers)
and the arm64 MSVC path did not reject it, but the x86 clang build of the
package failed three times on it (OV3 4c097f70, owner_ov2_ranges at
e900735d fixed in f38f9835). This scans the files as the package
preprocesses them (the native owner, GPUVA winsys and OV3 configuration,
with and without the optional prepared-NIR / DXIL / staging switches).
"""
import re
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
FILES = [ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp',
         ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp']
BASE = {'APPLE_AGX_D3D12_NATIVE_OWNER', 'APPLE_AGX_GPUVA_WINSYS', 'AGX_D3D12_OWNER_OV3', '_MSC_VER', '_WIN32'}
OPTIONAL = ['APPLE_AGX_D3D12_PREPARED_NIR_OWNER', 'APPLE_AGX_D3D12_DXIL', 'APPLE_AGX_NATIVE_STAGING',
            'APPLE_AGX_D3D12_NIR_TEXT_DUMP']


def strip_comments(src):
    def blank(m):
        return re.sub(r'[^\n]', ' ', m.group(0))
    src = re.sub(r'/\*.*?\*/', blank, src, flags=re.S)
    src = re.sub(r'//[^\n]*', blank, src)
    return re.sub(r'"(\\.|[^"\\\n])*"', blank, src)


def evaluate(expr, defined):
    expr = re.sub(r'defined\s*\(\s*(\w+)\s*\)', lambda m: ' 1 ' if m.group(1) in defined else ' 0 ', expr)
    expr = re.sub(r'defined\s+(\w+)', lambda m: ' 1 ' if m.group(1) in defined else ' 0 ', expr)
    expr = re.sub(r'\b[A-Za-z_]\w*\b', lambda m: ' 1 ' if m.group(0) in defined else ' 0 ', expr)
    expr = expr.replace('&&', ' and ').replace('||', ' or ').replace('!', ' not ')
    return bool(eval(expr, {'__builtins__': {}}))


def preprocess(src, defined):
    """Blank the lines of inactive #if branches (no macro expansion)."""
    out, stack = [], []  # stack of (active, taken)
    for line in src.split('\n'):
        d = re.match(r'\s*#\s*(ifdef|ifndef|if|elif|else|endif)\b(.*)', line)
        active = all(a for a, _ in stack)
        if d:
            kind, rest = d.group(1), d.group(2).strip()
            if kind in ('ifdef', 'ifndef', 'if'):
                if kind == 'ifdef':
                    cond = rest.split()[0] in defined
                elif kind == 'ifndef':
                    cond = rest.split()[0] not in defined
                else:
                    cond = evaluate(rest, defined)
                stack.append((cond, cond))
            elif kind == 'elif':
                _, taken = stack.pop()
                cond = not taken and evaluate(rest, defined)
                stack.append((cond, taken or cond))
            elif kind == 'else':
                _, taken = stack.pop()
                stack.append((not taken, True))
            else:
                stack.pop()
            out.append('')
            continue
        out.append(line if active else '')
    return '\n'.join(out)


def violations(path, defined):
    src = preprocess(strip_comments(path.read_text()), defined)
    first = {}
    for m in re.finditer(r'^static\b[^;{=()]*?\b([A-Za-z_]\w*)\s*\(', src, re.M):
        first.setdefault(m.group(1), m.start())
    found = []
    for name, pos in first.items():
        use = re.search(r'(?<![\w.>:])' + re.escape(name) + r'\s*\(', src[:pos])
        if use:
            found.append('%s:%d: %s used before its declaration at line %d' % (
                path.name, src[:use.start()].count('\n') + 1, name, src[:pos].count('\n') + 1))
    return found


class DeclarationOrder(unittest.TestCase):
    def test_static_functions_declared_before_use(self):
        configs = [set(BASE)] + [set(BASE) | {o} for o in OPTIONAL] + [set(BASE) | set(OPTIONAL)]
        found = set()
        for defined in configs:
            for path in FILES:
                found.update(violations(path, defined))
        self.assertEqual(sorted(found), [])

    def test_checker_catches_the_e900735d_break(self):
        src = ('static int a(void) { return b(); }\n'
               'static int b(void) { return 1; }\n')
        tmp = ROOT / 'tests' / '.declorder_probe.cpp'
        tmp.write_text(src)
        try:
            self.assertEqual(len(violations(tmp, set(BASE))), 1)
        finally:
            tmp.unlink()


if __name__ == '__main__':
    unittest.main()
