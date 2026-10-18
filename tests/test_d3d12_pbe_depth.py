"""Gallium view input DATA + native paired descriptors through current pure projector.

Invariant: true3D PBE carries base depth separately from selected mip; normalized
2D/array paths retain selected layer count. No private candidate fallback.
"""
import ast
import hashlib
import json
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_dxbc_to_nir import ROOT, local_tree
from test_d3d12_view_mapping import block


def projector():
    path = ROOT / 'drivers/apple-agx/mesa/scripts/build-native-asahi-state.py'
    tree = ast.parse(path.read_text())
    found = [n for n in tree.body if isinstance(n, ast.FunctionDef) and n.name == 'project_3d_pbe_base_depth']
    if len(found) != 1:
        raise AssertionError('one actual current pure PBE projector required')
    namespace = {}
    exec(compile(ast.fix_missing_locations(ast.Module(body=found, type_ignores=[])), str(path), 'exec'), namespace)
    # Verify production wiring remains under args.project, without importing or
    # executing the build script's command-line side effects.
    calls = [n for n in ast.walk(tree) if isinstance(n, ast.Call) and isinstance(n.func, ast.Name)
             and n.func.id == 'project_3d_pbe_base_depth']
    if len(calls) != 1:
        raise AssertionError('one production projector invocation required')
    guarded = [n for n in ast.walk(tree) if isinstance(n, ast.If)
               and isinstance(n.test, ast.Attribute) and isinstance(n.test.value, ast.Name)
               and n.test.value.id == 'args' and n.test.attr == 'project'
               and calls[0] in list(ast.walk(n))]
    if len(guarded) != 1:
        raise AssertionError('actual production invocation must remain under args.project')
    return namespace['project_3d_pbe_base_depth']


def selected(mesa, config, unit):
    chunks = []
    for item in config['functions']:
        if item['unit'] != unit:
            continue
        path = mesa / item['source_relative']
        raw = path.read_bytes()
        if hashlib.sha256(raw).hexdigest() != item['source_sha256']:
            raise AssertionError('pinned actual native source drift: ' + str(path))
        text = raw.decode()
        function = block(text, item['marker'])
        if hashlib.sha256(function.encode()).hexdigest() != item['body_sha256']:
            raise AssertionError('actual native function drift: ' + item['name'])
        chunks.append(text[:text.index('*/') + 2] + '\n' + function + '\n')
    return ''.join(chunks)



class ActualPbeDepth(unittest.TestCase):
    def test_true3d_base_depth_and_non3d_controls(self):
        mesa = local_tree('reference', 'mesa')
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        if not mesa or not build:
            self.skipTest('pinned actual Mesa/AIL build unavailable')
        cc = Path('/Applications/Xcode.app/Contents/Developer/Toolchains/XcodeDefault.xctoolchain/usr/bin/clang')
        if not cc.exists():
            self.skipTest('accepted native host compiler unavailable')
        config = json.loads((ROOT / 'tests/d3d12_pbe_depth_build.json').read_text())
        with tempfile.TemporaryDirectory(prefix='actual-pbe-depth-') as directory:
            out = Path(directory)
            state = selected(mesa, config, 'state')
            (out / 'selected-state.inc').write_text(projector()(state))
            (out / 'selected-view.inc').write_text(selected(mesa, config, 'view'))
            (out / 'd3d12_pbe_depth_boundary.h').write_bytes((ROOT / 'tests/d3d12_pbe_depth_boundary.h').read_bytes())
            (out / 'descriptor.c').write_bytes((ROOT / 'tests/d3d12_pbe_depth_fixture.c').read_bytes())
            (out / 'view.c').write_text('#include "util/u_helpers.h"\n#include "util/u_inlines.h"\n#include "selected-view.inc"\n')
            flags = config['flags']
            commands = [
                [str(cc), '-DC24_SELECTED_SOURCE="' + str(out / 'selected-state.inc') + '"', *flags, '-c', str(out / 'descriptor.c'), '-o', str(out / 'descriptor.o')],
                [str(cc), *flags, '-c', str(out / 'view.c'), '-o', str(out / 'view.o')],
                [str(cc) + '++', '-isysroot', '/Applications/Xcode.app/Contents/Developer/Platforms/MacOSX.platform/Developer/SDKs/MacOSX27.0.sdk',
                 '-fsanitize=address,undefined', '-Wl,-dead_strip', str(out / 'descriptor.o'), str(out / 'view.o'),
                 *[str(build / n) for n in config['libs']], '-L/opt/homebrew/opt/zstd/lib', '-lzstd', '-lz', '-lpthread', '-o', str(out / 'check')],
                [str(out / 'check')],
            ]
            for argv in commands:
                result = subprocess.run(argv, cwd=build, capture_output=True, timeout=120)
                self.assertEqual(result.returncode, 0, result.stdout.decode(errors='replace') + result.stderr.decode(errors='replace'))
            self.assertIn(b'C24_ACTUAL_DESCRIPTOR_PACK_PASS cases=18', result.stdout)
            self.assertIn(b'C1436_PBE_BASE_DEPTH checks=33 failures=0', result.stdout)
