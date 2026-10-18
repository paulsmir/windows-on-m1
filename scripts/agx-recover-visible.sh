#!/bin/bash
# Exact-package rollback through GPU-visible, service-disabled Windows.
set -euo pipefail
N=${1:?experiment directory}; X=${2:?experiment number}
[[ "$X" =~ ^[0-9]+$ && "$(basename "$N")" == EXP"$X"-* ]] || exit 2
W=/Volumes/pwdev/public_windows/.worktrees/integration-ad04-windows-compiler
T=/Users/pavel/public_windows/.local/tandem
R="$N/evidence/rec${X}h"
NORMAL=/Users/pavel/public_windows/.local/experiments/EXP950-dwm-output-trace/launch-normal2.sh
mkdir -p "$R" /tmp/agx-lld-dir
[ -x /tmp/agx-lld-dir/ld.lld ] || cp /Volumes/pwdev/public_windows/.local/toolchain22/ld.lld /tmp/agx-lld-dir/ld.lld
ssh_bounded() { perl -e 'alarm 30; exec @ARGV' "$T/airssh" -o ConnectTimeout=6 "$@"; }
wait_cycle() {
  local owner=$1 end=$(( $(date +%s) + 180 ))
  while kill -0 "$owner" 2>/dev/null; do
    [ "$(date +%s)" -lt "$end" ] || { echo "OWNED_LAUNCHER_STILL_RUNNING $owner"; return 1; }
    sleep 3
  done
  end=$(( $(date +%s) + 60 ))
  until [ -e /dev/cu.usbmodemC02HDNCCQ6L41 ] && [ -e /dev/cu.usbmodemC02HDNCCQ6L43 ]; do
    [ "$(date +%s)" -lt "$end" ] || { echo USB_ENDPOINTS_MISSING; return 1; }
    sleep 3
  done
  sleep 5
  (cd "$W" && perl -e 'alarm 30; exec @ARGV' /Users/pavel/public_windows/proxyenv/bin/python scripts/air-serial-guard.py --port /dev/cu.usbmodemC02HDNCCQ6L41 --port /dev/cu.usbmodemC02HDNCCQ6L43 -- /Users/pavel/public_windows/proxyenv/bin/python /Users/pavel/public_windows/.local/experiments/EXP909-render-local-view/proxy-nop.py) > "$R/proxy-ready.log" 2>&1
}
launch_normal() {
  bash "$NORMAL" > "$R/normal-$1-launch.log" 2>&1 &
  normal_pid=$!
  local end=$(( $(date +%s) + 300 ))
  sleep 30
  until ssh_bounded 'echo up'; do
    [ "$(date +%s)" -lt "$end" ] || { echo NORMAL_SSH_NOT_UP; return 1; }
    sleep 5
  done
}
# Select only this experiment's contract owner. Never terminate other launchers.
owners=$(pgrep -f "[r]un_uefi.py.*${N}/contract.bin" || true)
[ "$(printf '%s\n' "$owners" | sed '/^$/d' | wc -l | tr -d ' ')" = 1 ] || { echo EXACT_OWNER_NOT_FOUND; exit 3; }
printf '%s\n' "$owners" > "$R/visible-previous-owner.pid"
python3 - "$N" "$X" "$W" <<'PY'
from pathlib import Path
import re,sys
n=Path(sys.argv[1]); x=sys.argv[2]; w=Path(sys.argv[3]); r=n/'evidence'/f'rec{x}h'
original=(n/f'cleanup{x}'/'cleanup-hidden.ps1').read_text()
assert original.count('-ExpectedProblemCode 45')==1
(r/'cleanup-visible.ps1').write_text(original.replace('-ExpectedProblemCode 45','-ExpectedProblemCode 32'))
args={k:re.search(r"-"+k+r" '([^']+)'",original).group(1) for k in ['ExpectedPublishedName','ExpectedInfSha256','ExpectedSysSha256','ExpectedUmdSha256']}
for k,v in args.items(): assert re.fullmatch(r'oem[0-9]+\.inf' if k=='ExpectedPublishedName' else r'[0-9a-fA-F]{64}',v)
cmd=f"& 'C:\\Users\\pavel\\EXP{x}\\cleanup{x}\\agx-recovery-disable-service.ps1' "
cmd+=' '.join(f"-{k} '{v}'" for k,v in args.items())
cmd+=f" -ReceiptPath 'C:\\Users\\pavel\\EXP{x}\\cleanup{x}\\visible-disable.json'\n"
(r/'disable-visible.ps1').write_text(cmd)
PY
for f in "$W/scripts/agx-recovery-disable-service.ps1" "$R/disable-visible.ps1" "$R/cleanup-visible.ps1"; do
  "$T/airscp" "$f" "pavel@192.168.1.37:C:/Users/pavel/EXP$X/cleanup$X/" >/dev/null
done
ssh_bounded "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\Users\\pavel\\EXP$X\\cleanup$X\\disable-visible.ps1" | tee "$R/visible-disable-output.txt"
grep -q 'AGX_RECOVERY_DISABLE_SERVICE: PASS' "$R/visible-disable-output.txt"
ssh_bounded 'shutdown /r /t 3'
wait_cycle "$owners"
launch_normal disabled
ssh_bounded 'powershell -NoProfile -Command "$ErrorActionPreference=\"Stop\"; $d=@(Get-PnpDevice -PresentOnly | Where-Object InstanceId -like \"*APPL0002*\"); $s=Get-CimInstance Win32_SystemDriver -Filter \"Name='"'"'AppleAgxAdmission'"'"'\"; if($d.Count -ne 1 -or [int]$d[0].Problem -ne 32 -or $s.State -ne \"Stopped\" -or (Get-ItemProperty HKLM:\SYSTEM\CurrentControlSet\Services\AppleAgxAdmission).Start -ne 4){throw \"disabled-visible gate\"}; \"VISIBLE_CODE32_SERVICE_STOPPED\""' | tee "$R/visible-disabled-gate.txt"
grep -q VISIBLE_CODE32_SERVICE_STOPPED "$R/visible-disabled-gate.txt"
ssh_bounded "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\Users\\pavel\\EXP$X\\cleanup$X\\cleanup-visible.ps1" > "$R/cleanup-visible-output.txt" 2>&1
grep -q '"Staged":  *0' "$R/cleanup-visible-output.txt"
grep -q '"Sys":  *false' "$R/cleanup-visible-output.txt"
grep -q '"Umd":  *false' "$R/cleanup-visible-output.txt"
wait_cycle "$normal_pid"
launch_normal absent
ssh_bounded "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\Users\\pavel\\EXP$X\\cleanup$X\\durable-transition.ps1 -Mode Preflight -ManifestPath C:\\Users\\pavel\\EXP$X\\cleanup$X\\durability.json" | tr -d '\r' | tee "$R/visible-preflight.txt"
grep -Eq '"Verdict": *"PASS"' "$R/visible-preflight.txt"
ssh_bounded 'powershell -NoProfile -Command "$ErrorActionPreference=\"Stop\"; $d=@(Get-PnpDevice -PresentOnly | Where-Object InstanceId -like \"*APPL0002*\"); if($d.Count -ne 1 -or [int]$d[0].Problem -ne 28 -or (Test-Path HKLM:\SYSTEM\CurrentControlSet\Services\AppleAgxAdmission)){throw \"normal-inert gate\"}; $p=(Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors; if($p -ne 8){throw \"CPU count\"}; \"VISIBLE_INERT_PACKAGE_ABSENT_8CPU\""' | tee "$R/visible-inert-gate.txt"
grep -q VISIBLE_INERT_PACKAGE_ABSENT_8CPU "$R/visible-inert-gate.txt"
echo RECOVERY_DONE
