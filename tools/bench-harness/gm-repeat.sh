#!/bin/bash
# gm-repeat.sh <N> <evidence-dir> <tag-suffix...>: rerun the d HD PERF arm of experiment N in the current boot under new tags.
set -u
N=$1; E=$2; shift 2
T=/Users/pavel/public_windows/.local/tandem; S=/private/tmp/claude-502/-Volumes-pwdev-public-windows/8c9ead37-29d6-4c63-9bb0-35c6eb850f40/scratchpad
G() { perl -e 'alarm 60; exec @ARGV' "$T/airssh" -o ConnectTimeout=8 "$@" | tr -d '\r'; }
mkdir -p "$E"
for R in "$@"; do tag=gm$N$R; f=gm-d3d12$R-$N.ps1
  sed "s/gm${N}d/$tag/; s/arm d:/arm $R (repeat of d):/" $S/gm-d3d12d-$N.ps1 > $S/$f
  "$T/airscp" $S/$f pavel@192.168.1.37:C:/Users/pavel/gravitymark/$f >/dev/null 2>&1
  G "powershell -NoProfile -Command \"Remove-Item C:\\Users\\pavel\\gravitymark\\runs\\$tag-* -ErrorAction SilentlyContinue\"" >/dev/null 2>&1
  G "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\Users\\pavel\\EXP$N\\arm-task.ps1 -Name AgxGM$R -Script C:\\Users\\pavel\\gravitymark\\$f" >/dev/null
  for i in $(seq 1 100); do sleep 5; G "cmd /c if exist C:\\Users\\pavel\\gravitymark\\runs\\$tag-done.txt echo True" 2>/dev/null | grep -q True && break; done  # EXP2338: no powershell inside a measured run
  for x in out.txt done.txt umd.log; do "$T/airscp" pavel@192.168.1.37:C:/Users/pavel/gravitymark/runs/$tag-$x $E/ >/dev/null 2>&1; done
  G "schtasks /Delete /TN AgxGM$R /F" >/dev/null 2>&1
  echo "GM_REPEAT $tag $(tr -d '\r\0' < $E/$tag-done.txt 2>/dev/null | head -1)"
done
