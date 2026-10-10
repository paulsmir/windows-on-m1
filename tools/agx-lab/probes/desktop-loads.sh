#!/bin/bash
# Probe: the desktop control load set (EXP1143 order; EXP1144 soak with CYCLES>1).
# usage: desktop-loads.sh <evidence dir> [cycles]   (SKIP_SELFTESTS=1 skips selftests.sh)
# Per cycle: Notepad position-only drag, Notepad typing, File Explorer drag,
# charmap position-only drag, charmap same-size drag (30 s each, vsync trace,
# receipts mid-load), Settings open/close x3 with a host-side DCP swap timeline.
# Then guest events, the paging profile and a post-load video self-test.
# Keep this order and run it before any other GPU client: EXP1150-EXP1152
# showed that a GL process or a fresh Notepad before the loads changes the
# typing and charmap pacing. Analyse with analysis/latch_pacing.py, vsync_fps.py
# and (CYCLES>1) soak_summary.py.
. "$(dirname "$0")/../lib.sh"
E=$1; CYCLES=${2:-1}; N=$(dirname "$(dirname "$E")")
[ "${SKIP_SELFTESTS:-}" = 1 ] || bash "$AGX_LAB_DIR/probes/selftests.sh" "$E"
guest_alive || { echo "GUEST_DOWN_AFTER_PROBES" | tee -a "$E/probe.txt"; exit 0; }
upload_guest_tools
snap_receipts "$E" nolayout
for c in $(seq 1 "$CYCLES"); do
  s=; [ "$CYCLES" -gt 1 ] && s=-c$c
  echo "CYCLE $c $(date -u +%H:%M:%SZ)" | tee -a "$E/probe.txt"
  guest_alive || { echo "GUEST_DOWN_CYCLE_$c" | tee -a "$E/probe.txt"; break; }
  guest "powershell -NoProfile -ExecutionPolicy Bypass -File $GH_WIN\\notepad-reset.ps1" | tr -d '\r' | tee -a "$E/probe.txt"
  run_load "$E" "npmove$s" load-notepad-move.ps1 npmove-done.txt
  run_load "$E" "nptype$s" load-notepad-type.ps1 nptype-done.txt
  guest_kill notepad
  run_load "$E" "exmove$s" load-explorer-move.ps1 exmove-done.txt
  run_load "$E" "gdimove$s" load-charmap-move.ps1 gdi-nosize-done.txt
  guest_kill charmap
  run_load "$E" "gdiresize$s" load-charmap-resize.ps1 gdi-fast-done.txt
  guest -t 8 'powershell -NoProfile -Command "Get-Process charmap -EA 0 | Stop-Process -Force; (New-Object -ComObject Shell.Application).Windows() | %{ $_.Quit() }"' >/dev/null 2>&1
  # Settings open/close: vsync trace + host-side DCP swap timeline from the launcher log.
  ( for i in $(seq 1 120); do
      printf '%s %s %s\n' "$(perl -MTime::HiRes=time -MPOSIX=strftime -e '$t=time; printf "%s.%03d", strftime("%H:%M:%S",gmtime($t)), ($t-int($t))*1000')" \
        "$(stat -f %z "$N/full.log" 2>/dev/null)" "$(tail -c 4000 "$N/full.log" 2>/dev/null | tr -d '\000' | grep -ao 'swap_id=[0-9]*' | tail -1)"
      sleep 0.5
    done > "$E/swap-timeline$s.txt" ) &
  (guest "$GH_WIN\\AppleAgxVsyncTrace.exe 52" > "$E/vsync-settings$s.txt" 2>&1 &)
  sleep 3
  arm_interactive "$GH_WIN\\load-settings-cycle.ps1"
  sleep 50; snap_receipts "$E" "settings$s"
  guest_get settings-done.txt "$E/settings-done$s.txt" >/dev/null 2>&1
  utf16_cat "$E/settings-done$s.txt" | sed "s/^/[settings$s] /" | tee -a "$E/probe.txt"
done
guest "powershell -NoProfile -ExecutionPolicy Bypass -File $GH_WIN\\events.ps1" | tr -d '\r' > "$E/events-after-loads.txt" 2>&1
wait
guest -t 8 'powershell -NoProfile -Command "$p=Get-ItemProperty \"HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters\"; [Convert]::ToBase64String($p.Wom1G3PagingProfile)"' 2>&1 \
  | grep -v '^\*\*' | tr -d '\r' > "$E/pp-after.b64"
post_vid "$E"
snap_receipts "$E" end
