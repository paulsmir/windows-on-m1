#!/bin/sh
# Run the historical Linux-only Mesa shim from macOS through a local byte bridge.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
IMAGE=windows-on-m1-agx-capture:mesa-7a4f2406
PROXY=
CONTRACT=
ARTIFACT_DIR=
IDENTITY=
DESTINATION=
BRIDGE_PORT=43137
DRY_RUN=0

usage()
{
    echo "usage: $0 --proxy DEVICE --contract REPO_PATH --artifact-dir REPO_PATH" >&2
    echo "          --identity REPO_PATH --destination ABSOLUTE_PATH" >&2
    echo "          [--bridge-port PORT] [--dry-run]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --proxy) [ "$#" -ge 2 ] || usage; PROXY=$2; shift 2 ;;
        --contract) [ "$#" -ge 2 ] || usage; CONTRACT=$2; shift 2 ;;
        --artifact-dir) [ "$#" -ge 2 ] || usage; ARTIFACT_DIR=$2; shift 2 ;;
        --identity) [ "$#" -ge 2 ] || usage; IDENTITY=$2; shift 2 ;;
        --destination) [ "$#" -ge 2 ] || usage; DESTINATION=$2; shift 2 ;;
        --bridge-port) [ "$#" -ge 2 ] || usage; BRIDGE_PORT=$2; shift 2 ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage ;;
        *) echo "unknown option: $1" >&2; exit 2 ;;
    esac
done

for value in "$PROXY" "$CONTRACT" "$ARTIFACT_DIR" "$IDENTITY" "$DESTINATION"; do
    [ -n "$value" ] || usage
done
case "$BRIDGE_PORT" in *[!0-9]*|'') usage ;; esac
[ "$BRIDGE_PORT" -ge 1024 ] && [ "$BRIDGE_PORT" -le 65535 ] || usage
for path in "$CONTRACT" "$ARTIFACT_DIR" "$IDENTITY"; do
    case "$path" in
        /*|..|../*|*/../*|*/..) echo "repository path escapes root: $path" >&2; exit 2 ;;
    esac
done
case "$DESTINATION" in /*) ;; *) echo "destination must be absolute" >&2; exit 2 ;; esac

DEST_PARENT=$(dirname -- "$DESTINATION")
DEST_NAME=$(basename -- "$DESTINATION")
[ "$DEST_NAME" != . ] && [ "$DEST_NAME" != / ] || usage

if [ "$DRY_RUN" -eq 1 ]; then
    cat <<EOF
socat TCP-LISTEN:$BRIDGE_PORT,bind=127.0.0.1,reuseaddr,fork FILE:$PROXY,raw,echo=0
docker run --rm --add-host host.docker.internal:host-gateway \\
  -v <repository-root>:/work:ro \\
  -v $DEST_PARENT:/capture-host:rw \\
  -e AGX_BRIDGE_PORT=$BRIDGE_PORT $IMAGE \\
  /opt/agx-capture/run-capture.sh \\
  --contract /work/$CONTRACT --artifact-dir /work/$ARTIFACT_DIR \\
  --identity /work/$IDENTITY --destination /capture-host/$DEST_NAME
EOF
    exit 0
fi

[ -c "$PROXY" ] || {
    echo "proxy is not a character device: $PROXY" >&2
    exit 1
}
[ -f "$ROOT/$CONTRACT" ] || { echo "missing contract: $CONTRACT" >&2; exit 1; }
[ -d "$ROOT/$ARTIFACT_DIR" ] || { echo "missing artifact directory: $ARTIFACT_DIR" >&2; exit 1; }
[ -f "$ROOT/$IDENTITY" ] || { echo "missing identity: $IDENTITY" >&2; exit 1; }
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
    "$IMAGE" /opt/agx-capture/run-capture.sh \
    --contract "/work/$CONTRACT" \
    --artifact-dir "/work/$ARTIFACT_DIR" \
    --identity "/work/$IDENTITY" \
    --destination "/capture-host/$DEST_NAME"
