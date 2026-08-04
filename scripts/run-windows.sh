#!/bin/sh
# Resolve a public Windows launch profile and dispatch the selected execution path.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
EXECUTION=standalone
DISPLAY=physical
DEBUG=off
PROXY=
VUART=
FIRMWARE=
RAMDISK=
DRY_RUN=0

usage() {
    echo "usage: $0 [--execution standalone|assisted]" >&2
    echo "          [--display none|physical|virtual|both] [--debug off|uart|full]" >&2
    echo "          [--proxy DEVICE] [--vuart DEVICE] [--firmware FILE]" >&2
    echo "          [--ramdisk FILE] [--dry-run]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --execution) [ "$#" -ge 2 ] || usage; EXECUTION=$2; shift 2 ;;
        --display) [ "$#" -ge 2 ] || usage; DISPLAY=$2; shift 2 ;;
        --debug) [ "$#" -ge 2 ] || usage; DEBUG=$2; shift 2 ;;
        --proxy) [ "$#" -ge 2 ] || usage; PROXY=$2; shift 2 ;;
        --vuart) [ "$#" -ge 2 ] || usage; VUART=$2; shift 2 ;;
        --firmware) [ "$#" -ge 2 ] || usage; FIRMWARE=$2; shift 2 ;;
        --ramdisk) [ "$#" -ge 2 ] || usage; RAMDISK=$2; shift 2 ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done

case "$EXECUTION" in standalone|assisted) ;; *) usage ;; esac
case "$DISPLAY" in none|physical|virtual|both) ;; *) usage ;; esac
case "$DEBUG" in off|uart|full) ;; *) usage ;; esac

virtual=disabled
case "$DISPLAY" in virtual|both) virtual=enabled ;; esac
telemetry=disabled
[ "$DEBUG" = full ] && telemetry=enabled
vuart_summary=disabled
[ "$DEBUG" = off ] || vuart_summary=${VUART:-'<auto>'}

if [ "$DRY_RUN" -eq 1 ]; then
    echo "execution: $EXECUTION"
    echo "display: $DISPLAY"
    echo "debug: $DEBUG"
    echo "virtual UART: $vuart_summary"
    echo "USB framebuffer: $virtual"
    echo "telemetry: $telemetry"
    exit 0
fi

if [ "$EXECUTION" = standalone ]; then
    echo "Standalone profile is applied when boot.bin is built and installed." >&2
    echo "Boot the Asahi Windows entry on the target; use --dry-run to inspect defaults." >&2
    exit 1
fi

set -- "$ROOT/scripts/run-assisted.sh" --display "$DISPLAY" --debug "$DEBUG"
[ -z "$PROXY" ] || set -- "$@" --proxy "$PROXY"
[ -z "$VUART" ] || set -- "$@" --vuart "$VUART"
[ -z "$FIRMWARE" ] || set -- "$@" --firmware "$FIRMWARE"
[ -z "$RAMDISK" ] || set -- "$@" --ramdisk "$RAMDISK"
exec "$@"
