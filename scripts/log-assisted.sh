#!/bin/sh
# Start or reuse the live hypervisor-log viewer used by assisted mode.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PORT=8765
LOG="$ROOT/hv.log"
DRY_RUN=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --port) PORT=$2; shift 2 ;;
        --log) LOG=$2; shift 2 ;;
        --dry-run) DRY_RUN=1; shift ;;
        *) echo "usage: $0 [--port PORT] [--log FILE] [--dry-run]" >&2; exit 2 ;;
    esac
done

URL="http://127.0.0.1:$PORT/"
if [ "$DRY_RUN" -eq 1 ]; then
    echo "live hypervisor log: $URL"
    echo "source: $LOG"
    exit 0
fi

PYTHON="$ROOT/proxyenv/bin/python"
[ -x "$PYTHON" ] || PYTHON=python3

if ! curl -fsS --max-time 1 "$URL" >/dev/null 2>&1; then
    nohup env HVLOG="$LOG" HVPORT="$PORT" "$PYTHON" "$ROOT/logview.py" \
        >/tmp/m1n1-log-server.log 2>&1 &
    for _ in $(seq 1 30); do
        curl -fsS --max-time 1 "$URL" >/dev/null 2>&1 && break
        sleep 0.1
    done
fi

curl -fsS --max-time 1 "$URL" >/dev/null 2>&1 || {
    echo "Log server did not start; see /tmp/m1n1-log-server.log" >&2
    exit 1
}
echo "Live hypervisor log: $URL"
open "$URL"
