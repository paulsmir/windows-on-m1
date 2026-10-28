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
ssh_long() { perl -e 'alarm 120; exec @ARGV' "$T/airssh" -o ConnectTimeout=6 "$@"; }
ssh_bounded() { perl -e 'alarm 30; exec @ARGV' "$T/airssh" -o ConnectTimeout=6 "$@"; }
select_recovery_owner() {
  local selected status=0 launchers guest
  selected=$(python3 "$W/scripts/agx-select-exact-launcher.py" "$N" --worktree "$W" 2>&1) || status=$?
  if [ "$status" -eq 0 ] && [[ "$selected" =~ ^[1-9][0-9]*$ ]]; then
    printf '%s\n' "$selected"
    return 0
  fi
  if { [ "${AGX_RECOVERY_ALLOW_NO_OWNER:-0}" != 1 ] && [ "${AGX_RECOVERY_ALLOW_AT_PROXY:-0}" != 1 ]; } ||
     [ "$status" -ne 3 ] ||
     [ "$selected" != 'EXACT_OWNER_REFUSED: expected exactly one owner; found 0' ]; then
    printf '%s\n' "$selected" >&2
    return 3
  fi
  status=0
  launchers=$(pgrep -f 'run_uefi[.]py' 2>&1) || status=$?
  if [ "$status" -ne 1 ] || [ -n "$launchers" ]; then
    printf 'NO_HOST_OWNER_REFUSED launcher_query_status=%s output=%s\n' "$status" "$launchers" >&2
    return 3
  fi
  status=0
  guest=$(ssh_bounded 'echo up') || status=$?
  guest=${guest//$'\r'/}
  if [ "$status" -eq 0 ] && [ "$guest" = up ]; then
    if [ "${AGX_RECOVERY_ALLOW_NO_OWNER:-0}" != 1 ]; then
      printf 'AT_PROXY_REFUSED windows_ssh_up\n' >&2
      return 3
    fi
    printf '%s\n' NO_HOST_OWNER
    return 0
  fi
  # EXP1876G: a guest bugcheck/PSCI reset returns the air to Running proxy and
  # ends run_uefi; the experiment profile is booted again to own the recovery.
  if [ "${AGX_RECOVERY_ALLOW_AT_PROXY:-0}" = 1 ] && proxy_endpoints_present; then
    printf '%s\n' AT_PROXY
    return 0
  fi
  printf 'NO_HOST_OWNER_REFUSED live_windows_ssh_status=%s\n' "$status" >&2
  return 3
}
proxy_endpoints_present() {
  [ -e /dev/cu.usbmodemC02HDNCCQ6L41 ] && [ -e /dev/cu.usbmodemC02HDNCCQ6L43 ]
}
wait_cycle() {
  local owner=$1 end=$(( $(date +%s) + 180 ))
  if [ "$owner" = NO_HOST_OWNER ]; then
    # The ordered Windows reboot must become observable before proxy/serial use.
    while ssh_bounded 'echo up' >/dev/null 2>&1; do
      [ "$(date +%s)" -lt "$end" ] || { echo NO_HOST_OWNER_WINDOWS_SSH_STILL_UP; return 1; }
      sleep 3
    done
    echo NO_HOST_OWNER_WINDOWS_SSH_DOWN
  else
    while kill -0 "$owner" 2>/dev/null; do
      [ "$(date +%s)" -lt "$end" ] || { echo "OWNED_LAUNCHER_STILL_RUNNING $owner"; return 1; }
      sleep 3
    done
  fi
  end=$(( $(date +%s) + 60 ))
  until [ -e /dev/cu.usbmodemC02HDNCCQ6L41 ] && [ -e /dev/cu.usbmodemC02HDNCCQ6L43 ]; do
    [ "$(date +%s)" -lt "$end" ] || { echo USB_ENDPOINTS_MISSING; return 1; }
    sleep 3
  done
  sleep 5
  (cd "$W" && bash scripts/agx-recovery-proxy-ready.sh \
    /dev/cu.usbmodemC02HDNCCQ6L41 /dev/cu.usbmodemC02HDNCCQ6L43 "$R/proxy-ready.log" \
    perl -e 'alarm 30; exec @ARGV' /Users/pavel/public_windows/proxyenv/bin/python \
    scripts/air-serial-guard.py --port /dev/cu.usbmodemC02HDNCCQ6L41 \
    --port /dev/cu.usbmodemC02HDNCCQ6L43 -- \
    /Users/pavel/public_windows/proxyenv/bin/python \
    /Users/pavel/public_windows/.local/experiments/EXP909-render-local-view/proxy-nop.py)
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
  cp /Users/pavel/public_windows/.local/experiments/EXP950-dwm-output-trace/normal2-full.log "$R/normal-$1-full.log"
  cp /Users/pavel/public_windows/.local/experiments/EXP950-dwm-output-trace/normal2-contract.bin "$R/normal-$1-contract.bin"
}
# Select only this experiment's contract owner. Never terminate other launchers.
owners=$(select_recovery_owner) || { echo EXACT_OWNER_NOT_FOUND; exit 3; }
if [ "$owners" = AT_PROXY ]; then
  # Boot the installed experiment profile again; its launcher owns the rest.
  printf '%s\n' AT_PROXY | tee "$R/visible-at-proxy.txt"
  bash "$N/full-owner$X.sh" > "$R/at-proxy-owner-launch.log" 2>&1 &
  owners=$!
  end=$(( $(date +%s) + 420 ))
  until ssh_bounded 'echo up' >/dev/null 2>&1; do
    kill -0 "$owners" 2>/dev/null || { echo AT_PROXY_OWNER_EXITED; exit 3; }
    [ "$(date +%s)" -lt "$end" ] || { echo AT_PROXY_SSH_NOT_UP; exit 3; }
    sleep 10
  done
  echo AT_PROXY_OWNER_UP "$owners"
fi
if [ "$owners" = NO_HOST_OWNER ]; then
  printf '%s\n' NO_HOST_OWNER | tee "$R/visible-no-host-owner.txt"
else
  printf '%s\n' "$owners" > "$R/visible-previous-owner.pid"
fi
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
# EXP1920: the crash-time KMD receipts (Wom1GpuReset/Wom1GpuFault, Device Parameters) were erased by the cleanup
# before anyone read them. Snapshot them read-only before the first mutation; a failed snapshot never blocks recovery.
ssh_bounded 'powershell -NoProfile -Command "$k=Get-Item \"HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters\" -EA 0; if($k){ $k.GetValueNames() | % { $v=$k.GetValue($_); if($v -is [byte[]]){ \"$_ \" + ((($v) | % { $_.ToString(\"x2\") }) -join \"\") } else { \"$_ = $v\" } } }"' 2>/dev/null | tr -d '\r' > "$R/kmd-receipts-pre-disable.txt" || true
echo "PRE_DISABLE_RECEIPTS $(wc -l < "$R/kmd-receipts-pre-disable.txt" | tr -d ' ') values"
for f in "$W/scripts/agx-recovery-disable-service.ps1" "$R/disable-visible.ps1" "$R/cleanup-visible.ps1"; do
  perl -e 'alarm 60; exec @ARGV' "$T/airscp" "$f" "pavel@192.168.1.37:C:/Users/pavel/EXP$X/cleanup$X/" >/dev/null || { echo VISIBLE_COPY_FAILED "$f"; exit 3; }
done
# EXP2056: a recovery interrupted after the disable (the air was rebooted to
# macOS) left the guest disabled-visible with its receipt; the disable script
# never overwrites a receipt, so a rerun failed. Resume at the cleanup only
# when the receipt exists and the guest is already Code 32, service Stopped
# and Start=4; the running owner then stands in for the normal launch.
resume=$(ssh_bounded 'powershell -NoProfile -Command "$d=@(Get-PnpDevice -PresentOnly | Where-Object InstanceId -like \"*APPL0002*\"); $s=Get-CimInstance Win32_SystemDriver -Filter \"Name='"'"'AppleAgxAdmission'"'"'\"; if((Test-Path C:\Users\pavel\EXP'"$X"'\cleanup'"$X"'\visible-disable.json) -and $d.Count -eq 1 -and [int]$d[0].Problem -eq 32 -and $s.State -eq \"Stopped\" -and (Get-ItemProperty HKLM:\SYSTEM\CurrentControlSet\Services\AppleAgxAdmission).Start -eq 4){\"RESUME_DISABLED_VISIBLE\"}"' 2>/dev/null | tr -d '\r' || true)
if [ "$resume" = RESUME_DISABLED_VISIBLE ] && [ "$owners" != NO_HOST_OWNER ]; then
  echo RESUME_DISABLED_VISIBLE | tee "$R/visible-resume.txt"
  normal_pid=$owners
else
  ssh_long "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\Users\\pavel\\EXP$X\\cleanup$X\\disable-visible.ps1" | tee "$R/visible-disable-output.txt"
  grep -q 'AGX_RECOVERY_DISABLE_SERVICE: PASS' "$R/visible-disable-output.txt"
  ssh_bounded 'shutdown /r /t 3'
  wait_cycle "$owners"
  launch_normal disabled
fi
# rc1: a guest hang/bugcheck during the workload left no app logs (EXP1939 had no gm1939a UMD log, so the
# faulting resource could not be identified). They are still on the guest disk here; copy them before cleanup.
mkdir -p "$R/app-logs"
perl -e 'alarm 120; exec @ARGV' "$T/airscp" "pavel@192.168.1.37:C:/Users/pavel/gravitymark/runs/gm${X}*" "$R/app-logs/" >/dev/null 2>&1 || echo "VISIBLE_COPY_FAILED app-logs (best effort; recovery continues)"
# Keep only what the probe did not already save: byte-identical copies of evidence/EXP$X files only cost Mac disk.
for f in "$R"/app-logs/*; do s="$N/evidence/EXP$X/${f##*/}"; [ -f "$f" ] && [ -f "$s" ] && cmp -s "$f" "$s" && rm -f "$f"; done
echo "APP_LOGS_COLLECTED $(ls "$R/app-logs" | wc -l | tr -d ' ')"
ssh_bounded 'powershell -NoProfile -Command "$ErrorActionPreference=\"Stop\"; $d=@(Get-PnpDevice -PresentOnly | Where-Object InstanceId -like \"*APPL0002*\"); $s=Get-CimInstance Win32_SystemDriver -Filter \"Name='"'"'AppleAgxAdmission'"'"'\"; if($d.Count -ne 1 -or [int]$d[0].Problem -ne 32 -or $s.State -ne \"Stopped\" -or (Get-ItemProperty HKLM:\SYSTEM\CurrentControlSet\Services\AppleAgxAdmission).Start -ne 4){throw \"disabled-visible gate\"}; \"VISIBLE_CODE32_SERVICE_STOPPED\""' | tee "$R/visible-disabled-gate.txt"
grep -q VISIBLE_CODE32_SERVICE_STOPPED "$R/visible-disabled-gate.txt"
# Cleanup commits an ordered restart itself. An SSH EOF/timeout is not proof
# that removal failed; the ordered restart and durable final preflight decide.
cleanup_status=0
perl -e 'alarm 180; exec @ARGV' "$T/airssh" -o ConnectTimeout=6 "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\Users\\pavel\\EXP$X\\cleanup$X\\cleanup-visible.ps1" > "$R/cleanup-visible-output.txt" 2>&1 || cleanup_status=$?
echo "CLEANUP_SSH_STATUS $cleanup_status; require owned restart and final durable preflight"
wait_cycle "$normal_pid"
launch_normal absent
ssh_long "powershell -NoProfile -ExecutionPolicy Bypass -File C:\\Users\\pavel\\EXP$X\\cleanup$X\\durable-transition.ps1 -Mode Preflight -ManifestPath C:\\Users\\pavel\\EXP$X\\cleanup$X\\durability.json" | tr -d '\r' | tee "$R/visible-preflight.txt"
grep -Eq '"Verdict": *"PASS"' "$R/visible-preflight.txt"
ssh_bounded 'powershell -NoProfile -Command "$ErrorActionPreference=\"Stop\"; $d=@(Get-PnpDevice -PresentOnly | Where-Object InstanceId -like \"*APPL0002*\"); if($d.Count -ne 1 -or [int]$d[0].Problem -ne 28 -or (Test-Path HKLM:\SYSTEM\CurrentControlSet\Services\AppleAgxAdmission)){throw \"normal-inert gate\"}; $p=(Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors; if($p -ne 8){throw \"CPU count\"}; \"VISIBLE_INERT_PACKAGE_ABSENT_8CPU\""' | tee "$R/visible-inert-gate.txt"
grep -q VISIBLE_INERT_PACKAGE_ABSENT_8CPU "$R/visible-inert-gate.txt"
echo RECOVERY_DONE
