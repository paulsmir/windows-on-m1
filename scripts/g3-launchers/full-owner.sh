#!/bin/bash
set -euo pipefail
cd /Users/pavel/public_windows
phase=${1:?supply-log-phase}
python3 - <<'PY_CHECK'
import hashlib,json
from pathlib import Path
root=Path('.local/experiments/EXP784-g3-parent-size')
manifest=json.loads((root/'hardware-manifest.json').read_text())
info=json.loads((root/'firmware/m1n1-build-info.json').read_text())
def sha(p):return hashlib.sha256(p.read_bytes()).hexdigest()
if info.get('profile')!='J313_GPUVA_FULL_OWNER' or info.get('make_flags',{}).get('IOMFB_FULL_OWNER')!=1:
    raise SystemExit('full-owner m1n1 build profile missing')
if info.get('m1n1_commit')!=manifest['m1n1_commit']:
    raise SystemExit('m1n1 commit mismatch')
for name in ('firmware/m1n1-build-info.json','firmware/m1n1-g2-abi6-iomfb-owner.macho'):
    if sha(root/name)!=manifest['artifact_sha256'][name]:
        raise SystemExit('frozen artifact mismatch: '+name)
if info.get('macho_sha256')!=manifest['artifact_sha256']['firmware/m1n1-g2-abi6-iomfb-owner.macho']:
    raise SystemExit('m1n1 build-info image mismatch')
PY_CHECK
if [ "$phase" = "--preflight-only" ]; then exit 0; fi
if [ "${AIR_SERIAL_LOCK_ACTIVE:-}" != 1 ]; then exec python3 /Users/pavel/public_windows/.worktrees/integration-ad04-windows-compiler/scripts/air-serial-guard.py --port /dev/cu.usbmodemC02HDNCCQ6L41 --port /dev/cu.usbmodemC02HDNCCQ6L43 -- "$0" "$@"; fi
export LLDDIR=/tmp/agx-lld-dir/ M1N1DEVICE=/dev/cu.usbmodemC02HDNCCQ6L41 PYTHONUNBUFFERED=1
python3 /Users/pavel/public_windows/.worktrees/integration-ad04-windows-compiler/scripts/g3-launchers/soc-reset.py --port /dev/cu.usbmodemC02HDNCCQ6L41 --port /dev/cu.usbmodemC02HDNCCQ6L43 -- proxyenv/bin/python m1n1_windows/proxyclient/tools/reboot.py 2>&1 | tee ".local/experiments/EXP784-g3-parent-size/${phase}-soc-reset.log"
proxyenv/bin/python m1n1_windows/proxyclient/tools/chainload.py .local/experiments/EXP784-g3-parent-size/firmware/m1n1-g2-abi6-iomfb-owner.macho 2>&1 | tee ".local/experiments/EXP784-g3-parent-size/${phase}-chainload.log"
env WOM1_ALLOW_LEGACY_LAUNCH_CONTRACT=1 WOM1_AGX_G2_POWER_BROKER=1 proxyenv/bin/python -u run_uefi.py .local/experiments/EXP784-g3-parent-size/firmware/J313_EFI-exp406.fd --device "$M1N1DEVICE" --display-mode physical --debug-mode off --low-mem --contract-output ".local/experiments/EXP784-g3-parent-size/${phase}-contract.bin" 2>&1 | tee ".local/experiments/EXP784-g3-parent-size/${phase}-full.log"
