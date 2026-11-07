"""EXP1940 build: the WOW64 UMD link failed with an undefined
_AdmissionUmdGpuvaValidateRefResident.  umd_gpuva_windows.c is compiled as C++
(/TP, the $cpp list of build-umd-x86-native.ps1) while agx_d3d12_windows.cpp
includes umd_internal.h inside extern "C" {}; a prototype without its own
extern "C" gave the definition C++ linkage.  Every umd_internal.h function that
a /TP unit defines must carry extern "C" on its prototype when a unit that sees C linkage (a /TC unit, or one
including umd_internal.h inside extern "C" {}) uses it."""
from pathlib import Path
import re
import unittest

ROOT = Path(__file__).resolve().parents[1]
AGX = ROOT / 'drivers/apple-agx'
HEADER = AGX / 'render-admission/umd/src/umd_internal.h'
BUILDERS = [AGX / 'render-admission/umd/build-umd-x86-native.ps1', AGX / 'render-admission/umd/build-umd-x64.ps1']


def units(kind):
    found = set()
    for builder in BUILDERS:
        block = re.search(r"\$" + kind + r" = @\((.*?)\)\n", builder.read_text(), re.S).group(1)
        found.update(re.findall(r"'([^']+\.c(?:pp)?)'", block))
    return [AGX / u.replace('\\', '/') for u in sorted(found)]


def c_view_units():
    """Units that see umd_internal.h prototypes with C linkage."""
    view = list(units('c'))
    for unit in units('cpp'):
        text = unit.read_text()
        include = text.find('#include "umd_internal.h"')
        if include >= 0 and re.search(r'extern "C"\s*\{[^}]*$', text[:include], re.S):
            view.append(unit)
    return view


class UmdInternalCLinkage(unittest.TestCase):
    def test_prototypes_defined_by_cpp_units_have_c_linkage(self):
        lines = HEADER.read_text().splitlines()
        defined = {}
        c_view_paths = {unit for unit in c_view_units()}
        for unit in units('cpp'):
            # a /TP unit including umd_internal.h inside extern "C" {} defines with C linkage
            if unit.suffix != '.c' or unit in c_view_paths:
                continue
            for name in re.findall(r'(?m)^[A-Za-z_][\w \*]*?\b(\w+)\([^;{]*\)\s*\{', unit.read_text()):
                defined[name] = unit.name
        c_view = {unit.name: unit.read_text() for unit in c_view_units()}
        missing = []
        for i, line in enumerate(lines):
            m = re.match(r'^(?!\s*(?:#|//|/\*|typedef|static))[A-Za-z_][\w \*]*?\b(\w+)\s*\([^;]*\);', line)
            if not m or m.group(1) not in defined:
                continue
            # its own guard: extern "C" directly above, or above the #endif closing #ifdef __cplusplus
            prev = [l.strip() for l in lines[max(0, i - 2):i]]
            before = prev[-1] if prev[-1:] != ['#endif'] else ' '.join(prev)
            users = [n for n, text in c_view.items() if re.search(r'\b' + m.group(1) + r'\b', text)]
            if users and 'extern "C"' not in before and 'extern "C"' not in line and not self._in_block(lines, i):
                missing.append(f'{m.group(1)} (defined in {defined[m.group(1)]}, used by {", ".join(users)})')
        self.assertEqual(missing, [])

    @staticmethod
    def _in_block(lines, i):
        depth = 0
        for line in lines[:i]:
            if re.search(r'extern "C"\s*\{', line):
                depth += 1
            elif depth and line.strip().startswith('}'):
                depth -= 1
        return depth > 0


if __name__ == '__main__':
    unittest.main()
