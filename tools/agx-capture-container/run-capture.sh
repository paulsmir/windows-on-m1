#!/bin/sh
# Reconnect a Linux PTY across physical m1n1 USB resets, then run the bounded capture.
set -eu

: "${AGX_BRIDGE_PORT:?AGX_BRIDGE_PORT is required}"
PROXY=/tmp/m1n1-proxy
EXPORT=/opt/agx-capture/export
MESA=/opt/asahi-mesa
SHIM="$MESA/build/src/asahi/drm-shim/libasahi_m1n1_drm_shim.so"
PROGRAM=/opt/agx-capture/bin/agx-clear-capture

python3 "$EXPORT/verify-agx-capture-env.py" "$EXPORT"
SHIM_SHA=$(python3 -c \
    'import json; print(json.load(open("/opt/agx-capture/export/manifest.json"))["artifacts"]["libasahi_m1n1_drm_shim.so"])')
PROGRAM_SHA=$(python3 -c \
    'import json; print(json.load(open("/opt/agx-capture/export/manifest.json"))["artifacts"]["agx-clear-capture"])')

bridge_loop()
{
    while :; do
        rm -f "$PROXY"
        socat PTY,link=/tmp/m1n1-proxy,raw,echo=0,ignoreeof \
            TCP:host.docker.internal:${AGX_BRIDGE_PORT} || true
        sleep 1
    done
}
bridge_loop &
BRIDGE_PID=$!
cleanup()
{
    kill "$BRIDGE_PID" >/dev/null 2>&1 || true
    wait "$BRIDGE_PID" >/dev/null 2>&1 || true
}
trap cleanup EXIT HUP INT TERM

attempt=0
while [ ! -e "$PROXY" ]; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 100 ] || {
        echo "container serial bridge did not create $PROXY" >&2
        exit 1
    }
    sleep 0.1
done
sleep 1

AGX_CAPTURE_PYTHON=python3 /work/scripts/capture-agx-clear-frame.sh \
    --proxy "$PROXY" \
    --mesa-source "$MESA" \
    --shim-library "$SHIM" \
    --shim-library-sha256 "$SHIM_SHA" \
    --capture-program "$PROGRAM" \
    --capture-program-sha256 "$PROGRAM_SHA" \
    "$@"
