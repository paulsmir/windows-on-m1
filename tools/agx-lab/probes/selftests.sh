#!/bin/bash
# Probe: the no-layout self-test in modes C, B, A and the video self-test, with
# the WATCHDOG dump count before and after each. usage: selftests.sh <evidence dir>
. "$(dirname "$0")/../lib.sh"
E=$1
guest_put "$AGX_SELFTEST_DIR/agx_nolayout_selftest.exe" "$AGX_SELFTEST_DIR/agx_vid_selftest.exe" . >/dev/null 2>&1
wd() { guest -t 8 'powershell -NoProfile -Command "(gci C:\ProgramData\Microsoft\Windows\WER -Recurse -Filter WATCHDOG*.dmp -EA 0 | measure).Count"' 2>&1 | tr -d '\r' | tail -1; }
echo "watchdogs-before $(wd)" | tee -a "$E/probe.txt"
for m in C B A; do
  guest_timeout 90 "powershell -NoProfile -Command \"\$o = & $GH_WIN\\agx_nolayout_selftest.exe $m 2>&1; \\\"exit=\$LASTEXITCODE\\\"; \$o\"" 2>&1 \
    | tr -d '\r' | grep -v '^\*\*' | sed "s/^/[$m] /" | tee -a "$E/probe.txt"
  sleep 8; echo "[$m] watchdogs $(wd)" | tee -a "$E/probe.txt"
done
guest_timeout 90 "powershell -NoProfile -Command \"\$o = & $GH_WIN\\agx_vid_selftest.exe 2>&1; \\\"vid exit=\$LASTEXITCODE\\\"; \$o\"" 2>&1 \
  | tr -d '\r' | grep -v '^\*\*' | tee -a "$E/probe.txt"
