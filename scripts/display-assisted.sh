#!/bin/sh
# Start or reuse the virtual-framebuffer viewer used by assisted mode.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PORT=8766
DRY_RUN=0

while [ "$#" -gt 0 ]; do
    case "$1" in
        --port) PORT=$2; shift 2 ;;
        --dry-run) DRY_RUN=1; shift ;;
        *) echo "usage: $0 [--port PORT] [--dry-run]" >&2; exit 2 ;;
    esac
done

URL="http://127.0.0.1:$PORT/"
if [ "$DRY_RUN" -eq 1 ]; then
    echo "virtual framebuffer: $URL"
    echo "source: $ROOT/fb.raw and $ROOT/fb-info.json"
    exit 0
fi

PYTHON="$ROOT/proxyenv/bin/python"
[ -x "$PYTHON" ] || PYTHON=python3

if ! curl -fsS --max-time 1 "$URL" >/dev/null 2>&1; then
    nohup "$PYTHON" "$ROOT/extra/display_server.py" --root "$ROOT" --port "$PORT" \
        >/tmp/m1n1-display-server.log 2>&1 &
    for _ in $(seq 1 30); do
        curl -fsS --max-time 1 "$URL" >/dev/null 2>&1 && break
        sleep 0.1
    done
fi

curl -fsS --max-time 1 "$URL" >/dev/null 2>&1 || {
    echo "Display server did not start; see /tmp/m1n1-display-server.log" >&2
    exit 1
}
echo "Virtual display: $URL"
open "$URL"
