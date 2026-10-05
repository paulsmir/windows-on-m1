#!/bin/sh
# Reproduce the historical Mesa startup path without importing or starting AGX.
set -eu

: "${AGX_BRIDGE_PORT:?AGX_BRIDGE_PORT is required}"
PROXY=/tmp/m1n1-proxy
DESTINATION=
MESA=/opt/asahi-mesa
SHIM="$MESA/build/src/asahi/drm-shim/libasahi_m1n1_drm_shim.so"
PROGRAM=/opt/agx-capture/bin/agx-clear-capture

python3 /opt/agx-capture/export/verify-agx-capture-env.py \
    /opt/agx-capture/export

usage()
{
    echo "usage: $0 --destination DIRECTORY" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --destination) [ "$#" -ge 2 ] || usage; DESTINATION=$2; shift 2 ;;
        *) usage ;;
    esac
done
[ -n "$DESTINATION" ] || usage
[ ! -e "$DESTINATION" ] || {
    echo "full-client destination already exists: $DESTINATION" >&2
    exit 2
}

bridge_loop()
{
    while :; do
        rm -f "$PROXY"
        socat PTY,link="$PROXY",raw,echo=0,ignoreeof \
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
    [ "$attempt" -le 100 ] || exit 1
    sleep 0.1
done
sleep 1

mkdir "$DESTINATION"
export PYTHONPATH="/work:/work/m1n1_windows/proxyclient"
M1N1DEVICE="$PROXY" \
M1N1_BOOTSTRAP_TIMEOUT=3.0 \
AGX_SHIM_MODULE=tools.agx_capture_bootstrap_probe \
AGX_BOOTSTRAP_PROBE_DIR="$DESTINATION" \
LD_PRELOAD="$SHIM" \
    "$PROGRAM" "$DESTINATION/unused.rgba"

[ -f "$DESTINATION/before.json" ]
[ -f "$DESTINATION/bootstrap-metrics.json" ]
M1N1DEVICE="$PROXY" python3 \
    /work/m1n1_windows/proxyclient/tools/reboot.py

attempt=0
while :; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 20 ] || exit 1
    sleep 2
    rm -f "$DESTINATION/after-candidate.json"
    if M1N1DEVICE="$PROXY" python3 \
        /work/tools/agx-capture-container/probe-proxy-identity.py capture \
        --output "$DESTINATION/after-candidate.json"; then
        if python3 /work/tools/agx-capture-container/probe-proxy-identity.py receipt \
            --before "$DESTINATION/before.json" \
            --after "$DESTINATION/after-candidate.json" \
            --output "$DESTINATION/transport-receipt.json"; then
            mv "$DESTINATION/after-candidate.json" "$DESTINATION/after.json"
            break
        fi
    fi
done

echo "full-client bootstrap probe passed"
