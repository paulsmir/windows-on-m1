#!/bin/bash
set -euo pipefail
cd /Users/pavel/public_windows
phase=${1:?supply-log-phase}
python3 - <<'PY_CHECK'
import hashlib, json
from pathlib import Path

root = Path('.local/experiments/EXP801-g3-shared-reserve')
manifest = json.loads((root / 'hardware-manifest-r60.json').read_text())
info = json.loads((root / 'firmware/m1n1-build-info.json').read_text())
def sha(path): return hashlib.sha256(path.read_bytes()).hexdigest()
if manifest['profile'] != 'G1b16 WDDM3.2 GPUVA full-owner R60':
    raise SystemExit('wrong hardware profile')
if info.get('profile') != 'J313_GPUVA_FULL_OWNER' or info.get('make_flags', {}).get('IOMFB_FULL_OWNER') != 1:
    raise SystemExit('full-owner m1n1 profile missing')
if info.get('m1n1_commit') != manifest['m1n1_commit'] or info.get('macho_sha256') != manifest['m1n1_macho_sha256']:
    raise SystemExit('m1n1 build identity mismatch')
for path, expected in (
    ('firmware/m1n1-build-info.json', manifest['m1n1_build_info_sha256']),
    ('firmware/m1n1-g3-arm-consumed.macho', manifest['m1n1_macho_sha256']),
    ('firmware/J313_EFI-exp406.fd', manifest['mu_sha256']),
    ('source-manifest.json', manifest['source_manifest_sha256']),
    ('package-manifest.json', manifest['package_manifest_sha256']),
):
    if sha(root / path) != expected:
        raise SystemExit('artifact mismatch: ' + path)
for name, expected in manifest['artifact_sha256'].items():
    if sha(root / 'package' / name) != expected:
        raise SystemExit('package mismatch: ' + name)
PY_CHECK
if [ "$phase" = "--preflight-only" ]; then exit 0; fi
if [ "${AIR_SERIAL_LOCK_ACTIVE:-}" != 1 ]; then exec python3 /Users/pavel/public_windows/.worktrees/integration-ad04-windows-compiler/scripts/air-serial-guard.py --port /dev/cu.usbmodemC02HDNCCQ6L41 --port /dev/cu.usbmodemC02HDNCCQ6L43 -- "$0" "$@"; fi
export LLDDIR=/tmp/agx-lld-dir/ M1N1DEVICE=/dev/cu.usbmodemC02HDNCCQ6L41 PYTHONUNBUFFERED=1
python3 /Users/pavel/public_windows/.worktrees/integration-ad04-windows-compiler/scripts/g3-launchers/soc-reset.py --port /dev/cu.usbmodemC02HDNCCQ6L41 --port /dev/cu.usbmodemC02HDNCCQ6L43 -- proxyenv/bin/python m1n1_windows/proxyclient/tools/reboot.py 2>&1 | tee ".local/experiments/EXP801-g3-shared-reserve/${phase}-soc-reset.log"
proxyenv/bin/python m1n1_windows/proxyclient/tools/chainload.py .local/experiments/EXP801-g3-shared-reserve/firmware/m1n1-g3-arm-consumed.macho 2>&1 | tee ".local/experiments/EXP801-g3-shared-reserve/${phase}-chainload.log"
env WOM1_ALLOW_LEGACY_LAUNCH_CONTRACT=1 WOM1_AGX_G2_POWER_BROKER=1 proxyenv/bin/python -u run_uefi.py .local/experiments/EXP801-g3-shared-reserve/firmware/J313_EFI-exp406.fd --device "$M1N1DEVICE" --display-mode physical --debug-mode off --low-mem --contract-output ".local/experiments/EXP801-g3-shared-reserve/${phase}-contract.bin" 2>&1 | tee ".local/experiments/EXP801-g3-shared-reserve/${phase}-full.log"
