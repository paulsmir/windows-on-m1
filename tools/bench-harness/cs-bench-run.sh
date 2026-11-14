#!/bin/bash
# cs-bench-run.sh <evidence-dir> [mode-script]: the standard CS 1.6 benchmark (EXP1184 harness: timedemo
# agxbench, fullscreen 2560x1600, Steam overlay UI off) on the live guest through an interactive scheduled
# task (arm-task.ps1, like the GravityMark arms). Recreates the pruned EXP1184 bench.sh. Prints
# CS_BENCH <frames> <seconds> <fps> or CS_BENCH_FAIL <reason>. Launches Steam: the user allowed it 2026-11-05.
set -u
. "$(dirname "$0")/../../scripts/lib/bench-env.sh"
E=$1; MS=${2:-cs-bench-playnoovl.ps1}
S=/private/tmp/claude-502/-Volumes-pwdev-public-windows/8c9ead37-29d6-4c63-9bb0-35c6eb850f40/scratchpad
BK=/Volumes/pwdev/public_windows/.local/scratchpad-tools-backup
T="$AGX_TANDEM"
mkdir -p "$E"; B=$S/bench; [ -s $B/cs-bench.ps1 ] || B=$BK/bench
G() { perl -e 'alarm 60; exec @ARGV' $T/airssh -o ConnectTimeout=8 "$@" 2>/dev/null | tr -d '\r'; }
H='C:/Program Files (x86)/Steam/steamapps/common/Half-Life/cstrike'
[ "$(G "powershell -NoProfile -Command \"Test-Path '$H/agxbench.dem'\"")" = True ] || { echo CS_BENCH_FAIL demo-missing; exit 0; }
$T/airscp $B/cs-bench.ps1 $B/$MS "$AGX_GUEST_SSH:$AGX_GUEST_HOME"/ >/dev/null 2>&1 || { echo CS_BENCH_FAIL copy; exit 0; }
$T/airscp $B/agxbench_play.cfg "pavel@192.168.1.37:$H/agxbench_play.cfg" >/dev/null 2>&1 || { echo CS_BENCH_FAIL copy-cfg; exit 0; }
$T/airscp $S/arm-task.ps1 "$AGX_GUEST_SSH:$AGX_GUEST_HOME"/arm-task.ps1 >/dev/null 2>&1 || { echo CS_BENCH_FAIL copy-arm; exit 0; }
G "powershell -NoProfile -Command \"Remove-Item C:\\Users\\pavel\\cs-bench.log -EA 0\"" >/dev/null
G "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\Users\\pavel\\arm-task.ps1 -Name AgxCsBench -Script C:\\Users\\pavel\\$MS" >/dev/null
for i in $(seq 1 90); do sleep 5; G "cmd /c findstr /b /c:\"end \" C:\\Users\\pavel\\cs-bench.log" 2>/dev/null | grep -q '^end ' && break; done  # EXP2338: no powershell inside a measured run
for f in cs-bench.log cs-bench-mem.csv; do $T/airscp "$AGX_GUEST_SSH:$AGX_GUEST_HOME"/$f "$E/" >/dev/null 2>&1; done
G "schtasks /Delete /TN AgxCsBench /F" >/dev/null
L=$(iconv -f UTF-16LE -t UTF-8 "$E/cs-bench.log" 2>/dev/null | tr -d "\r" | grep -a -o "[0-9]* frames [0-9.]* seconds [0-9.]* fps" | tail -1)
[ -n "$L" ] && echo "CS_BENCH $(echo $L | awk '{print $1, $3, $5}')" || echo "CS_BENCH_FAIL no-result $(tail -2 "$E/cs-bench.log" 2>/dev/null | tr '\n' ' ')"
