#!/bin/sh
# Run the assisted J313 AGX G1R private-render gate.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PROXY=
CONTRACT=
ARTIFACT_DIR=
FRAME=
MANIFEST=
IDENTITY=
EVIDENCE_DIR=
CYCLES=
LAUNCH_WINDOWS=0
DRY_RUN=0

usage() {
    echo "usage: $0 --proxy DEVICE --contract FILE --artifact-dir DIR" >&2
    echo "          --frame FILE --manifest FILE --identity FILE" >&2
    echo "          --evidence-dir DIR --cycles 10" >&2
    echo "          [--launch-stable-windows] [--dry-run]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --proxy) [ "$#" -ge 2 ] || usage; PROXY=$2; shift 2 ;;
        --contract) [ "$#" -ge 2 ] || usage; CONTRACT=$2; shift 2 ;;
        --artifact-dir) [ "$#" -ge 2 ] || usage; ARTIFACT_DIR=$2; shift 2 ;;
        --frame) [ "$#" -ge 2 ] || usage; FRAME=$2; shift 2 ;;
        --manifest) [ "$#" -ge 2 ] || usage; MANIFEST=$2; shift 2 ;;
        --identity) [ "$#" -ge 2 ] || usage; IDENTITY=$2; shift 2 ;;
        --evidence-dir) [ "$#" -ge 2 ] || usage; EVIDENCE_DIR=$2; shift 2 ;;
        --cycles) [ "$#" -ge 2 ] || usage; CYCLES=$2; shift 2 ;;
        --launch-stable-windows) LAUNCH_WINDOWS=1; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done

for value in "$PROXY" "$CONTRACT" "$ARTIFACT_DIR" "$FRAME" "$MANIFEST" \
    "$IDENTITY" "$EVIDENCE_DIR"; do
    [ -n "$value" ] || usage
done
if [ "$CYCLES" != 10 ]; then
    echo "--cycles must be exactly 10" >&2
    exit 2
fi
if [ -d "$EVIDENCE_DIR" ] && [ -n "$(find "$EVIDENCE_DIR" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
    echo "Evidence directory is not empty: $EVIDENCE_DIR" >&2
    exit 1
fi

PYTHON="$ROOT/proxyenv/bin/python"
[ -x "$PYTHON" ] || PYTHON=python3
cd "$ROOT"
"$PYTHON" -m tools.agx_gate preflight \
    --root "$ROOT" --contract "$CONTRACT" --artifact-dir "$ARTIFACT_DIR"
"$PYTHON" -m tools.agx_render_gate preflight-fixture \
    --frame "$FRAME" --manifest "$MANIFEST" --identity "$IDENTITY" \
    --contract "$CONTRACT"

echo "mode: assisted AGX G1R private render gate"
echo "context: 63"
echo "queue: renderer index 1"
echo "work: TA + 3D"
echo "completion deadline: 0.5 seconds"
echo "cycles: $CYCLES"
echo "reset policy: physical cold reset after every cycle"
if [ "$LAUNCH_WINDOWS" -eq 1 ]; then
    echo "post-gate action: launch the same stable Windows artifacts"
else
    echo "post-gate action: remain at proxy"
fi
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

mkdir -p "$EVIDENCE_DIR"
CYCLE=1
while [ "$CYCLE" -le "$CYCLES" ]; do
    LABEL=$(printf "%02d" "$CYCLE")
    CYCLE_DIR="$EVIDENCE_DIR/cycle-$LABEL"
    RESULT="$CYCLE_DIR/render-gate-result.json"
    RECEIPT="$EVIDENCE_DIR/reset-$LABEL.json"
    CYCLE_OK=0
    NEEDS_REBOOT=1
    if M1N1DEVICE="$PROXY" "$PYTHON" -m tools.agx_render_gate run-one \
        --contract "$CONTRACT" --frame "$FRAME" --manifest "$MANIFEST" \
        --identity "$IDENTITY" --evidence-dir "$CYCLE_DIR"; then
        CYCLE_OK=1
    fi

    if ! M1N1DEVICE="$PROXY" "$PYTHON" \
        "$ROOT/m1n1_windows/proxyclient/tools/reboot.py"; then
        echo "hardware reboot failed after AGX G1R cycle $CYCLE" >&2
        exit 1
    fi
    NEEDS_REBOOT=0
    if [ "$CYCLE_OK" -ne 1 ]; then
        echo "AGX G1R cycle $CYCLE failed; hardware reboot requested" >&2
        exit 1
    fi

    ATTEMPT=1
    RECEIPT_OK=0
    while [ "$ATTEMPT" -le 30 ]; do
        sleep 1
        if M1N1DEVICE="$PROXY" "$PYTHON" -m tools.agx_render_gate proxy-receipt \
            --contract "$CONTRACT" --frame "$FRAME" --manifest "$MANIFEST" \
            --identity "$IDENTITY" --cycle-result "$RESULT" --cycle "$CYCLE" \
            --output "$RECEIPT"; then
            RECEIPT_OK=1
            break
        fi
        ATTEMPT=$((ATTEMPT + 1))
    done
    if [ "$RECEIPT_OK" -ne 1 ]; then
        echo "fresh proxy receipt timed out after AGX G1R cycle $CYCLE" >&2
        exit 1
    fi
    CYCLE=$((CYCLE + 1))
done

"$PYTHON" -m tools.agx_render_gate aggregate-cold \
    --contract "$CONTRACT" --frame "$FRAME" --manifest "$MANIFEST" \
    --identity "$IDENTITY" --evidence-dir "$EVIDENCE_DIR" --cycles "$CYCLES"
RESULT="$EVIDENCE_DIR/render-gate-result.json"
"$PYTHON" -m tools.agx_render_gate verify-result "$RESULT"

if [ "$LAUNCH_WINDOWS" -eq 1 ]; then
    exec "$ROOT/scripts/run-assisted.sh" \
        --proxy "$PROXY" --firmware "$ARTIFACT_DIR/J313_EFI.fd" --chainload \
        --m1n1 "$ARTIFACT_DIR/m1n1.macho" --display both --debug monitor
fi
echo "G1R complete; proxy remains available and Windows was not launched."
trap - EXIT
