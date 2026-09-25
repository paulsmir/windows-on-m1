#!/bin/sh
set -eu
cd /Users/pavel/public_windows
if [ "${AIR_SERIAL_LOCK_ACTIVE:-}" != 1 ]; then exec python3 /Users/pavel/public_windows/.worktrees/integration-ad04-windows-compiler/scripts/air-serial-guard.py --port /dev/cu.usbmodemC02HDNCCQ6L41 --port /dev/cu.usbmodemC02HDNCCQ6L43 -- "$0" "$@"; fi
export LLDDIR=/tmp/agx-lld-dir/ M1N1DEVICE=/dev/cu.usbmodemC02HDNCCQ6L41 PYTHONUNBUFFERED=1
unset WOM1_AGX_G2_POWER_BROKER
python3 /Users/pavel/public_windows/.worktrees/integration-ad04-windows-compiler/scripts/g3-launchers/soc-reset.py --port /dev/cu.usbmodemC02HDNCCQ6L41 --port /dev/cu.usbmodemC02HDNCCQ6L43 -- ./proxyenv/bin/python m1n1_windows/proxyclient/tools/reboot.py
./proxyenv/bin/python m1n1_windows/proxyclient/tools/chainload.py .local/experiments/EXP-20260903-377-secondary-cpu-receipt/assisted-boot/m1n1.macho
exec ./proxyenv/bin/python -u run_uefi.py .local/experiments/EXP-20260903-385-hvc-single-page/recovery/J313_EFI-no-agx-autoboot.fd --device "$M1N1DEVICE" --display-mode physical --debug-mode off --low-mem --contract-output .local/experiments/EXP784-g3-parent-size/emergency-contract.bin
