#!/bin/sh
# Build the matching m1n1 and Mu images used by assisted development mode.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
DRY_RUN=0
BUILD_MODE=--debug-build
DISPLAY=physical
DEBUG=off

usage() {
    echo "usage: $0 [--dry-run] [--release]" >&2
    echo "          [--display none|physical|virtual|both] [--debug off|uart|full]" >&2
    exit 2
}

while [ "$#" -gt 0 ]; do
    case "$1" in
        --dry-run) DRY_RUN=1; shift ;;
        --release) BUILD_MODE=--release; shift ;;
        --display) [ "$#" -ge 2 ] || usage; DISPLAY=$2; shift 2 ;;
        --debug) [ "$#" -ge 2 ] || usage; DEBUG=$2; shift 2 ;;
        -h|--help) usage ;;
        *) usage ;;
    esac
done

case "$DISPLAY" in none|physical|virtual|both) ;; *) usage ;; esac
case "$DEBUG" in off|uart|full) ;; *) usage ;; esac

set -- "$BUILD_MODE" --display "$DISPLAY" --debug "$DEBUG"

if [ "$DRY_RUN" -eq 1 ]; then
    BUILD_STANDALONE_DRY_RUN=1 "$ROOT/scripts/build-standalone.sh" "$@"
else
    "$ROOT/scripts/build-standalone.sh" "$@"
fi

PROFILE=debug
[ "$BUILD_MODE" != --release ] || PROFILE=release
echo "development m1n1: $ROOT/dist/j313/$PROFILE/m1n1.macho"
echo "development Mu: $ROOT/dist/j313/$PROFILE/J313_EFI.fd"
echo "chainload with: m1n1_windows/proxyclient/tools/chainload.py dist/j313/$PROFILE/m1n1.macho"
