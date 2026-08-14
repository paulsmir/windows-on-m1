#!/bin/sh
# Resolve a public Windows launch profile and dispatch the selected execution path.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
EXECUTION=standalone
DISPLAY=physical
DISPLAY_SET=0
DEBUG=off
PROXY=
VUART=
FIRMWARE=
RAMDISK=
CHAINLOAD=0
REUSE_PROXY=0
M1N1=
DRY_RUN=0
FOREGROUND=0
OBSERVED=0

usage() {
    echo "usage: $0 [--execution standalone|assisted]" >&2
    echo "          [--display none|physical|virtual|both] [--debug off|uart|full|monitor]" >&2
    echo "          [--proxy DEVICE] [--vuart DEVICE] [--firmware FILE]" >&2
    echo "          [--ramdisk FILE] [--chainload|--reuse-proxy]" >&2
    echo "          [--m1n1 FILE] [--foreground] [--observed] [--dry-run]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --execution) [ "$#" -ge 2 ] || usage; EXECUTION=$2; shift 2 ;;
        --display) [ "$#" -ge 2 ] || usage; DISPLAY=$2; DISPLAY_SET=1; shift 2 ;;
        --debug) [ "$#" -ge 2 ] || usage; DEBUG=$2; shift 2 ;;
        --proxy) [ "$#" -ge 2 ] || usage; PROXY=$2; shift 2 ;;
        --vuart) [ "$#" -ge 2 ] || usage; VUART=$2; shift 2 ;;
        --firmware) [ "$#" -ge 2 ] || usage; FIRMWARE=$2; shift 2 ;;
        --ramdisk) [ "$#" -ge 2 ] || usage; RAMDISK=$2; shift 2 ;;
        --chainload) CHAINLOAD=1; shift ;;
        --reuse-proxy) REUSE_PROXY=1; shift ;;
        --m1n1) [ "$#" -ge 2 ] || usage; M1N1=$2; shift 2 ;;
        --foreground) FOREGROUND=1; shift ;;
        --observed) OBSERVED=1; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done

case "$EXECUTION" in standalone|assisted) ;; *) usage ;; esac
case "$DISPLAY" in none|physical|virtual|both) ;; *) usage ;; esac
case "$DEBUG" in off|uart|full|monitor) ;; *) usage ;; esac
[ "$CHAINLOAD" -eq 0 ] || [ "$REUSE_PROXY" -eq 0 ] || usage

if [ "$OBSERVED" -eq 1 ]; then
    [ "$EXECUTION" = assisted ] || {
        echo "observed investigations require --execution assisted" >&2
        exit 2
    }
    if [ "$DISPLAY_SET" -eq 0 ]; then
        DISPLAY=both
    elif [ "$DISPLAY" != both ]; then
        echo "observed investigations require --display both" >&2
        exit 2
    fi
fi

# The public assisted entry point owns the complete launch contract.  Reusing
# whatever happens to be waiting in the proxy is an expert operation and must
# be requested explicitly; otherwise always chainload the matching artifact.
if [ "$EXECUTION" = assisted ] && [ "$REUSE_PROXY" -eq 0 ]; then
    CHAINLOAD=1
fi

virtual=disabled
case "$DISPLAY" in virtual|both) virtual=enabled ;; esac
telemetry=disabled
[ "$DEBUG" = full ] && telemetry=enabled
vuart_summary=disabled
[ "$DEBUG" = off ] || vuart_summary=${VUART:-'<auto>'}

if [ "$DRY_RUN" -eq 1 ]; then
    [ "$OBSERVED" -eq 0 ] || echo "observation: required"
    echo "execution: $EXECUTION"
    [ "$FOREGROUND" -eq 0 ] || echo "runner: foreground"
    echo "display: $DISPLAY"
    echo "debug: $DEBUG"
    echo "virtual UART: $vuart_summary"
    echo "USB framebuffer: $virtual"
    echo "telemetry: $telemetry"
    if [ "$CHAINLOAD" -eq 1 ]; then
        case "$DEBUG" in
            off) profile=release ;;
            uart) profile=debug-uart ;;
            full) profile=debug-forensic ;;
            monitor) profile=debug-monitor ;;
        esac
        echo "chainload: ${M1N1:-dist/j313/$profile/m1n1.macho}"
    elif [ "$REUSE_PROXY" -eq 1 ]; then
        echo "chainload: disabled (explicit proxy reuse)"
    else
        echo "chainload: disabled"
    fi
    [ "$OBSERVED" -eq 0 ] || "$ROOT/scripts/display-assisted.sh" --dry-run
    exit 0
fi

if [ "$EXECUTION" = standalone ]; then
    echo "Standalone profile is applied when boot.bin is built and installed." >&2
    echo "Boot the Asahi Windows entry on the target; use --dry-run to inspect defaults." >&2
    exit 1
fi

# Investigation runs must never enter the Windows-logo-to-network blind spot.
# Start the passive viewer before the guest so its generation and frame age are
# visible from the first USB framebuffer event.  Release diagnostics may remain
# off; display=both is the only additional guest-observation cost.
[ "$OBSERVED" -eq 0 ] || "$ROOT/scripts/display-assisted.sh"

set -- "$ROOT/scripts/run-assisted.sh" --display "$DISPLAY" --debug "$DEBUG"
[ -z "$PROXY" ] || set -- "$@" --proxy "$PROXY"
[ -z "$VUART" ] || set -- "$@" --vuart "$VUART"
[ -z "$FIRMWARE" ] || set -- "$@" --firmware "$FIRMWARE"
[ -z "$RAMDISK" ] || set -- "$@" --ramdisk "$RAMDISK"
[ "$CHAINLOAD" -eq 0 ] || set -- "$@" --chainload
[ -z "$M1N1" ] || set -- "$@" --m1n1 "$M1N1"
[ "$FOREGROUND" -eq 0 ] || set -- "$@" --foreground
exec "$@"
