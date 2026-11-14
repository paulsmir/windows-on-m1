#!/bin/sh
set -eu
. "$(dirname "$0")/../lib/bench-env.sh"
cd "$AGX_BENCH_ROOT"
if [ "${AIR_SERIAL_LOCK_ACTIVE:-}" != 1 ]; then exec python3 "$AGX_INTEGRATION"/scripts/air-serial-guard.py --port "$AGX_PROXY_PORT_A" --port "$AGX_PROXY_PORT_B" -- "$0" "$@"; fi
export LLDDIR=/tmp/agx-lld-dir/ M1N1DEVICE="$AGX_PROXY_PORT_A" PYTHONUNBUFFERED=1
unset WOM1_AGX_G2_POWER_BROKER
python3 "$AGX_INTEGRATION"/scripts/g3-launchers/soc-reset.py --emergency-recovery --port "$AGX_PROXY_PORT_A" --port "$AGX_PROXY_PORT_B" -- ./proxyenv/bin/python m1n1_windows/proxyclient/tools/reboot.py
./proxyenv/bin/python m1n1_windows/proxyclient/tools/chainload.py .local/experiments/EXP-20260903-377-secondary-cpu-receipt/assisted-boot/m1n1.macho
exec ./proxyenv/bin/python -u run_uefi.py .local/experiments/EXP-20260903-385-hvc-single-page/recovery/J313_EFI-no-agx-autoboot.fd --device "$M1N1DEVICE" --display-mode physical --debug-mode off --low-mem --contract-output .local/experiments/EXP784-g3-parent-size/emergency-contract.bin
