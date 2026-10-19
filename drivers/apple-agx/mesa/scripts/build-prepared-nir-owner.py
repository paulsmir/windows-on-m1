"""Build ONLY eight original legacy prepared-compute objects in a fresh directory."""
import argparse
import json
import os
from pathlib import Path
import signal
import subprocess
import sys
import time
import xml.etree.ElementTree as ET
sys.path.insert(0, str(Path(__file__).resolve().parent))
from prepared_nir_owner import (check_inputs, arguments, check_closure, coff,
    sha, load, require, key, verify_receipt)


def record(path):
    p = Path(path)
    return {'path': str(p.resolve()), 'bytes': p.stat().st_size, 'sha256': sha(p)}


def owned_run(argv, env, cwd, timeout, taskkill):
    process = subprocess.Popen(argv, cwd=cwd, env=env, stdout=subprocess.PIPE,
        stderr=subprocess.PIPE, start_new_session=(os.name != 'nt'),
        creationflags=subprocess.CREATE_NEW_PROCESS_GROUP if os.name == 'nt' else 0)
    timed_out = False
    try:
        stdout, stderr = process.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        timed_out = True
        if os.name == 'nt':
            # This invocation owns process.pid; no name-based/global cancellation.
            killed = subprocess.run([taskkill, '/PID', str(process.pid), '/T', '/F'],
                stdout=subprocess.PIPE, stderr=subprocess.PIPE, timeout=15)
            require(killed.returncode == 0 or process.poll() is not None, 'owned taskkill failed')
        else:
            os.killpg(process.pid, signal.SIGKILL)
        stdout, stderr = process.communicate(timeout=15)
    return {'pid': process.pid, 'returncode': process.returncode, 'timed_out': timed_out}, stdout, stderr


def build(config_path, expected_sha, out):
    require(sha(config_path) == expected_sha, 'request digest')
    c = load(config_path)
    require(os.name == 'nt', 'Windows build only')
    require(not out.exists(), 'fresh output directory required')
    pins, profiles = check_inputs(c)
    require(key(c['taskkill']) in pins, 'taskkill must be pinned')
    out.mkdir(parents=True)
    result = {'status': 'STOP', 'request_sha256': expected_sha,
        'source_commit': load(c['source_manifest'])['repository_commit'],
        'source_manifest_sha256': sha(c['source_manifest']),
        'native_result_sha256': sha(c['native_result']), 'objects': []}
    try:
        env = dict(os.environ)
        for name in c['clear_environment']:
            env.pop(name, None)
        # Environment is explicit authority from the reviewed profiles, not caller CL.
        for name, value in profiles['environment'].items():
            if name == 'PATH_PREFIX':
                env['PATH'] = value + ';' + env.get('PATH', '')
            else:
                env[name] = value
        (out / 'environment.json').write_text(json.dumps({k: env.get(k) for k in ('CL', '_CL_', 'INCLUDE', 'LIB', 'PATH')}, indent=2))
        for unit in profiles['commands']:
            check_inputs(c)
            argv = arguments(c, unit, out)
            require(key(argv[0]) in pins, 'compiler pin')
            label = unit['label']
            (out / (label + '.argv.json')).write_text(json.dumps(argv, indent=2))
            began = time.monotonic()
            state, stdout, stderr = owned_run(argv, env, out, unit['timeout_seconds'], c['taskkill'])
            stdout_path = out / (label + '.stdout.bin'); stdout_path.write_bytes(stdout)
            stderr_path = out / (label + '.stderr.bin'); stderr_path.write_bytes(stderr)
            log = dict(state, argv=argv, seconds=time.monotonic()-began,
                       stdout=record(stdout_path), stderr=record(stderr_path))
            (out / (label + '.command.json')).write_text(json.dumps(log, indent=2))
            require(not state['timed_out'] and state['returncode'] == 0, 'compile failed: ' + label)
            deps = check_closure(c, unit, argv, stdout, stderr, pins)
            obj = out / (label + '.obj'); coff(obj)
            result['objects'].append(dict(record(obj), label=label, argv=argv,
                returncode=state['returncode'], stdout=record(stdout_path),
                stderr=record(stderr_path), dependencies=deps))
        check_inputs(c)
        result['status'] = 'EIGHT_OBJECTS_COMPLETE'
    except BaseException as error:
        result['error'] = {'type': type(error).__name__, 'repr': repr(error)}
        raise
    finally:
        (out / 'result.json').write_text(json.dumps(result, indent=2)+'\n')
    ns = 'http://schemas.microsoft.com/developer/msbuild/2003'
    ET.register_namespace('', ns)
    tag = lambda s: '{'+ns+'}'+s
    root = ET.Element(tag('Project'))
    group = ET.SubElement(root, tag('PropertyGroup'))
    for name, value in {'PreparedNirOwnerReceiptSha256': sha(out/'result.json'),
                        'PreparedNirOwnerRequestSha256': expected_sha}.items():
        ET.SubElement(group, tag(name)).text = value
    objects = ET.SubElement(root, tag('ItemGroup'))
    for obj in result['objects']:
        ET.SubElement(objects, tag('Object'), Include=obj['path'])
    link = ET.SubElement(ET.SubElement(root, tag('ItemDefinitionGroup')), tag('Link'))
    libraries = [a['path'] for a in c['archives'] if a['group'] in ('core_extra', 'checked')]
    ET.SubElement(link, tag('AdditionalDependencies')).text = ';'.join(libraries)+';%(AdditionalDependencies)'
    props_path = out / 'PreparedNirOwner.props'
    ET.ElementTree(root).write(props_path, encoding='utf-8', xml_declaration=True)
    verify_receipt(config_path, expected_sha, out/'result.json', props_path)
    print('PREPARED_NIR_OWNER_EIGHT_OBJECT_BUILD_PASS')


def main():
    p = argparse.ArgumentParser()
    p.add_argument('--request', type=Path, required=True)
    p.add_argument('--request-sha256', required=True)
    p.add_argument('--out', type=Path, required=True)
    a = p.parse_args()
    build(a.request, a.request_sha256, a.out.resolve())

if __name__ == '__main__':
    main()
