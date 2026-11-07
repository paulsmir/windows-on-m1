"""Original provenance rules for the opt-in legacy prepared compute object set."""
import hashlib
import importlib.util
import json
import os
from pathlib import Path
import re
import struct
import sys
import xml.etree.ElementTree as ET

LABELS = ('frontend', 'map', 'program', 'legacy', 'owned', 'common', 'facade', 'handoff')
PROFILE = 'drivers/apple-agx/mesa/scripts/prepared-nir-owner-profiles.json'
THIS = 'drivers/apple-agx/mesa/scripts/prepared_nir_owner.py'
BUILDER = 'drivers/apple-agx/mesa/scripts/build-prepared-nir-owner.py'
VERIFIER = 'drivers/apple-agx/render-admission/scripts/verify-prepared-nir-owner-provenance.py'
NORMAL = 'drivers/apple-agx/render-admission/scripts/verify-native-runtime-provenance.py'
GROUPS = {'runtime': 1, 'core_normal': 5, 'core_extra': 2, 'checked': 9}

def sha(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()

def load(path):
    return json.loads(Path(path).read_text(encoding='utf-8-sig'))

def key(path):
    return str(Path(path).resolve()).replace('\\', '/').casefold()

def require(ok, message):
    if not ok:
        raise ValueError(message)

def check_pin(pin):
    p = Path(pin['path'])
    require(p.is_file() and p.stat().st_size == pin['bytes'] and sha(p) == pin['sha256'], 'input hash: ' + str(p))

def pointer(value, expression):
    require(expression.startswith('/'), 'absolute JSON pointer required')
    for part in expression[1:].split('/'):
        part = part.replace('~1', '/').replace('~0', '~')
        value = value[int(part)] if isinstance(value, list) else value[part]
    return value

def fields(path):
    return {n.tag.rsplit('}', 1)[-1]: n.text for n in ET.parse(path).getroot().iter()}

def check_inputs(c):
    pins = {}
    for pin in c['pins']:
        k = key(pin['path'])
        require(k not in pins, 'duplicate input path')
        check_pin(pin)
        pins[k] = pin
    def pinned(path):
        require(key(path) in pins, 'unbound input: ' + str(path))
    for name in ('source_manifest', 'native_props', 'native_result', 'prepared_result', 'core_identity', 'checked_identity'):
        pinned(c[name])
    source = load(c['source_manifest'])
    require(re.fullmatch('[0-9a-f]{40}', source['repository_commit']) is not None, 'source commit')
    index = {x['path']: x['sha256'] for x in source['files']}
    require(len(index) == len(source['files']), 'duplicate source manifest path')
    project = Path(c['roots']['PROJECT'])
    profiles = load(project / PROFILE)
    require(key(sys.executable) in pins, 'running interpreter must be pinned')
    # These original scripts/profiles must be part of the SAME committed snapshot.
    for rel in (PROFILE, THIS, BUILDER, VERIFIER, NORMAL):
        require(rel in index and sha(project / rel) == index[rel], 'uncommitted hook: ' + rel)
        pinned(project / rel)
    normal_spec = importlib.util.spec_from_file_location('normal_runtime_provenance', project / NORMAL)
    normal = importlib.util.module_from_spec(normal_spec)
    normal_spec.loader.exec_module(normal)
    normal.verify(Path(c['source_manifest']), Path(c['native_props']), Path(c['native_result']), Path(c['prepared_result']))
    native = fields(c['native_props'])
    require(native['NativeRuntimeArchitecture'] == 'arm64' and native['NativeRuntimeGpuva'] == 'true', 'ARM64 GPUVA normal runtime required')
    require(key(native['NativeRuntimeNativeSource']) == key(c['roots']['NATIVE_SOURCE']), 'native source root')
    require(key(Path(c['native_result']).parent) == key(c['roots']['NATIVE_OUTPUT']), 'native output root')
    # Check source-root authority, not only an independently supplied header digest.
    identity = load(c['core_identity'])
    require(key(identity['mesa_source']) == key(c['roots']['MESA']) and key(identity['mesa_build']) == key(c['roots']['CORE_BUILD']), 'core source-root')
    header = Path(c['roots']['MESA']) / 'src/compiler/shader_info.h'
    pinned(header)
    require(sha(header) == identity['shader_info_sha256'], 'canonical header hash')
    native_result = load(c['native_result'])
    resource = native_result['resource_source']
    pinned(resource['path'])
    archives = c['archives']
    require(len(archives) == 17 and len({key(x['path']) for x in archives}) == 17, '17 unique archives required')
    require({g: sum(x['group'] == g for x in archives) for g in GROUPS} == GROUPS, 'archive groups')
    for a in archives:
        require(a['group'] in GROUPS, 'unknown archive group')
        pinned(a['path'])
        require(pins[key(a['path'])]['sha256'] == a['sha256'], 'archive digest authority')
        authority = a['authority']
        pinned(authority['path'])
        receipt = load(authority['path'])
        require(pointer(receipt, authority['sha256_pointer']) == a['sha256'], 'archive receipt digest')
        require(key(pointer(receipt, authority['path_pointer'])) == key(a['path']), 'archive receipt path')
    runtime = next(x for x in archives if x['group'] == 'runtime')
    require(key(runtime['path']) == key(native['NativeRuntimeLibrary']), 'normal runtime archive mismatch')
    # Exact normal deps must remain in normal props; feature adds only remaining11.
    deps = next((n.text for n in ET.parse(c['native_props']).getroot().iter() if n.tag.rsplit('}', 1)[-1] == 'AdditionalDependencies'), '')
    normal_paths = {key(x) for x in deps.split(';') if x.lower().endswith(('.a', '.lib')) and not x.startswith('%')}
    expected = {key(x['path']) for x in archives if x['group'] in ('runtime', 'core_normal')}
    require(expected <= normal_paths, 'normal archive closure mismatch')
    require(not (normal_paths & {key(x['path']) for x in archives if x['group'] in ('core_extra', 'checked')}), 'feature archive already in normal props')
    require([x['label'] for x in profiles['commands']] == list(LABELS), 'exact eight profiles')
    for unit in profiles['commands']:
        rel = unit['source']
        require(rel in index and sha(project / rel) == index[rel], 'production source: ' + rel)
        pinned(project / rel)
    require(c['clear_environment'] == ['CL', '_CL_'], 'compiler env must be cleared')
    return pins, profiles

def arguments(c, unit, out):
    roots = dict(c['roots'], OUT=str(out))
    def expand(s):
        for name, value in roots.items():
            s = s.replace('@' + name + '@', value)
        require('@' not in s, 'unresolved argv token')
        require(not re.search(r'(^|[\\/])tests([\\/]|$)|ref_redirect|refinspect|owner_callbacks', s, re.I), 'fixture input forbidden')
        return s
    return [expand(s) for s in unit['argv']]

def dependency_paths(stdout, stderr):
    result = []
    for raw in (stdout + b'\n' + stderr).decode('utf-8', errors='strict').splitlines():
        m = re.match(r'^\s*Note: including file:\s*(.+?)\s*$', raw)
        if m:
            result.append(m.group(1))
    return result

def check_closure(c, unit, argv, stdout, stderr, pins):
    require(key(argv[0]) in pins, 'compiler tool must be pinned')
    require(stderr == b'' or sha_bytes(stderr) in c['allowed_stderr_sha256'].get(unit['label'], []), 'unreviewed compiler stderr')
    diagnostic = []
    for line in stdout.decode('utf-8', errors='strict').splitlines(keepends=True):
        if re.match(r'^\s*Note: including file:', line):
            continue
        if line.strip() in ('', Path(unit['source']).name):
            continue
        diagnostic.append(line)
    diagnostic_bytes = ''.join(diagnostic).encode('utf-8')
    require(not diagnostic_bytes or sha_bytes(diagnostic_bytes) in c['allowed_stdout_diagnostic_sha256'].get(unit['label'], []), 'unreviewed compiler stdout diagnostic')
    deps = dependency_paths(stdout, stderr)
    require(deps, 'missing actual showIncludes closure')
    for path in deps:
        require(key(path) in pins, 'unknown dependency: ' + path)
        require('/tests/' not in key(path), 'test dependency')
        project = Path(c['roots']['PROJECT']).resolve()
        try:
            relative = Path(path).resolve().relative_to(project).as_posix()
        except ValueError:
            pass
        else:
            source_index = {x['path']: x['sha256'] for x in load(c['source_manifest'])['files']}
            require(relative in source_index and sha(path) == source_index[relative], 'uncommitted project dependency')
    headers = {key(p) for p in deps if Path(p).name.lower() == 'shader_info.h'}
    expected = key(Path(c['roots']['MESA']) / 'src/compiler/shader_info.h')
    if unit['label'] in ('frontend', 'map', 'handoff'):
        require(headers == {expected}, 'canonical header selection')
    require(not headers or headers == {expected}, 'mixed shader_info headers')
    return sorted(set(deps), key=key)

def sha_bytes(b):
    return hashlib.sha256(b).hexdigest()

def coff(path):
    require(Path(path).stat().st_size >= 20 and struct.unpack('<H', Path(path).read_bytes()[:2])[0] == 0xaa64, 'not ARM64 COFF: ' + str(path))

def verify_receipt(config_path, expected_config_sha, receipt_path, props_path):
    require(sha(config_path) == expected_config_sha, 'request digest')
    c = load(config_path)
    pins, profiles = check_inputs(c)
    r = load(receipt_path)
    require(r['status'] == 'EIGHT_OBJECTS_COMPLETE' and r['request_sha256'] == expected_config_sha, 'incomplete/wrong receipt')
    require(r['source_commit'] == load(c['source_manifest'])['repository_commit'], 'receipt source commit')
    require(r['source_manifest_sha256'] == sha(c['source_manifest']), 'receipt source manifest')
    require([u['label'] for u in r['objects']] == list(LABELS), 'missing/duplicate object')
    require(len({key(x['path']) for x in r['objects']}) == 8, 'duplicate object path')
    for unit, obj in zip(profiles['commands'], r['objects']):
        check_pin(obj)
        coff(obj['path'])
        require(obj['argv'] == arguments(c, unit, Path(receipt_path).parent), 'command/profile drift')
        check_pin(obj['stdout']); check_pin(obj['stderr'])
        require(obj['returncode'] == 0, 'compile result')
        deps = check_closure(c, unit, obj['argv'], Path(obj['stdout']['path']).read_bytes(), Path(obj['stderr']['path']).read_bytes(), pins)
        require(obj['dependencies'] == deps, 'closure receipt drift')
    require(r['native_result_sha256'] == sha(c['native_result']), 'runtime receipt drift')
    xml = ET.parse(props_path).getroot()
    f = fields(props_path)
    require(f['PreparedNirOwnerReceiptSha256'] == sha(receipt_path), 'props receipt')
    require(f['PreparedNirOwnerRequestSha256'] == expected_config_sha, 'props request')
    objects = [n.attrib['Include'] for n in xml.iter() if n.tag.rsplit('}', 1)[-1] == 'Object']
    require(objects == [x['path'] for x in r['objects']], 'props object list')
    libraries = [n.text for n in xml.iter() if n.tag.rsplit('}', 1)[-1] == 'AdditionalDependencies']
    wanted = [a['path'] for a in c['archives'] if a['group'] in ('core_extra', 'checked')]
    require(libraries == [';'.join(wanted) + ';%(AdditionalDependencies)'], 'props archive closure')
    check_inputs(c)
    return r
