"""Real-file provenance mutations; no compiler, subprocess, external cache or network.

The object headers/receipts below are test DATA, not claimed compiled output.
This tests the production verifier against stale files and malformed identities.
"""
import copy
import importlib.util
import json
from pathlib import Path
import shutil
import struct
import sys
import tempfile
import unittest
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[1]
SCRIPTS = ROOT / 'drivers/apple-agx/mesa/scripts'
spec = importlib.util.spec_from_file_location('prepared_rules', SCRIPTS/'prepared_nir_owner.py')
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)


class Provenance(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.base = Path(self.tmp.name)
        self.project = self.base/'project'; self.out = self.base/'objects'; self.out.mkdir()
        self.pins = []
        self.sources = []
        self.config = {'roots': {'PROJECT': str(self.project), 'MESA': str(self.base/'mesa'),
            'CORE_BUILD': str(self.base/'core'), 'NATIVE_SOURCE': str(self.base/'native-source'),
            'NATIVE_OUTPUT': str(self.base/'native')}, 'pins': self.pins,
            'clear_environment': ['CL', '_CL_'], 'archives': [], 'allowed_stderr_sha256': {}, 'allowed_stdout_diagnostic_sha256': {}}
        def write(path, data):
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_bytes(data if isinstance(data, bytes) else data.encode())
            return path
        self.write = write
        def pin(path):
            item = {'path': str(path), 'bytes': path.stat().st_size, 'sha256': m.sha(path)}
            self.pins.append(item); return item
        self.pin = pin
        pin(Path(sys.executable))
        def source(rel, data):
            p = write(self.project/rel, data); pin(p)
            self.sources.append({'path': rel, 'sha256': m.sha(p)})
        self.source = source
        for rel in (m.THIS, m.BUILDER, m.VERIFIER, m.NORMAL):
            source(rel, (ROOT/rel).read_bytes())
        tool = write(self.base/'compiler-input-only', b'not executed'); pin(tool)
        header = write(self.base/'mesa/src/compiler/shader_info.h', b'header identity'); pin(header)
        profile = {'commands': [], 'environment': {}}
        for label in m.LABELS:
            rel = 'drivers/apple-agx/mesa/dxil/'+label+'.c'
            source(rel, b'original test data, never compiled')
            profile['commands'].append({'label': label, 'source': rel, 'timeout_seconds': 1,
                'argv': [str(tool), '/showIncludes', '/c', '@PROJECT@/'+rel, '/Fo@OUT@/'+label+'.obj']})
        source(m.PROFILE, json.dumps(profile))
        manifest = write(self.base/'source.json', json.dumps({'repository_commit': 'a'*40, 'files': self.sources}))
        self.config['source_manifest'] = str(manifest); pin(manifest)
        resource = write(self.base/'native-source/src/gallium/frontends/d3d10umd/Resource.cpp', b'actual projection data'); pin(resource)
        prepared = write(self.base/'prepared.json', json.dumps({'overlays': {m_resource(): {'final_sha256': m.sha(resource)}}})); pin(prepared)
        self.config['prepared_result'] = str(prepared)
        core = write(self.base/'core/source-root.json', json.dumps({'mesa_source': self.config['roots']['MESA'],
            'mesa_build': self.config['roots']['CORE_BUILD'], 'shader_info_sha256': m.sha(header)})); pin(core)
        self.config['core_identity'] = str(core)
        authority = self.base/'archives.json'; archive_data = []
        for group, count in m.GROUPS.items():
            for n in range(count):
                p = write(self.base/'libs'/(group+str(n)+'.lib'), (group+str(n)).encode()); pin(p)
                a = {'group': group, 'path': str(p), 'sha256': m.sha(p), 'authority': {'path': str(authority),
                    'path_pointer': '/'+str(len(archive_data))+'/path', 'sha256_pointer': '/'+str(len(archive_data))+'/sha256'}}
                self.config['archives'].append(a); archive_data.append({'path': str(p), 'sha256': m.sha(p)})
        write(authority, json.dumps(archive_data)); pin(authority)
        self.config['checked_identity'] = str(authority)
        archive = self.config['archives'][0]
        result = write(self.base/'native/result.json', json.dumps({'source_commit': 'a'*40,
            'source_manifest_sha256': m.sha(manifest), 'library': archive,
            'prepared_result_sha256': m.sha(prepared), 'resource_source': {'path': str(resource), 'sha256': m.sha(resource)}})); pin(result)
        self.config['native_result'] = str(result)
        normal = ET.Element('Project'); g = ET.SubElement(normal, 'PropertyGroup')
        for k,v in {'NativeRuntimeArchitecture': 'arm64', 'NativeRuntimeGpuva': 'true', 'NativeRuntimeSourceCommit': 'a'*40, 'NativeRuntimeSourceManifestSha256':m.sha(manifest),
            'NativeRuntimeLibrary':archive['path'], 'NativeRuntimeLibrarySha256':archive['sha256'],
            'NativeRuntimeNativeSource':self.config['roots']['NATIVE_SOURCE']}.items(): ET.SubElement(g,k).text=v
        ET.SubElement(normal,'AdditionalDependencies').text=';'.join(a['path'] for a in self.config['archives'] if a['group'] in ('runtime','core_normal'))
        props = self.base/'native/NativeRuntime.props'; ET.ElementTree(normal).write(props); pin(props)
        self.config['native_props'] = str(props)
        self.request = write(self.base/'request.json', json.dumps(self.config))
        self.receipt = self.out/'result.json'; self.props = self.out/'PreparedNirOwner.props'
        objects = []
        for unit in profile['commands']:
            label = unit['label']; obj = write(self.out/(label+'.obj'), struct.pack('<H',0xaa64)+b'\0'*18)
            stdout = write(self.out/(label+'.stdout.bin'), 'Note: including file: '+str(header)+'\n')
            stderr = write(self.out/(label+'.stderr.bin'), b'')
            def rec(p): return {'path':str(p),'bytes':p.stat().st_size,'sha256':m.sha(p)}
            objects.append(dict(rec(obj), label=label, argv=m.arguments(self.config,unit,self.out),
                returncode=0, stdout=rec(stdout), stderr=rec(stderr), dependencies=[str(header)]))
        self.r = {'status':'EIGHT_OBJECTS_COMPLETE','request_sha256':m.sha(self.request),
            'source_commit':'a'*40,'source_manifest_sha256':m.sha(manifest),'native_result_sha256':m.sha(result),'objects':objects}
        self.save_receipt()

    def save_receipt(self):
        self.write(self.receipt, json.dumps(self.r))
        root=ET.Element('Project'); ET.SubElement(root,'PreparedNirOwnerReceiptSha256').text=m.sha(self.receipt)
        ET.SubElement(root,'PreparedNirOwnerRequestSha256').text=m.sha(self.request)
        for o in self.r['objects']: ET.SubElement(root,'Object',Include=o['path'])
        ET.SubElement(root,'AdditionalDependencies').text=';'.join(a['path'] for a in self.config['archives'] if a['group'] in ('core_extra','checked'))+';%(AdditionalDependencies)'
        ET.ElementTree(root).write(self.props)

    def verify(self):
        return m.verify_receipt(self.request,m.sha(self.request),self.receipt,self.props)

    def test_complete_real_files(self): self.verify()
    def test_missing_object(self):
        Path(self.r['objects'][0]['path']).unlink()
        with self.assertRaises(ValueError): self.verify()
    def test_duplicate_object(self):
        self.r['objects'][1]=copy.deepcopy(self.r['objects'][0]); self.save_receipt()
        with self.assertRaises(ValueError): self.verify()
    def test_stale_source_bytes(self):
        p=self.project/self.sources[-2]['path']; p.write_bytes(p.read_bytes()+b'drift')
        with self.assertRaises(ValueError): self.verify()
    def test_resource_cpp_mismatch(self):
        p=Path(json.loads(Path(self.config['native_result']).read_text())['resource_source']['path']); p.write_bytes(b'wrong')
        with self.assertRaises(ValueError): self.verify()
    def test_wrong_snapshot(self):
        self.r['source_commit']='b'*40; self.save_receipt()
        with self.assertRaises(ValueError): self.verify()
    def test_test_forced_include_in_receipt(self):
        self.r['objects'][0]['argv'].append('/FItests/legacy_prepared_ref_redirect.h'); self.save_receipt()
        with self.assertRaises(ValueError): self.verify()
    def test_wrong_coff_machine(self):
        p=Path(self.r['objects'][0]['path']);p.write_bytes(struct.pack('<H',0x8664)+b'\0'*18)
        self.r['objects'][0]['sha256']=m.sha(p);self.save_receipt()
        with self.assertRaises(ValueError): self.verify()
    def test_archive_changed(self):
        Path(self.config['archives'][-1]['path']).write_bytes(b'new archive')
        with self.assertRaises(ValueError): self.verify()


def m_resource(): return 'src/gallium/frontends/d3d10umd/Resource.cpp'

if __name__ == '__main__': unittest.main()
