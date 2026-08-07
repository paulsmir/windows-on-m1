#!/bin/sh
# Passively record both standalone m1n1 USB ACM channels across resets.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CONSOLE=
VUART=
OUTPUT="$ROOT/standalone-monitor-logs"
ONCE=0
DRY_RUN=0

usage() {
    echo "usage: $0 [--console DEVICE --vuart DEVICE] [--output DIR] [--once] [--dry-run]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --console) [ "$#" -ge 2 ] || usage; CONSOLE=$2; shift 2 ;;
        --vuart) [ "$#" -ge 2 ] || usage; VUART=$2; shift 2 ;;
        --output) [ "$#" -ge 2 ] || usage; OUTPUT=$2; shift 2 ;;
        --once) ONCE=1; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done

if { [ -n "$CONSOLE" ] && [ -z "$VUART" ]; } ||
   { [ -z "$CONSOLE" ] && [ -n "$VUART" ]; }; then
    usage
fi

PYTHON="$ROOT/proxyenv/bin/python"
[ -x "$PYTHON" ] || PYTHON=python3

set -- "$PYTHON" "$ROOT/tools/standalone_monitor.py" --output "$OUTPUT"
if [ -n "$CONSOLE" ]; then
    set -- "$@" --console "$CONSOLE" --vuart "$VUART"
fi
[ "$ONCE" -eq 0 ] || set -- "$@" --once

if [ "$DRY_RUN" -eq 1 ]; then
    printf '%s' "$1"
    shift
    for argument in "$@"; do
        printf ' %s' "$argument"
    done
    printf '\n'
    exit 0
fi

exec "$@"
