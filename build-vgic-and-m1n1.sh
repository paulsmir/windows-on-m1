#!/bin/sh
# Compatibility entry point retained for existing container commands.
set -eu
ROOT=$(CDPATH= cd -- "$(dirname -- "$0")" && pwd)
exec "$ROOT/scripts/build-standalone.sh" "$@"
