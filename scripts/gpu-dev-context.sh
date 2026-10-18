#!/bin/sh
# Print only the bounded state needed to resume J313 GPU development.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
GIT_ROOT=$(git -C "$ROOT" rev-parse --show-toplevel)
EXPECTED_ROOT=${GPU_DEV_ROOT:-$ROOT}
if [ "$GIT_ROOT" != "$ROOT" ] || [ "$ROOT" != "$EXPECTED_ROOT" ]; then
    echo "Refusing unexpected checkout: $ROOT" >&2
    echo "Expected repository root: $EXPECTED_ROOT" >&2
    exit 1
fi

cd "$ROOT"
{
    echo "CURRENT GPU STATE"
    sed -n '1,180p' investigation/CURRENT_STATE.md
    echo
    echo "REPOSITORY IDENTITY"
    echo "root: $(git rev-parse HEAD)"
    echo "branch: $(git branch --show-current)"
    git status --short
    git submodule status
    echo
    echo "RECENT CHANGE LEDGER ROWS"
    tail -n 6 investigation/CHANGES.csv
} | sed -n '1,220p'
