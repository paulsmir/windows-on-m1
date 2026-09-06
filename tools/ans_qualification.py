"""Hardware-free ANS001 candidate admission. A manifest is evidence, not permission."""
from __future__ import annotations
import argparse
import hashlib
import json
import os
from pathlib import Path
import re
import subprocess

SCHEMA = 'apple-ans-qualification/v1'
ROLES = ('m1n1', 'mu', 'winpe', 'layout', 'broker_config', 'broker_abi',
         'm1n1_build', 'mu_build', 'winpe_audit', 'winpe_collector', 'qualification_audit')
CANONICAL = {'layout': 'config/j313-guest-layout.json',
             'broker_config': 'config/j313-ans.json', 'broker_abi': 'config/apple-ans-broker-abi.json'}
PINNED = {'display': 'physical', 'debug': 'uart', 'low_mem': True, 'cpu_count': 8,
          'no_pin': False, 'wdt_cpu': None, 'agx_power_broker': False,
          'chainload': True, 'boot_source': 'preloaded-ramdisk'}
OVERRIDES = ('phys_base', 'ramdisk_base', 'ramdisk_max_size', 'low_mem_base',
             'low_mem_size', 'low_mem_backing', 'fb_base', 'fb_width', 'fb_height', 'guest_ram_end')


def digest(path):
    h = hashlib.sha256()
    with Path(path).open('rb') as f:
        for block in iter(lambda: f.read(1024 * 1024), b''):
            h.update(block)
    return h.hexdigest()


def require(condition, message):
    if not condition:
        raise ValueError(message)


def read_json(path):
    value = json.loads(Path(path).read_text())
    require(isinstance(value, dict), f'JSON object required: {path}')
    return value


def validate_manifest(path, expected_sha256, root):
    root = Path(root).resolve()
    path = Path(path).resolve()
    require(path.is_relative_to(root), 'qualification manifest must be inside isolated root')
    require(isinstance(expected_sha256, str) and re.fullmatch('[0-9a-f]{64}', expected_sha256),
            'explicit --qualification-sha256 is required')
    require(digest(path) == expected_sha256, 'qualification manifest SHA-256 mismatch')
    m = read_json(path)
    require(set(m) == {'schema', 'experiment', 'platform', 'storage_mode', 'scope', 'read_only', 'launch', 'artifacts'}, 'unknown/missing qualification fields')
    require((m['schema'], m['experiment'], m['platform'], m['storage_mode'], m['scope']) ==
            (SCHEMA, 'ANS001', 'J313', 'ANS_NATIVE', 'ans0-unbound-no-driver'), 'wrong qualification profile')
    require(m['read_only'] is True and json.dumps(m['launch'], sort_keys=True) == json.dumps(PINNED, sort_keys=True), 'qualification launch contract mismatch')
    require(set(m['artifacts']) == set(ROLES), 'all exact artifact/evidence roles required')
    paths, hashes = {}, {}
    for role in ROLES:
        item = m['artifacts'][role]
        require(isinstance(item, dict) and set(item) == {'path', 'sha256'}, f'bad artifact record: {role}')
        require(isinstance(item['path'], str) and item['path'], f'bad artifact path: {role}')
        p = (root / item['path']).resolve()
        require(p.is_relative_to(root) and p.is_file(), f'missing/outside artifact: {role}')
        require(isinstance(item['sha256'], str) and re.fullmatch('[0-9a-f]{64}', item['sha256']), f'pending/invalid hash: {role}')
        require(digest(p) == item['sha256'], f'artifact SHA-256 mismatch: {role}')
        if role in CANONICAL:
            require(p == (root / CANONICAL[role]).resolve(), f'noncanonical {role}')
        paths[role], hashes[role] = p, item['sha256']
    config = read_json(paths['broker_config'])
    require(config.get('native_launch_enabled') is False and config.get('qualification_read_only') is True,
            'general native launch must remain disabled and read-only')
    require(config.get('default_mode') == 'VNVME_RECOVERY' and config.get('acpi_hid') == 'APPL0003', 'broker profile mismatch')
    audit = read_json(paths['winpe_audit'])
    require(audit.get('schema') == 'apple-ans-winpe-audit/v1' and audit.get('result') == 'pass' and
            audit.get('image_sha256') == hashes['winpe'] and audit.get('architecture') == 'ARM64' and
            audit.get('boot_index') == 1 and audit.get('boot_source') == 'preloaded-ramdisk' and
            audit.get('installed_windows_boot') is False, 'WinPE audit not bound/complete')
    indices = audit.get('indices')
    require(isinstance(indices, list) and len(indices) == 2 and
            {x.get('index') for x in indices if isinstance(x, dict)} == {1, 2}, 'both WinPE indices must be audited')
    for index in indices:
        require(all(index.get(field) == [] for field in ('appleans_packages', 'appleans_services', 'appleans_drivers')),
                'AppleAns package/service/driver present or audit incomplete')
    audit = read_json(paths['qualification_audit'])
    require(audit.get('schema') == 'apple-ans-qualification-audit/v1' and audit.get('result') == 'pass', 'qualification audit pending/failed')
    require(audit.get('bindings') == {r: hashes[r] for r in ROLES if r != 'qualification_audit'}, 'audit artifact/receipt bindings mismatch')
    for key in ('offline_gates_passed', 'mu_ramdisk_only', 'mu_native_gate_bound', 'mu_native_autoboot_bound',
                'agx_power_broker_disabled', 'winpe_no_appleans', 'no_installed_windows_boot', 'collector_test_passed'):
        require(audit.get(key) is True, f'unproven qualification property: {key}')
    collector = read_json(paths['winpe_collector'])
    require(collector.get('schema') == 'apple-ans-winpe-collector-test/v1' and
            collector.get('result') == 'pass' and collector.get('image_sha256') == hashes['winpe'] and
            collector.get('hardware_access') is False, 'exact WinPE collector software test incomplete')
    # Validate the actual build-receipt formats, including the native-only Mu
    # bindings. A failed or recovery build with a copied hash is not evidence.
    backend = read_json(paths['m1n1_build'])
    require(type(backend.get('exit_code')) is int and backend.get('exit_code') == 0 and backend.get('frozen_inputs_unchanged') is True and
            backend.get('artifacts', {}).get('m1n1.macho') == hashes['m1n1'] and
            isinstance(backend.get('build_tag'), str) and re.fullmatch('[0-9a-f]{7,40}', backend['build_tag']),
            'm1n1 build receipt not successful/frozen/bound')
    firmware = read_json(paths['mu_build'])
    builds = [x for x in firmware.get('builds', []) if isinstance(x, dict) and x.get('profile') == 'native']
    require(len(builds) == 1, 'exactly one native Mu build receipt required')
    build = builds[0]
    require(type(build.get('exit_code')) is int and build.get('exit_code') == 0 and build.get('unchanged_source_files', 0) > 0 and
            build.get('expected_core_gate') == 'AppleAnsExitBootServicesGateLib' and
            build.get('expected_autoboot') == 'WindowsAutoBootAnsNative' and
            build.get('native_source_guard_cache_symbol') is True and
            build.get('g2_inert_no_operationregion_or_power_methods') is True and
            build.get('acpi_ffs_hid_counts') == {'APPL0002': 1, 'APPL0003': 1} and
            any(x.get('sha256') == hashes['mu'] and str(x.get('path', '')).lower().endswith('.fd')
                for x in build.get('artifacts', []) if isinstance(x, dict)),
            'Mu native/source-guard build receipt not successful/bound')
    return m, paths


def validate_launch(path, seal, root, *, firmware=None, m1n1=None, ramdisk=None,
                    layout=None, display='physical', debug='uart', low_mem=True,
                    no_pin=False, wdt_cpu=None, agx_power_broker=False, overrides=(),
                    confirmation=None, execute=False, environment=None, contract_output=None):
    require(path is not None, "native qualification candidate is not yet admitted: --qualification-manifest required")
    env = os.environ if environment is None else environment
    for key, value in env.items():
        if key.startswith('WOM1_') and key != 'WOM1_STORAGE_MODE':
            require(key == 'WOM1_AGX_G2_POWER_BROKER' and value == '0', f'inherited native launch override forbidden: {key}')
    require(not overrides and not no_pin and wdt_cpu is None and not agx_power_broker,
            'native qualification forbids layout/no-pin/watchdog/AGX overrides')
    require(display == 'physical' and debug == 'uart' and low_mem is True, 'ANS001 requires physical/uart/low-memory profile')
    require(not execute or confirmation == 'ANS001', 'execution requires explicit --confirm-experiment ANS001')
    require(confirmation in (None, 'ANS001'), 'wrong experiment confirmation')
    require(path is not None, 'native qualification candidate is not yet admitted: --qualification-manifest required')
    m, paths = validate_manifest(path, seal, root)
    for role, selected in (('mu', firmware), ('m1n1', m1n1), ('winpe', ramdisk), ('layout', layout)):
        require(selected is None or Path(selected).resolve() == paths[role], f'CLI {role} differs from sealed candidate')
    reject_output_aliases(root, paths, path, contract_output)
    return m, paths


def reject_output_aliases(root, paths, manifest, contract_output=None):
    inputs=[*paths.values(),Path(manifest).resolve()]
    outputs=[Path(root)/n for n in ('assisted-runner.log','guest-uart.log','guest-uart.tlog',
              'guest-uart-reader.log','hv.log','guest.pid')]
    if contract_output is not None:outputs.append(Path(contract_output))
    for output in outputs:
        for source in inputs:
            alias=output.resolve()==source.resolve()
            if output.exists() and source.exists():alias=alias or os.path.samefile(output,source)
            require(not alias, f'launch output aliases sealed input: {output}')
        require(output.resolve().is_relative_to(Path(root).resolve()),
                f'native output must remain inside isolated root: {output}')


def reject_running_launcher():
    from tools.ans_launch_lock import reject_legacy
    reject_legacy(Path(__file__).resolve().parents[1])


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument('--manifest', type=Path, required=True)
    ap.add_argument('--sha256', required=True)
    ap.add_argument('--root', type=Path, default=Path(__file__).resolve().parents[1])
    a = ap.parse_args()
    m, p = validate_manifest(a.manifest, a.sha256, a.root)
    print(json.dumps({'experiment': m['experiment'], 'hardware_access': False,
                      'artifacts': {k: str(v) for k, v in p.items()}}, indent=2))

if __name__ == '__main__':
    main()
