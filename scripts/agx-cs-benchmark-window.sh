#!/bin/bash
# Defer a CS benchmark until the user's Steam-account window ends.
set -euo pipefail
if [ "$#" -lt 4 ]; then
  echo "usage: $0 not-before-epoch bench-script evidence-dir mode [trace]" >&2
  exit 2
fi
not_before=$1
case "$not_before" in ''|*[!0-9]*) echo INVALID_CS_WINDOW >&2; exit 2;; esac
shift
if [ "$(date -u +%s)" -lt "$not_before" ]; then
  evidence=$2
  mkdir -p -- "$evidence"
  printf '%s\n' 'CS benchmark skipped: Steam account in use by the user' | tee -a "$evidence/bench.txt"
  printf 'observed_utc=%s not_before_epoch=%s\n' "$(date -u +%Y-%m-%dT%H:%M:%SZ)" "$not_before" > "$evidence/cs-skip-window.txt"
  exit 0
fi
exec bash "$@"
