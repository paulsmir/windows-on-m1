#!/bin/sh
# Reset a firmware-shell guest and wait for the m1n1 proxy to re-enumerate.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PROXY=${M1N1DEVICE:-}
VUART=${M1N1VUART:-}
DRY_RUN=0

usage() {
    echo "usage: $0 [--proxy DEVICE] [--vuart DEVICE] [--dry-run]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --proxy) [ "$#" -ge 2 ] || usage; PROXY=$2; shift 2 ;;
        --vuart) [ "$#" -ge 2 ] || usage; VUART=$2; shift 2 ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done

if [ "$DRY_RUN" -eq 1 ]; then
    echo "send UEFI shell reset through ${VUART:-<vuart-device>}"
    echo "wait for proxy ${PROXY:-<proxy-device>} and run probe.py"
    exit 0
fi

[ -n "$PROXY" ] && [ -n "$VUART" ] || {
    echo "Pass --proxy and --vuart, or set M1N1DEVICE and M1N1VUART." >&2
    exit 1
}

PYTHON="$ROOT/proxyenv/bin/python"
[ -x "$PYTHON" ] || PYTHON=python3

if pgrep -f '[r]un_uefi.py' >/dev/null; then
    "$PYTHON" - "$VUART" <<'PY'
import serial
import sys
import time

port = serial.Serial(sys.argv[1], 115200, timeout=1)
port.write(b"reset\r\n")
port.flush()
time.sleep(3)
PY
    for _ in $(seq 1 60); do
        pgrep -f '[r]un_uefi.py' >/dev/null || break
        sleep 2
    done
fi

if pgrep -f '[r]un_uefi.py' >/dev/null; then
    echo "The guest did not reset. Physically reboot the Air." >&2
    exit 1
fi

for _ in $(seq 1 60); do
    [ -e "$PROXY" ] && break
    sleep 2
done
sleep 3
[ -e "$PROXY" ] || { echo "Proxy did not re-enumerate: $PROXY" >&2; exit 1; }

M1N1DEVICE="$PROXY" "$PYTHON" "$ROOT/probe.py" >/dev/null
echo "Proxy is responding: $PROXY"
