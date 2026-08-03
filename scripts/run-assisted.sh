#!/bin/sh
# Start the host-assisted Mu/Windows guest with early virtual-UART capture.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PROXY=${M1N1DEVICE:-}
VUART=${M1N1VUART:-}
FIRMWARE="$ROOT/dist/j313/J313_EFI.fd"
RAMDISK=
DRY_RUN=0
LOW_MEM=1

usage() {
    echo "usage: $0 [--proxy DEVICE] [--vuart DEVICE] [--firmware FILE]" >&2
    echo "          [--ramdisk FILE] [--no-low-mem] [--dry-run]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --proxy) [ "$#" -ge 2 ] || usage; PROXY=$2; shift 2 ;;
        --vuart) [ "$#" -ge 2 ] || usage; VUART=$2; shift 2 ;;
        --firmware) [ "$#" -ge 2 ] || usage; FIRMWARE=$2; shift 2 ;;
        --ramdisk) [ "$#" -ge 2 ] || usage; RAMDISK=$2; shift 2 ;;
        --no-low-mem) LOW_MEM=0; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done

discover_ports() {
    [ -n "$PROXY" ] && [ -n "$VUART" ] && return
    set -- /dev/cu.usbmodem*
    if [ "$1" = '/dev/cu.usbmodem*' ] || [ "$#" -ne 2 ]; then
        echo "Unable to select proxy/vUART automatically." >&2
        echo "Connect the Air, list /dev/cu.usbmodem*, then pass --proxy and --vuart." >&2
        exit 1
    fi
    [ -n "$PROXY" ] || PROXY=$1
    [ -n "$VUART" ] || VUART=$2
}

if [ "$DRY_RUN" -eq 0 ]; then
    discover_ports
else
    [ -n "$PROXY" ] || PROXY='<proxy-device>'
    [ -n "$VUART" ] || VUART='<vuart-device>'
fi

PYTHON="$ROOT/proxyenv/bin/python"
[ -x "$PYTHON" ] || PYTHON=python3

if [ "$DRY_RUN" -eq 1 ]; then
    echo "mode: assisted development"
    echo "ordering: reader-before-guest"
    echo "proxy: $PROXY"
    echo "virtual UART: $VUART"
    echo "firmware: $FIRMWARE"
    [ -z "$RAMDISK" ] || echo "RAM disk: $RAMDISK"
    echo "logs: $ROOT/hv.log and $ROOT/guest-uart.log"
    exit 0
fi

[ -f "$FIRMWARE" ] || { echo "Firmware not found: $FIRMWARE" >&2; exit 1; }
[ -z "$RAMDISK" ] || [ -f "$RAMDISK" ] || {
    echo "RAM disk not found: $RAMDISK" >&2
    exit 1
}

if pgrep -f '[r]un_uefi.py' >/dev/null; then
    echo "A guest runner already owns the proxy. Use scripts/reset-assisted.sh." >&2
    exit 1
fi

cd "$ROOT"
rm -f guest-uart.log guest-uart.tlog guest-uart-reader.log hv.log guest.pid

# The reader must hold the virtual UART open before Mu emits its first byte.
"$PYTHON" -u "$ROOT/extra/uart-reader.py" "$VUART" 2400 \
    2>guest-uart-reader.log &
READER=$!
sleep 2

set -- "$FIRMWARE" --device "$PROXY"
[ -z "$RAMDISK" ] || set -- "$@" --ramdisk "$RAMDISK"
[ "$LOW_MEM" -eq 0 ] || set -- "$@" --low-mem

PYTHONUNBUFFERED=1 M1N1DEVICE="$PROXY" \
    "$PYTHON" -u "$ROOT/run_uefi.py" "$@" >hv.log 2>&1 &
RUNNER=$!
echo "$RUNNER" >guest.pid

echo "reader=$READER runner=$RUNNER"
echo "hypervisor log: $ROOT/hv.log"
echo "guest UART log: $ROOT/guest-uart.log"
