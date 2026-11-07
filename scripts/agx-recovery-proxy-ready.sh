#!/bin/bash
# Retry only a measured USB re-enumeration loss during recovery proxy readiness.
set -euo pipefail
[ "$#" -ge 4 ] || { echo 'usage: helper port1 port2 log command...' >&2; exit 2; }
port1=$1;port2=$2;log=$3;shift 3
timeout=${AGX_RECOVERY_PROXY_READY_TIMEOUT_SECONDS:-90}
settle=${AGX_RECOVERY_PROXY_SETTLE_SECONDS:-5}
retry=${AGX_RECOVERY_PROXY_RETRY_SECONDS:-3}
for value in "$timeout" "$settle" "$retry"; do
 case "$value" in ''|*[!0-9]*) echo INVALID_PROXY_WAIT >&2; exit 2;; esac
done
[ "$timeout" -gt 0 ] || { echo INVALID_PROXY_TIMEOUT >&2; exit 2; }
deadline=$(( SECONDS + timeout ))
while [ "$SECONDS" -lt "$deadline" ]; do
 if [ ! -e "$port1" ] || [ ! -e "$port2" ]; then sleep 1; continue; fi
 sleep "$settle"
 if [ ! -e "$port1" ] || [ ! -e "$port2" ]; then continue; fi
 status=0
 "$@" > "$log" 2>&1 || status=$?
 if [ "$status" -eq 0 ] && grep -q '^PROXY_NOP_PASS' "$log"; then
  echo PROXY_READY_PASS
  exit 0
 fi
 if grep -q '^Air serial port absent:' "$log" && [ "$SECONDS" -lt "$deadline" ]; then
  sleep "$retry"
  continue
 fi
 echo "PROXY_READY_REFUSED status=$status" >&2
 return_code=1
 exit "$return_code"
done
echo PROXY_READY_TIMEOUT >&2
exit 1
