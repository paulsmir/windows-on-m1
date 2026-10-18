"""C15 immutable-cache invariants: real bytes, unsafe archives and graph drift.

No network/compiler/GPU. Tests catch source substitution, unreviewed target inputs,
archive path/link escape and cache edits before a compiler can execute CMake.
"""
import copy
import hashlib
import importlib.util
import io
import json
import os
from pathlib import Path
import re
import tarfile
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / 'drivers/apple-agx/mesa/scripts'
spec = importlib.util.spec_from_file_location('dxil_dependency', SCRIPTS / 'dxil_spirv_dependency.py')
dep = importlib.util.module_from_spec(spec)
spec.loader.exec_module(dep)


class DependencyCache(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory(prefix='dxil-dependency-')
        self.root = Path(self.tmp.name)
        self.archives = self.root / 'downloads'
        self.archives.mkdir()
        self.lock = json.loads((SCRIPTS / 'dxil_spirv_dependency_lock.json').read_text(encoding='utf-8'))
        self.lock['files'] = []
        self.lock['targets'] = {name: [] for name in self.lock['targets']}
        self.payloads = {c['name']: {} for c in self.lock['components']}
        for number, name in enumerate(self.lock['targets']):
            path = f'dxil-spirv/fixture/{number}.cpp'
            self.lock['targets'][name] = [path]
            self.add_file(path, f'original fixture TU {number}\n'.encode(), 'TU')
        cmake = ''.join(f'add_library({name} STATIC fixture/{number}.cpp)\n'
                        for number, name in enumerate(self.lock['targets']))
        self.add_file('dxil-spirv/CMakeLists.txt', cmake.encode(), 'metadata')
        self.add_file('dxil-spirv/bc/CMakeLists.txt', b'# fixture graph in root\n', 'metadata')
        self.add_file('dxil-spirv/third_party/CMakeLists.txt', b'# fixture graph in root\n', 'metadata')
        self.add_file('dxbc-spirv/LICENSE', b'fixture license B\n', 'metadata')
        self.add_file('spirv-headers/LICENSE', b'fixture license H\n', 'metadata')
        self.lock['expected_file_count'] = len(self.lock['files'])
        self.lock['expected_tu_count'] = 9
        for group in self.lock['notice_groups'].values():
            group['file_count'] = 0
        self.lock['notice_groups']['N02']['file_count'] = 9
        for component in self.lock['components']:
            archive = self.write_archive(component)
            component['archive_sha256'] = dep.sha256_file(archive)
        self.cache = self.root / 'cache'

    def tearDown(self):
        self.tmp.cleanup()

    def add_file(self, path, data, role):
        component, relative = path.split('/', 1)
        self.payloads[component][relative] = data
        self.lock['files'].append({'path': path, 'sha256': hashlib.sha256(data).hexdigest(),
                                  'role': role, 'notice': 'N02' if role == 'TU' else None})

    def write_archive(self, component, extra=()):
        path = self.archives / dep.archive_filename(component)
        with tarfile.open(path, 'w:gz') as archive:
            for relative, data in self.payloads[component['name']].items():
                entry = tarfile.TarInfo(component['archive_root'] + '/' + relative)
                entry.size = len(data)
                archive.addfile(entry, io.BytesIO(data))
            for entry, data in extra:
                archive.addfile(entry, io.BytesIO(data) if data is not None else None)
        return path

    def test_reviewed_lock_matches_all_197_documented_inputs(self):
        lock = dep.load_lock(SCRIPTS / 'dxil_spirv_dependency_lock.json')
        text = (ROOT / lock['review']['inventory']).read_text(encoding='utf-8')
        rows = re.findall(r'\|`([^`]+)`\|(TU|H|IDE)\|([^|]+)\|`([0-9a-f]{64})`\|', text)
        metadata = re.findall(r'\|`((?:dxil-spirv|dxbc-spirv|spirv-headers)/[^`]+)`\|`([0-9a-f]{64})`\|', text)
        expected = {p: h for p, _, _, h in rows} | dict(metadata)
        self.assertEqual({f['path']: f['sha256'] for f in lock['files']}, expected)
        self.assertEqual(len(expected), 197)
        self.assertEqual(sum(f['role'] == 'TU' for f in lock['files']), 83)
        self.assertEqual(len(dep.collect_notices(lock, text)), 14)

    def test_stage_and_revalidate_real_archive_bytes(self):
        receipt = dep.stage_cache(self.lock, self.archives, self.cache)
        self.assertEqual(receipt['reviewed_files'], 14)
        self.assertEqual(dep.verify_cache(self.lock, self.cache), receipt)
        self.assertEqual((self.cache / 'trees/dxil-spirv/fixture/0.cpp').read_bytes(), b'original fixture TU 0\n')
        with self.assertRaisesRegex(dep.DependencyError, 'exists'):
            dep.stage_cache(self.lock, self.archives, self.cache)

    def test_archive_digest_and_selected_input_drift_fail_before_admission(self):
        component = self.lock['components'][0]
        archive = self.archives / dep.archive_filename(component)
        archive.write_bytes(archive.read_bytes() + b'corrupt')
        with self.assertRaisesRegex(dep.DependencyError, 'archive digest'):
            dep.stage_cache(self.lock, self.archives, self.cache)
        self.assertFalse(self.cache.exists())
        archive = self.write_archive(component)
        component['archive_sha256'] = dep.sha256_file(archive)
        self.lock['files'][0]['sha256'] = '0' * 64
        with self.assertRaisesRegex(dep.DependencyError, 'reviewed input'):
            dep.stage_cache(self.lock, self.archives, self.cache)
        self.assertFalse(self.cache.exists())

    def test_dirty_cache_edits_additions_links_and_receipt_fail_closed(self):
        dep.stage_cache(self.lock, self.archives, self.cache)
        tree = self.cache / 'trees/dxil-spirv'
        source = tree / 'fixture/0.cpp'
        original = source.read_bytes()
        source.write_bytes(b'changed upstream')
        with self.assertRaisesRegex(dep.DependencyError, 'tree'):
            dep.verify_cache(self.lock, self.cache)
        source.write_bytes(original)
        extra = tree / 'unreviewed.cmake'
        extra.write_text('execute_process(COMMAND unexpected)')
        with self.assertRaisesRegex(dep.DependencyError, 'tree'):
            dep.verify_cache(self.lock, self.cache)
        extra.unlink()
        source.unlink()
        source.symlink_to(self.root / 'outside')
        with self.assertRaises(dep.DependencyError):
            dep.verify_cache(self.lock, self.cache)
        source.unlink()
        source.write_bytes(original)
        outside = self.root / 'outside-hardlink'
        outside.write_bytes(original)
        source.unlink()
        os.link(outside, source)
        with self.assertRaises(dep.DependencyError):
            dep.verify_cache(self.lock, self.cache)
        source.unlink()
        outside.unlink()
        source.write_bytes(original)
        (self.cache / 'receipt.json').write_text('{}')
        with self.assertRaisesRegex(dep.DependencyError, 'receipt'):
            dep.verify_cache(self.lock, self.cache)

    def test_unsafe_archive_paths_links_duplicates_and_bombs_are_refused(self):
        component = self.lock['components'][0]
        root = component['archive_root']
        cases = []
        for name in ('../outside', '/absolute', root + '/../outside', root + '/C:escape',
                     root + '/back\\escape', root + '/CON.txt', root + '/CON .txt', root + '/CONOUT$', root + '/é.txt', root + '/trailing.', root + '/trailing ', root + '/.git/config', root + '/.GIT/config'):
            entry = tarfile.TarInfo(name)
            entry.size = 1
            cases.append((entry, b'x'))
        for kind in (tarfile.SYMTYPE, tarfile.LNKTYPE, tarfile.FIFOTYPE):
            entry = tarfile.TarInfo(root + '/escape')
            entry.type = kind
            entry.linkname = '../../outside'
            cases.append((entry, None))
        for entry, data in cases:
            with self.subTest(name=entry.name, kind=entry.type):
                archive = self.write_archive(component, [(entry, data)])
                with self.assertRaises(dep.DependencyError):
                    dep.inspect_archive(component, archive)
                self.assertFalse((self.root / 'outside').exists())
        entry = tarfile.TarInfo(root + '/FIXTURE/0.cpp')
        entry.size = 1
        with self.assertRaisesRegex(dep.DependencyError, 'collision'):
            dep.inspect_archive(component, self.write_archive(component, [(entry, b'x')]))
        component['max_unpacked_bytes'] = 1
        with self.assertRaisesRegex(dep.DependencyError, 'limit'):
            dep.inspect_archive(component, self.write_archive(component))

    def test_declared_cmake_drift_is_refused_before_any_cmake_execution(self):
        component = self.lock['components'][0]
        text = self.payloads['dxil-spirv']['CMakeLists.txt'].replace(b'fixture/0.cpp', b'unreviewed.cpp')
        self.payloads['dxil-spirv']['CMakeLists.txt'] = text
        for item in self.lock['files']:
            if item['path'] == 'dxil-spirv/CMakeLists.txt':
                item['sha256'] = hashlib.sha256(text).hexdigest()
        component['archive_sha256'] = dep.sha256_file(self.write_archive(component))
        with self.assertRaisesRegex(dep.DependencyError, 'declared target source'):
            dep.stage_cache(self.lock, self.archives, self.cache)
        self.assertFalse(self.cache.exists())

    def test_gitlink_and_configured_runtime_graph_drift(self):
        dep.verify_gitlinks(self.lock, copy.deepcopy(self.lock['gitlinks']))
        bad = copy.deepcopy(self.lock['gitlinks'])
        bad['dxil-spirv']['third_party/spirv-headers'] = 'f' * 40
        with self.assertRaisesRegex(dep.DependencyError, 'gitlink'):
            dep.verify_gitlinks(self.lock, bad)
        reply = self.root / 'reply'
        reply.mkdir()
        for name, paths in self.lock['targets'].items():
            (reply / (name + '.json')).write_text(json.dumps({'name': name, 'sources': [{'path': p.removeprefix('dxil-spirv/')} for p in paths]}))
        # A prefix collision was the real EXP1386 gate bug. Exact JSON identity
        # must accept an unused dxbc-spirv-test target, without compiling it.
        (reply / 'test.json').write_text(json.dumps({'name': 'dxbc-spirv-test', 'sources': [{'path': 'unused.cpp'}]}))
        self.assertEqual(dep.verify_codemodel(self.lock, reply), 9)
        path = reply / 'dxbc-spirv.json'
        data = json.loads(path.read_text(encoding='utf-8'))
        data['sources'].append({'path': 'unreviewed.cpp'})
        path.write_text(json.dumps(data))
        with self.assertRaisesRegex(dep.DependencyError, 'target source'):
            dep.verify_codemodel(self.lock, reply)

    def test_materialized_source_mounts_are_exact_and_build_outputs_stay_outside_cache(self):
        dep.stage_cache(self.lock, self.archives, self.cache)
        source = self.root / 'build-source'
        result = dep.materialize_source(self.lock, self.cache, source)
        self.assertEqual(result['status'], 'source_materialized_not_built')
        self.assertEqual((source / 'subprojects/dxbc-spirv/LICENSE').read_bytes(), b'fixture license B\n')
        self.assertEqual((source / 'third_party/spirv-headers/LICENSE').read_bytes(), b'fixture license H\n')
        dep.verify_build_source(self.lock, self.cache, source)
        with self.assertRaisesRegex(dep.DependencyError, 'exists'):
            dep.materialize_source(self.lock, self.cache, source)
        (source / 'CMakeCache.txt').write_text('unexpected in-source build output')
        with self.assertRaisesRegex(dep.DependencyError, 'tree'):
            dep.verify_build_source(self.lock, self.cache, source)
        dep.verify_cache(self.lock, self.cache)

    def test_build_contract_preserves_nine_archives_crt_and_notice_gate(self):
        dep.stage_cache(self.lock, self.archives, self.cache)
        contract = dep.build_contract(self.lock, self.cache)
        self.assertEqual(contract['status'], 'prepared_not_built')
        self.assertEqual(len(contract['archives']), 9)
        self.assertEqual(contract['toolchain']['crt'], 'MultiThreadedDLL')
        self.assertEqual(contract['required_notice_groups'], [f'N{i:02d}' for i in range(1, 15)])
        self.assertFalse(contract['production_linkage_authorized'])
        bad = copy.deepcopy(self.lock)
        bad['build']['options']['DXIL_SPIRV_NATIVE_LLVM'] = 'ON'
        with self.assertRaisesRegex(dep.DependencyError, 'options'):
            dep.validate_lock(bad)


if __name__ == '__main__':
    unittest.main()
