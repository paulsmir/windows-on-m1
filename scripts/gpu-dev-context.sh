#!/bin/sh
# Print only the bounded state needed to resume J313 GPU development.
set -eu

ROOT=$(CDPATH= cd -- "$(dirname -- "$0")/.." && pwd)
CANONICAL_ROOT=/Users/pavel/public_windows
if [ "$ROOT" != "$CANONICAL_ROOT" ]; then
    echo "Refusing non-canonical checkout: $ROOT" >&2
    echo "Expected: $CANONICAL_ROOT" >&2
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
