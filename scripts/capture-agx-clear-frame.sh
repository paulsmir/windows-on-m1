#!/bin/sh
# Capture the fixed private AGX clear twice across cold proxy identities.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PROXY=
CONTRACT=
ARTIFACT_DIR=
MESA_SOURCE=
SHIM_LIBRARY=
SHIM_LIBRARY_SHA256=
CAPTURE_PROGRAM=
CAPTURE_PROGRAM_SHA256=
IDENTITY=
DESTINATION=
DRY_RUN=0

usage() {
    echo "usage: $0 --proxy DEVICE --contract FILE --artifact-dir DIR" >&2
    echo "          --mesa-source DIR --shim-library FILE --shim-library-sha256 SHA256" >&2
    echo "          --capture-program FILE" >&2
    echo "          --capture-program-sha256 SHA256 --identity FILE" >&2
    echo "          --destination DIR [--dry-run]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --proxy) [ "$#" -ge 2 ] || usage; PROXY=$2; shift 2 ;;
        --contract) [ "$#" -ge 2 ] || usage; CONTRACT=$2; shift 2 ;;
        --artifact-dir) [ "$#" -ge 2 ] || usage; ARTIFACT_DIR=$2; shift 2 ;;
        --mesa-source) [ "$#" -ge 2 ] || usage; MESA_SOURCE=$2; shift 2 ;;
        --shim-library) [ "$#" -ge 2 ] || usage; SHIM_LIBRARY=$2; shift 2 ;;
        --shim-library-sha256) [ "$#" -ge 2 ] || usage; SHIM_LIBRARY_SHA256=$2; shift 2 ;;
        --capture-program) [ "$#" -ge 2 ] || usage; CAPTURE_PROGRAM=$2; shift 2 ;;
        --capture-program-sha256) [ "$#" -ge 2 ] || usage; CAPTURE_PROGRAM_SHA256=$2; shift 2 ;;
        --identity) [ "$#" -ge 2 ] || usage; IDENTITY=$2; shift 2 ;;
        --destination) [ "$#" -ge 2 ] || usage; DESTINATION=$2; shift 2 ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done

for value in "$PROXY" "$CONTRACT" "$ARTIFACT_DIR" "$MESA_SOURCE" "$SHIM_LIBRARY" \
    "$SHIM_LIBRARY_SHA256" "$CAPTURE_PROGRAM" "$CAPTURE_PROGRAM_SHA256" "$IDENTITY" \
    "$DESTINATION"; do
    [ -n "$value" ] || usage
done
if [ -d "$DESTINATION" ] && [ -n "$(find "$DESTINATION" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
    echo "Capture destination is not empty: $DESTINATION" >&2
    exit 1
fi

PYTHON=${AGX_CAPTURE_PYTHON:-"$ROOT/proxyenv/bin/python"}
[ -x "$PYTHON" ] || PYTHON=python3
cd "$ROOT"
"$PYTHON" -m tools.agx_gate preflight \
    --root "$ROOT" --contract "$CONTRACT" --artifact-dir "$ARTIFACT_DIR"
"$PYTHON" -m tools.agx_capture_clear preflight \
    --mesa-source "$MESA_SOURCE" --shim-library "$SHIM_LIBRARY" \
    --shim-library-sha256 "$SHIM_LIBRARY_SHA256" \
    --capture-program "$CAPTURE_PROGRAM" \
    --capture-program-sha256 "$CAPTURE_PROGRAM_SHA256" \
    --identity "$IDENTITY" --contract "$CONTRACT"

echo "mode: assisted AGX fixed-clear capture"
echo "clear: 16x16 RGBA8 11 22 33 ff"
echo "frame dump: enabled"
echo "attachment pull: enabled"
echo "cold captures: 2"
echo "identity policy: unique proxy identity and m1n1 base"
echo "reset policy: physical reboot after every capture"
echo "destination: $DESTINATION"
[ "$DRY_RUN" -eq 0 ] || exit 0

NEEDS_REBOOT=0
emergency_reboot() {
    status=$?
    trap - EXIT
    if [ "$NEEDS_REBOOT" -eq 1 ]; then
        M1N1DEVICE="$PROXY" "$PYTHON" \
            "$ROOT/m1n1_windows/proxyclient/tools/reboot.py" >/dev/null 2>&1 || true
    fi
    exit "$status"
}
trap emergency_reboot EXIT

mkdir -p "$DESTINATION/work"
CYCLE=1
while [ "$CYCLE" -le 2 ]; do
    LABEL=$(printf "%02d" "$CYCLE")
    CYCLE_DIR="$DESTINATION/work/capture-$LABEL"
    mkdir "$CYCLE_DIR"
    FINAL="$CYCLE_DIR/final.rgba"
    FRAME="$CYCLE_DIR/shim_frame000.agx"
    RECEIPT="$DESTINATION/work/receipt-$LABEL.json"
    CAPTURE_OK=0
    NEEDS_REBOOT=1
    if (
        cd "$CYCLE_DIR"
        ASAHI_SHIM_DUMP=1 ASAHI_SHIM_PULL=1 AGX_CAPTURE_PROGRAM="$CAPTURE_PROGRAM" \
            M1N1DEVICE="$PROXY" LD_PRELOAD="$SHIM_LIBRARY" \
            PYTHONPATH="$ROOT/m1n1_windows/proxyclient${PYTHONPATH:+:$PYTHONPATH}" \
            "$CAPTURE_PROGRAM" "$FINAL"
        [ -f "$FRAME" ]
        M1N1DEVICE="$PROXY" "$PYTHON" -m tools.agx_capture_clear live-receipt \
            --frame "$FRAME" --final-attachment "$FINAL" \
            --capture-program "$CAPTURE_PROGRAM" --identity "$IDENTITY" \
            --output "$RECEIPT"
    ); then
        CAPTURE_OK=1
    fi

    if ! M1N1DEVICE="$PROXY" "$PYTHON" \
        "$ROOT/m1n1_windows/proxyclient/tools/reboot.py"; then
        echo "hardware reboot failed after AGX capture $CYCLE" >&2
        exit 1
    fi
    NEEDS_REBOOT=0
    if [ "$CAPTURE_OK" -ne 1 ]; then
        echo "AGX capture $CYCLE failed; hardware reboot requested" >&2
        exit 1
    fi
    sleep 30
    CYCLE=$((CYCLE + 1))
done

"$PYTHON" -m tools.agx_capture_clear package-two \
    --first-receipt "$DESTINATION/work/receipt-01.json" \
    --second-receipt "$DESTINATION/work/receipt-02.json" \
    --capture-program "$CAPTURE_PROGRAM" \
    --destination "$DESTINATION/fixture"
echo "Capture pair packaged; Windows was not launched."
trap - EXIT
