#!/bin/sh
# Validate the container proxy bridge across one reboot without starting AGX.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
IMAGE=windows-on-m1-agx-capture:mesa-7a4f2406
PROXY=
DESTINATION=
BRIDGE_PORT=43138
DRY_RUN=0

usage()
{
    echo "usage: $0 --proxy DEVICE --destination ABSOLUTE_PATH" >&2
    echo "          [--bridge-port PORT] [--dry-run]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --proxy) [ "$#" -ge 2 ] || usage; PROXY=$2; shift 2 ;;
        --destination) [ "$#" -ge 2 ] || usage; DESTINATION=$2; shift 2 ;;
        --bridge-port) [ "$#" -ge 2 ] || usage; BRIDGE_PORT=$2; shift 2 ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done
[ -n "$PROXY" ] && [ -n "$DESTINATION" ] || usage
case "$BRIDGE_PORT" in *[!0-9]*|'') usage ;; esac
[ "$BRIDGE_PORT" -ge 1024 ] && [ "$BRIDGE_PORT" -le 65535 ] || usage
case "$DESTINATION" in /*) ;; *) echo "destination must be absolute" >&2; exit 2 ;; esac

DEST_PARENT=$(dirname -- "$DESTINATION")
DEST_NAME=$(basename -- "$DESTINATION")

if [ "$DRY_RUN" -eq 1 ]; then
    cat <<EOF
socat TCP-LISTEN:$BRIDGE_PORT,bind=127.0.0.1,reuseaddr,fork FILE:$PROXY,raw,echo=0
docker run --rm --add-host host.docker.internal:host-gateway \\
  -v <repository-root>:/work:ro \\
  -v $DEST_PARENT:/capture-host:rw \\
  -e AGX_BRIDGE_PORT=$BRIDGE_PORT $IMAGE \\
  /work/tools/agx-capture-container/probe-transport.sh \\
  --destination /capture-host/$DEST_NAME
EOF
    exit 0
fi

[ -c "$PROXY" ] || { echo "proxy is not a character device: $PROXY" >&2; exit 1; }
[ ! -e "$DESTINATION" ] || { echo "destination already exists: $DESTINATION" >&2; exit 2; }
mkdir -p "$DEST_PARENT"

SOCAT_PID=
cleanup()
{
    if [ -n "$SOCAT_PID" ]; then
        kill "$SOCAT_PID" >/dev/null 2>&1 || true
        wait "$SOCAT_PID" >/dev/null 2>&1 || true
    fi
}
trap cleanup EXIT HUP INT TERM

socat TCP-LISTEN:$BRIDGE_PORT,bind=127.0.0.1,reuseaddr,fork \
    FILE:"$PROXY",raw,echo=0 &
SOCAT_PID=$!
sleep 0.5
kill -0 "$SOCAT_PID" 2>/dev/null || {
    echo "host serial bridge failed to start" >&2
    exit 1
}

docker run --rm --add-host host.docker.internal:host-gateway \
    -v "$ROOT:/work:ro" \
    -v "$DEST_PARENT:/capture-host:rw" \
    -e AGX_BRIDGE_PORT="$BRIDGE_PORT" \
    "$IMAGE" /work/tools/agx-capture-container/probe-transport.sh \
    --destination "/capture-host/$DEST_NAME"
