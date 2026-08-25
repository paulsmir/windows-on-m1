#!/bin/sh
# Run the bounded J313 AGX firmware gate before an optional stable Windows boot.
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

echo "mode: assisted AGX firmware gate"
echo "proxy: $PROXY"
echo "contract: $CONTRACT"
echo "artifact directory: $ARTIFACT_DIR"
echo "evidence directory: $EVIDENCE_DIR"
echo "cycles: $CYCLES"
if [ "$LAUNCH_WINDOWS" -eq 1 ]; then
    echo "post-gate action: launch the same stable Windows artifacts"
else
    echo "post-gate action: remain at proxy"
fi

if [ "$DRY_RUN" -eq 1 ]; then
    exit 0
fi

if [ -d "$EVIDENCE_DIR" ] && [ -n "$(find "$EVIDENCE_DIR" -mindepth 1 -maxdepth 1 -print -quit)" ]; then
    echo "Evidence directory is not empty: $EVIDENCE_DIR" >&2
    exit 1
fi

M1N1DEVICE="$PROXY" "$PYTHON" -m tools.agx_gate run \
    --contract "$CONTRACT" \
    --evidence-dir "$EVIDENCE_DIR" \
    --cycles "$CYCLES"

RESULT="$EVIDENCE_DIR/gate-result.json"
"$PYTHON" -m tools.agx_gate verify-result "$RESULT"

if [ "$LAUNCH_WINDOWS" -eq 1 ]; then
    exec "$ROOT/scripts/run-assisted.sh" \
        --proxy "$PROXY" \
        --firmware "$ARTIFACT_DIR/J313_EFI.fd" \
        --chainload \
        --m1n1 "$ARTIFACT_DIR/m1n1.macho" \
        --display both \
        --debug monitor
fi

echo "G1 complete; proxy remains available and Windows was not launched."
