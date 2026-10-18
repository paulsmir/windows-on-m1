#!/bin/sh
# Prove two proxy identities across one reboot without starting AGX.
set -eu

: "${AGX_BRIDGE_PORT:?AGX_BRIDGE_PORT is required}"
PROXY=/tmp/m1n1-proxy
DESTINATION=

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
    echo "transport destination already exists: $DESTINATION" >&2
    exit 2
}

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

mkdir "$DESTINATION"
export PYTHONPATH="/work:/work/m1n1_windows/proxyclient"
M1N1DEVICE="$PROXY" python3 \
    /work/tools/agx-capture-container/probe-proxy-identity.py capture \
    --output "$DESTINATION/before.json"

M1N1DEVICE="$PROXY" python3 \
    /work/m1n1_windows/proxyclient/tools/reboot.py

attempt=0
while :; do
    attempt=$((attempt + 1))
    [ "$attempt" -le 20 ] || {
        echo "fresh proxy did not return after 20 bounded attempts" >&2
        exit 1
    }
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

echo "transport probe passed: $DESTINATION/transport-receipt.json"
