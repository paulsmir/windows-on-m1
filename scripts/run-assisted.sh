#!/bin/sh
# Start the host-assisted Mu/Windows guest with early virtual-UART capture.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PROXY=${M1N1DEVICE:-}
VUART=${M1N1VUART:-}
FIRMWARE=
RAMDISK=
DRY_RUN=0
LOW_MEM=1
DISPLAY=virtual
DEBUG=uart
CHAINLOAD=0
M1N1=
CONTRACT_OUTPUT=

usage() {
    echo "usage: $0 [--proxy DEVICE] [--vuart DEVICE] [--firmware FILE]" >&2
    echo "          [--display none|physical|virtual|both] [--debug off|uart|full]" >&2
    echo "          [--ramdisk FILE] [--chainload] [--m1n1 FILE]" >&2
    echo "          [--contract-output FILE]" >&2
    echo "          [--no-low-mem] [--dry-run]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --proxy) [ "$#" -ge 2 ] || usage; PROXY=$2; shift 2 ;;
        --vuart) [ "$#" -ge 2 ] || usage; VUART=$2; shift 2 ;;
        --firmware) [ "$#" -ge 2 ] || usage; FIRMWARE=$2; shift 2 ;;
        --display) [ "$#" -ge 2 ] || usage; DISPLAY=$2; shift 2 ;;
        --debug) [ "$#" -ge 2 ] || usage; DEBUG=$2; shift 2 ;;
        --ramdisk) [ "$#" -ge 2 ] || usage; RAMDISK=$2; shift 2 ;;
        --chainload) CHAINLOAD=1; shift ;;
        --m1n1) [ "$#" -ge 2 ] || usage; M1N1=$2; shift 2 ;;
        --contract-output) [ "$#" -ge 2 ] || usage; CONTRACT_OUTPUT=$2; shift 2 ;;
        --no-low-mem) LOW_MEM=0; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done

case "$DISPLAY" in none|physical|virtual|both) ;; *) usage ;; esac
case "$DEBUG" in off|uart|full) ;; *) usage ;; esac

PROFILE=debug
[ "$DEBUG" != off ] || PROFILE=release
[ -n "$FIRMWARE" ] || FIRMWARE="$ROOT/dist/j313/$PROFILE/J313_EFI.fd"
[ -n "$M1N1" ] || M1N1="$ROOT/dist/j313/$PROFILE/m1n1.macho"

discover_ports() {
    [ -n "$PROXY" ] && { [ "$DEBUG" = off ] || [ -n "$VUART" ]; } && return
    set -- /dev/cu.usbmodem*
    if [ "$1" = '/dev/cu.usbmodem*' ] || [ "$#" -ne 2 ]; then
        echo "Unable to select proxy/vUART automatically." >&2
        echo "Connect the Air, list /dev/cu.usbmodem*, then pass --proxy and --vuart." >&2
        exit 1
    fi
    [ -n "$PROXY" ] || PROXY=$1
    if [ "$DEBUG" != off ]; then
        [ -n "$VUART" ] || VUART=$2
    fi
}

if [ "$DRY_RUN" -eq 0 ]; then
    discover_ports
else
    [ -n "$PROXY" ] || PROXY='<proxy-device>'
    if [ "$DEBUG" != off ]; then
        [ -n "$VUART" ] || VUART='<vuart-device>'
    fi
fi

PYTHON="$ROOT/proxyenv/bin/python"
[ -x "$PYTHON" ] || PYTHON=python3

if [ "$DRY_RUN" -eq 1 ]; then
    echo "mode: assisted development"
    echo "display: $DISPLAY"
    echo "debug: $DEBUG"
    echo "proxy: $PROXY"
    if [ "$DEBUG" = off ]; then
        echo "virtual UART: disabled"
    else
        echo "ordering: reader-before-guest"
        echo "virtual UART: $VUART"
    fi
    case "$DISPLAY" in virtual|both) echo "USB framebuffer: enabled" ;; *) echo "USB framebuffer: disabled" ;; esac
    [ "$DEBUG" = full ] && echo "telemetry: enabled" || echo "telemetry: disabled"
    [ "$CHAINLOAD" -eq 0 ] && echo "chainload: disabled" || echo "chainload: $M1N1"
    echo "firmware: $FIRMWARE"
    [ -z "$RAMDISK" ] || echo "RAM disk: $RAMDISK"
    [ -z "$CONTRACT_OUTPUT" ] || echo "launch contract: $CONTRACT_OUTPUT"
    [ "$DEBUG" = off ] || echo "logs: $ROOT/hv.log and $ROOT/guest-uart.log"
    exit 0
fi

[ -f "$FIRMWARE" ] || { echo "Firmware not found: $FIRMWARE" >&2; exit 1; }
[ "$CHAINLOAD" -eq 0 ] || [ -f "$M1N1" ] || {
    echo "m1n1 image not found: $M1N1" >&2
    exit 1
}
[ -z "$RAMDISK" ] || [ -f "$RAMDISK" ] || {
    echo "RAM disk not found: $RAMDISK" >&2
    exit 1
}

MANIFEST=$(dirname "$FIRMWARE")/MANIFEST.json
[ -f "$MANIFEST" ] || { echo "Artifact manifest not found: $MANIFEST" >&2; exit 1; }
"$PYTHON" "$ROOT/tools/artifact_manifest.py" verify "$MANIFEST" --profile "$PROFILE"
if [ "$CHAINLOAD" -eq 1 ] && [ "$(dirname "$M1N1")" != "$(dirname "$FIRMWARE")" ]; then
    echo "m1n1 and Mu must come from the same artifact profile directory" >&2
    exit 1
fi

if pgrep -f '[r]un_uefi.py' >/dev/null; then
    echo "A guest runner already owns the proxy. Use scripts/reset-assisted.sh." >&2
    exit 1
fi

cd "$ROOT"
rm -f guest-uart.log guest-uart.tlog guest-uart-reader.log hv.log guest.pid

if [ "$CHAINLOAD" -eq 1 ]; then
    echo "Chainloading matching m1n1: $M1N1"
    M1N1DEVICE="$PROXY" "$PYTHON" \
        "$ROOT/m1n1_windows/proxyclient/tools/chainload.py" "$M1N1"
fi

READER=
if [ "$DEBUG" != off ]; then
    # The reader must hold the virtual UART open before Mu emits its first byte.
    nohup "$PYTHON" -u "$ROOT/extra/uart-reader.py" "$VUART" 2400 \
        </dev/null >guest-uart-reader.log 2>&1 &
    READER=$!
    sleep 2
fi

set -- "$FIRMWARE" --device "$PROXY" --display-mode "$DISPLAY" --debug-mode "$DEBUG"
[ -z "$RAMDISK" ] || set -- "$@" --ramdisk "$RAMDISK"
[ -z "$CONTRACT_OUTPUT" ] || set -- "$@" --contract-output "$CONTRACT_OUTPUT"
[ "$LOW_MEM" -eq 0 ] || set -- "$@" --low-mem

if [ "$DEBUG" = off ]; then
    PYTHONUNBUFFERED=1 M1N1DEVICE="$PROXY" \
        nohup "$PYTHON" -u "$ROOT/run_uefi.py" "$@" </dev/null >/dev/null 2>&1 &
else
    PYTHONUNBUFFERED=1 M1N1DEVICE="$PROXY" \
        nohup "$PYTHON" -u "$ROOT/run_uefi.py" "$@" </dev/null >hv.log 2>&1 &
fi
RUNNER=$!
echo "$RUNNER" >guest.pid

[ -z "$READER" ] && echo "runner=$RUNNER" || echo "reader=$READER runner=$RUNNER"
if [ "$DEBUG" != off ]; then
    echo "hypervisor log: $ROOT/hv.log"
    echo "guest UART log: $ROOT/guest-uart.log"
fi
