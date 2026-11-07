"""Opt-in actual Windows SDK CPU regression; default discovery runs no compiler."""
import hashlib
import json
import os
from pathlib import Path
import struct
import subprocess
import unittest

from d3d12_failed_table_publication_source import ROOT, SOURCE, FIXTURE, WDK_SHA256, sha, slices


class FailedTablePublication(unittest.TestCase):
    def test_actual_current_fill_publication(self):
        destination = os.environ.get('AGX_TABLE_PUBLICATION_RECEIPTS')
        if not destination:
            self.skipTest('parent BEFORE, actual SDK/tool/source pins and exact native argv required')
        self.assertEqual(os.name, 'nt', 'isolated native Windows x64 CPU test, no host ABI overlay')
        approval_path = Path(os.environ['AGX_TABLE_PUBLICATION_APPROVED_MANIFEST'])
        self.assertEqual(sha(approval_path), os.environ['AGX_TABLE_PUBLICATION_APPROVED_SHA256'])
        approval = json.loads(approval_path.read_text(encoding='utf-8-sig'))
        self.assertEqual(approval['schema'], 1)
        before = os.environ['AGX_TABLE_PUBLICATION_AUTHORIZED_BEFORE']
        self.assertTrue(before and not before.startswith('PENDING'))
        self.assertEqual(approval['approved_before'], before)
        out = Path(destination).resolve()
        self.assertEqual(str(out), approval['receipt_root'])
        self.assertFalse(out.exists(), 'fresh receipts only; never rerun over an earlier attempt')
        inputs = approval['inputs']
        for name, wanted in inputs.items():
            self.assertEqual(Path(name).stat().st_size, wanted['bytes'], name)
            self.assertEqual(sha(name), wanted['sha256'], name)
        compiler, linker = Path(approval['compiler']), Path(approval['linker'])
        metadata_path = Path(approval['generated_inputs'])
        metadata = json.loads(metadata_path.read_text())
        unit = Path(metadata['generated'])
        required = [SOURCE, FIXTURE, Path(__file__), ROOT / 'tests/d3d12_failed_table_publication_source.py',
                    compiler, linker, metadata_path, unit, Path(metadata['wdk'])]
        pinned = {str(Path(p).resolve()).casefold() for p in inputs}
        for path in required:
            self.assertIn(str(path.resolve()).casefold(), pinned, str(path))
        self.assertEqual(Path(metadata['source']).resolve(), SOURCE.resolve())
        self.assertEqual(Path(metadata['fixture']).resolve(), FIXTURE.resolve())
        self.assertEqual(metadata['source_sha256'], sha(SOURCE))
        self.assertEqual(metadata['fixture_sha256'], sha(FIXTURE))
        self.assertEqual(metadata['wdk_sha256'], WDK_SHA256)
        self.assertEqual(metadata['generated_sha256'], sha(unit))
        self.assertEqual(metadata['source_slices'], {
            name: hashlib.sha256(value.encode()).hexdigest() for name, value in slices(SOURCE.read_text()).items()})
        self.assertTrue(metadata['no_old_candidate_fallback'])
        self.assertTrue(metadata['no_Create_private_record_model'])
        commands = approval['commands']
        self.assertEqual(len(commands), 3)
        self.assertEqual(commands[0][0], str(compiler))
        self.assertIn(str(unit), commands[0])
        self.assertIn('/c', commands[0])
        self.assertIn('/sourceDependencies', commands[0])
        dependency = out / 'actual-deps.json'
        self.assertIn(str(dependency), commands[0])
        self.assertEqual(commands[1][0], str(linker))
        self.assertIn('/MACHINE:X64', commands[1])
        self.assertIn('/OPT:NOICF', commands[1])
        exe = out / 'actual-table-publication.exe'
        self.assertIn('/OUT:' + str(exe), commands[1])
        self.assertEqual(commands[2], [str(exe)])
        env = os.environ.copy()
        for name in ('CL', '_CL_', 'CFLAGS', 'CXXFLAGS', 'CPPFLAGS', 'LDFLAGS'):
            env.pop(name, None)
        env.update(approval['environment'])
        out.mkdir(parents=True)
        (out / 'approval.json').write_bytes(approval_path.read_bytes())
        (out / 'commands.json').write_text(json.dumps(commands, indent=2) + '\n')
        results = []
        for index, argv in enumerate(commands):
            try:
                result = subprocess.run(argv, cwd=out, env=env, capture_output=True, timeout=45)
            except subprocess.TimeoutExpired as error:
                (out / f'{index}-stdout.txt').write_bytes(error.stdout or b'')
                (out / f'{index}-stderr.txt').write_bytes(error.stderr or b'')
                (out / 'timeout.txt').write_text(str(error))
                self.fail('bounded native gate timeout; preserve attempt, do not retry')
            (out / f'{index}-stdout.txt').write_bytes(result.stdout)
            (out / f'{index}-stderr.txt').write_bytes(result.stderr)
            results.append({'step': index, 'returncode': result.returncode})
            (out / 'results.json').write_text(json.dumps(results, indent=2) + '\n')
            self.assertEqual(result.returncode, 0, ('COMPILE_OR_LINK_FAILURE_NOT_FUNCTIONAL_RED' if index < 2 else
                                                  'FUNCTIONAL_PUBLICATION_REGRESSION') + ': ' + result.stderr.decode(errors='replace'))
            if index == 0:
                data = json.loads(dependency.read_text(encoding='utf-8-sig'))['Data']
                allowed = {str(Path(path).resolve()).casefold(): wanted['sha256'] for path, wanted in inputs.items()}
                observed = []
                for name in [data['Source'], *data['Includes']]:
                    path = Path(name).resolve()
                    self.assertIn(str(path).casefold(), allowed, 'unregistered actual dependency: ' + str(path))
                    self.assertEqual(sha(path), allowed[str(path).casefold()])
                    observed.append({'path': str(path), 'sha256': sha(path)})
                (out / 'actual-inputs.json').write_text(json.dumps(observed, indent=2) + '\n')
            if index == 1:
                raw = exe.read_bytes()
                self.assertEqual(raw[:2], b'MZ')
                pe = struct.unpack_from('<I', raw, 60)[0]
                self.assertEqual(raw[pe:pe + 4], b'PE\0\0')
                self.assertEqual(struct.unpack_from('<H', raw, pe + 4)[0], 0x8664)
            if index == 2:
                self.assertIn(b'ACTUAL_FAILED_TABLE_PUBLICATION checks=137 failures=0 list_slots=51 queue_slots=7 '
                              b'candidate_scope=first_success_only cross_device_fix=NO', result.stdout)
                print(result.stdout.decode().strip())
        for name, wanted in inputs.items():
            self.assertEqual(sha(name), wanted['sha256'], 'input drift during execution: ' + name)
        manifest = {str(path.relative_to(out)): {'bytes': path.stat().st_size, 'sha256': sha(path)}
                    for path in out.rglob('*') if path.is_file()}
        (out / 'artifact-manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
