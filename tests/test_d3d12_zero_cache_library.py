"""Opt-in original ZERO_NATIVE_CACHE host contract; parent BEFORE required."""
import json
import os
from pathlib import Path
import subprocess
import unittest

from d3d12_zero_cache_library_source import ROOT, sha


class ZeroCacheLibrary(unittest.TestCase):
    def test_actual_six_callbacks(self):
        target = os.environ.get('AGX_ZERO_CACHE_RECEIPTS')
        if not target:
            self.skipTest('parent BEFORE, source/tool approval and exact commands required')
        approval = Path(os.environ['AGX_ZERO_CACHE_APPROVED_MANIFEST'])
        self.assertEqual(sha(approval), os.environ['AGX_ZERO_CACHE_APPROVED_SHA256'])
        approved = json.loads(approval.read_text())
        self.assertEqual(approved['schema'], 1)
        before = os.environ['AGX_ZERO_CACHE_AUTHORIZED_BEFORE']
        self.assertTrue(before and not before.startswith('PENDING'))
        self.assertEqual(approved['approved_before'], before)
        self.assertEqual(str(Path(target).resolve()), approved['receipt_root'])
        # Includes this runner, module, fixture, extractor, real WDK and already
        # generated SDK data. Fail before ANY compiler invocation on a mismatch.
        for name, wanted in approved['inputs'].items():
            self.assertEqual(sha(name), wanted['sha256'], name)
            self.assertEqual(Path(name).stat().st_size, wanted['bytes'], name)
        compiler = Path(approved['compiler']['path'])
        self.assertEqual(str(compiler.resolve(strict=True)), approved['compiler']['resolved'])
        self.assertEqual(sha(compiler), approved['compiler']['sha256'])
        out = Path(target)
        self.assertFalse(out.exists(), 'do not overwrite an earlier attempt')
        out.mkdir(parents=True)
        (out / 'approval.json').write_bytes(approval.read_bytes())
        commands = approved['commands']
        self.assertEqual(len(commands), 2)
        self.assertEqual(commands[0][0], str(compiler))
        self.assertEqual(commands[1], [str(out / 'zero-cache-check')])
        self.assertIn(str(ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_zero_cache_library.cpp'), commands[0])
        self.assertIn(str(ROOT / 'tests/d3d12_zero_cache_library_fixture.cpp'), commands[0])
        (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        results = []
        for i, command in enumerate(commands):
            try:
                run = subprocess.run(command, cwd=out, capture_output=True, timeout=30)
            except subprocess.TimeoutExpired as error:
                (out / f'{i}-timeout.txt').write_text(str(error))
                self.fail('bounded host compiler/fixture timeout; preserve and report')
            (out / f'{i}-stdout.txt').write_bytes(run.stdout)
            (out / f'{i}-stderr.txt').write_bytes(run.stderr)
            results.append({'step': i, 'returncode': run.returncode})
            (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
            if run.returncode:
                self.fail(('HOST_COMPILE_FAILED' if i == 0 else 'FUNCTIONAL_CONTRACT_FAILED') +
                          ': ' + run.stderr.decode(errors='replace'))
            if i == 1:
                self.assertIn(b'ZERO_NATIVE_CACHE_PASS', run.stdout)
                self.assertIn(b'callbacks=6 actual_WDK_declarations=YES host_Windows_ABI=NO', run.stdout)
                print(run.stdout.decode().strip())
        manifest = {p.name: {'bytes': p.stat().st_size, 'sha256': sha(p)}
                    for p in sorted(out.iterdir()) if p.is_file()}
        (out / 'output-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
