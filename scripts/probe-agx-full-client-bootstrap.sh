#!/bin/sh
# Qualify LD_PRELOAD/EGL/embedded-Python bootstrap through the capture bridge.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
IMAGE=windows-on-m1-agx-capture:mesa-7a4f2406
PROXY=
DESTINATION=
BRIDGE_PORT=43140
DRY_RUN=0

usage()
{
    echo "usage: $0 --proxy DEVICE --destination ABSOLUTE_PATH [--bridge-port PORT] [--dry-run]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --proxy) [ "$#" -ge 2 ] || usage; PROXY=$2; shift 2 ;;
        --destination) [ "$#" -ge 2 ] || usage; DESTINATION=$2; shift 2 ;;
        --bridge-port) [ "$#" -ge 2 ] || usage; BRIDGE_PORT=$2; shift 2 ;;
        --dry-run) DRY_RUN=1; shift ;;
        *) usage ;;
    esac
done
[ -n "$PROXY" ] && [ -n "$DESTINATION" ] || usage
case "$DESTINATION" in /*) ;; *) usage ;; esac
case "$BRIDGE_PORT" in *[!0-9]*|'') usage ;; esac

DEST_PARENT=$(dirname -- "$DESTINATION")
DEST_NAME=$(basename -- "$DESTINATION")
if [ "$DRY_RUN" -eq 1 ]; then
    echo "socat TCP-LISTEN:$BRIDGE_PORT,bind=127.0.0.1,reuseaddr,fork FILE:$PROXY,raw,echo=0"
    echo "docker run --rm -v <repository-root>:/work:ro -v $DEST_PARENT:/capture-host:rw $IMAGE /opt/agx-capture/probe-full-client-bootstrap.sh --destination /capture-host/$DEST_NAME"
    exit 0
fi

[ -c "$PROXY" ] || exit 1
[ ! -e "$DESTINATION" ] || exit 2
mkdir -p "$DEST_PARENT"
SOCAT_PID=
cleanup()
{
    [ -z "$SOCAT_PID" ] || kill "$SOCAT_PID" >/dev/null 2>&1 || true
}
trap cleanup EXIT HUP INT TERM
socat TCP-LISTEN:$BRIDGE_PORT,bind=127.0.0.1,reuseaddr,fork \
    FILE:"$PROXY",raw,echo=0 &
SOCAT_PID=$!
sleep 1
kill -0 "$SOCAT_PID"

docker run --rm --add-host host.docker.internal:host-gateway \
    -v "$ROOT:/work:ro" \
    -v "$DEST_PARENT:/capture-host:rw" \
    -v "$ROOT/tools/agx-capture-container/probe-full-client-bootstrap.sh:/opt/agx-capture/probe-full-client-bootstrap.sh:ro" \
    -e AGX_BRIDGE_PORT="$BRIDGE_PORT" \
    "$IMAGE" /opt/agx-capture/probe-full-client-bootstrap.sh \
    --destination "/capture-host/$DEST_NAME"
