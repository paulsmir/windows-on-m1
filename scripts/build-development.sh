#!/bin/sh
# Build the matching m1n1 and Mu images used by assisted development mode.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DRY_RUN=0
RELEASE=

case "${1:-}" in
    "") ;;
    --dry-run) DRY_RUN=1 ;;
    --release) RELEASE=--release ;;
    *) echo "usage: $0 [--dry-run|--release]" >&2; exit 2 ;;
esac

if [ "$DRY_RUN" -eq 1 ]; then
    BUILD_STANDALONE_DRY_RUN=1 "$ROOT/scripts/build-standalone.sh"
else
    if [ -n "$RELEASE" ]; then
        "$ROOT/scripts/build-standalone.sh" "$RELEASE"
    else
        "$ROOT/scripts/build-standalone.sh"
    fi
fi

echo "development m1n1: $ROOT/dist/j313/m1n1.macho"
echo "development Mu: $ROOT/dist/j313/J313_EFI.fd"
echo "chainload with: m1n1_windows/proxyclient/tools/chainload.py dist/j313/m1n1.macho"
