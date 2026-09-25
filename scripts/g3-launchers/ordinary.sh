#!/bin/bash
set -euo pipefail
cd /Users/pavel/public_windows
phase=${1:?supply-log-phase}
if [ "${AIR_SERIAL_LOCK_ACTIVE:-}" != 1 ]; then exec python3 /Users/pavel/public_windows/.worktrees/integration-ad04-windows-compiler/scripts/air-serial-guard.py --port /dev/cu.usbmodemC02HDNCCQ6L41 --port /dev/cu.usbmodemC02HDNCCQ6L43 -- "$0" "$@"; fi
export LLDDIR=/tmp/agx-lld-dir/ M1N1DEVICE=/dev/cu.usbmodemC02HDNCCQ6L41 PYTHONUNBUFFERED=1
python3 /Users/pavel/public_windows/.worktrees/integration-ad04-windows-compiler/scripts/g3-launchers/soc-reset.py --port /dev/cu.usbmodemC02HDNCCQ6L41 --port /dev/cu.usbmodemC02HDNCCQ6L43 -- proxyenv/bin/python m1n1_windows/proxyclient/tools/reboot.py 2>&1 | tee ".local/experiments/EXP784-g3-parent-size/${phase}-soc-reset.log"
proxyenv/bin/python m1n1_windows/proxyclient/tools/chainload.py .local/experiments/EXP784-g3-parent-size/recovery/m1n1-exp377.macho 2>&1 | tee ".local/experiments/EXP784-g3-parent-size/${phase}-chainload.log"
env -u WOM1_AGX_G2_POWER_BROKER WOM1_ALLOW_LEGACY_LAUNCH_CONTRACT=1 proxyenv/bin/python -u run_uefi.py .local/experiments/EXP784-g3-parent-size/recovery/J313_EFI-exp392.fd --device "$M1N1DEVICE" --display-mode physical --debug-mode off --low-mem --contract-output ".local/experiments/EXP784-g3-parent-size/${phase}-contract.bin" 2>&1 | tee ".local/experiments/EXP784-g3-parent-size/${phase}-full.log"
