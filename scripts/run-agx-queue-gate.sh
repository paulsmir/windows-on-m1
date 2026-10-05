#!/bin/sh
# Run the assisted J313 AGX G1Q queue gate before an optional stable boot.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
PROXY=
CONTRACT=
ARTIFACT_DIR=
EVIDENCE_DIR=
CYCLES=
LAUNCH_WINDOWS=0
DRY_RUN=0

usage() {
    echo "usage: $0 --proxy DEVICE --contract FILE --artifact-dir DIR" >&2
    echo "          --evidence-dir DIR --cycles 10" >&2
    echo "          [--launch-stable-windows] [--dry-run]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --proxy) [ "$#" -ge 2 ] || usage; PROXY=$2; shift 2 ;;
        --contract) [ "$#" -ge 2 ] || usage; CONTRACT=$2; shift 2 ;;
        --artifact-dir) [ "$#" -ge 2 ] || usage; ARTIFACT_DIR=$2; shift 2 ;;
        --evidence-dir) [ "$#" -ge 2 ] || usage; EVIDENCE_DIR=$2; shift 2 ;;
        --cycles) [ "$#" -ge 2 ] || usage; CYCLES=$2; shift 2 ;;
        --launch-stable-windows) LAUNCH_WINDOWS=1; shift ;;
        --dry-run) DRY_RUN=1; shift ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done

[ -n "$PROXY" ] || usage
[ -n "$CONTRACT" ] || usage
[ -n "$ARTIFACT_DIR" ] || usage
[ -n "$EVIDENCE_DIR" ] || usage
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
PREFLIGHT=$(
    "$PYTHON" -m tools.agx_gate preflight \
        --root "$ROOT" \
        --contract "$CONTRACT" \
        --artifact-dir "$ARTIFACT_DIR"
)
echo "$PREFLIGHT"

echo "mode: assisted AGX G1Q queue gate"
echo "proxy: $PROXY"
echo "contract: $CONTRACT"
echo "artifact directory: $ARTIFACT_DIR"
echo "evidence directory: $EVIDENCE_DIR"
echo "context: 63"
echo "queue: 3D index 1"
echo "commands per cycle: 1"
echo "completion deadline: 0.5 seconds"
echo "cycles: $CYCLES"
echo "reset policy: physical cold reset after every cycle"
if [ "$LAUNCH_WINDOWS" -eq 1 ]; then
    echo "post-gate action: launch the same stable Windows artifacts"
else
    echo "post-gate action: remain at proxy"
fi

if [ "$DRY_RUN" -eq 1 ]; then
    exit 0
fi

mkdir -p "$EVIDENCE_DIR"

CYCLE=1
while [ "$CYCLE" -le "$CYCLES" ]; do
    LABEL=$(printf "%02d" "$CYCLE")
    CYCLE_DIR="$EVIDENCE_DIR/cycle-$LABEL"
    RESULT="$CYCLE_DIR/queue-gate-result.json"
    RECEIPT="$EVIDENCE_DIR/reset-$LABEL.json"

    echo "AGX G1Q cold cycle $CYCLE/$CYCLES"
    CYCLE_OK=0
    if M1N1DEVICE="$PROXY" "$PYTHON" -m tools.agx_queue_gate run-one \
        --contract "$CONTRACT" \
        --evidence-dir "$CYCLE_DIR"; then
        CYCLE_OK=1
    fi

    # Software quiescence is not a qualified power reset.  Reboot after every
    # one-shot result, including a failed result, before accepting evidence.
    if ! M1N1DEVICE="$PROXY" "$PYTHON" \
        "$ROOT/m1n1_windows/proxyclient/tools/reboot.py"; then
        echo "hardware reboot failed after AGX G1Q cycle $CYCLE" >&2
        exit 1
    fi
    if [ "$CYCLE_OK" -ne 1 ]; then
        echo "AGX G1Q cycle $CYCLE failed; hardware reboot requested" >&2
        exit 1
    fi

    ATTEMPT=1
    RECEIPT_OK=0
    while [ "$ATTEMPT" -le 30 ]; do
        sleep 1
        if M1N1DEVICE="$PROXY" "$PYTHON" -m tools.agx_queue_gate proxy-receipt \
            --contract "$CONTRACT" \
            --cycle "$CYCLE" \
            --cycle-result "$RESULT" \
            --output "$RECEIPT"; then
            RECEIPT_OK=1
            break
        fi
        ATTEMPT=$((ATTEMPT + 1))
    done
    if [ "$RECEIPT_OK" -ne 1 ]; then
        echo "fresh proxy receipt timed out after AGX G1Q cycle $CYCLE" >&2
        exit 1
    fi
    CYCLE=$((CYCLE + 1))
done

"$PYTHON" -m tools.agx_queue_gate aggregate-cold \
    --contract "$CONTRACT" \
    --evidence-dir "$EVIDENCE_DIR" \
    --cycles "$CYCLES"

RESULT="$EVIDENCE_DIR/queue-gate-result.json"
"$PYTHON" -m tools.agx_queue_gate verify-result "$RESULT"

if [ "$LAUNCH_WINDOWS" -eq 1 ]; then
    exec "$ROOT/scripts/run-assisted.sh" \
        --proxy "$PROXY" \
        --firmware "$ARTIFACT_DIR/J313_EFI.fd" \
        --chainload \
        --m1n1 "$ARTIFACT_DIR/m1n1.macho" \
        --display both \
        --debug monitor
fi

echo "G1Q complete; proxy remains available and Windows was not launched."
