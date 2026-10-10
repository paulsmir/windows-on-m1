#!/bin/bash
# Launch a staged experiment and collect its evidence (no rollback).
# usage: run-probe.sh <dir> <num> <tag> <probe script> [probe args...]
#
# ordered restart into the full profile -> wait for "Running proxy" -> launch
# full-owner<num>.sh (m1n1 chainload + run_uefi.py) -> wait for Windows SSH ->
# device state -> probe (receives the evidence dir and the probe args) -> device
# state, receipts, UMD trace, screenshots, launcher log, scanout snapshots.
# Exit 1: refused before launch; exit 2: Windows never answered SSH. Run it in
# the background and read <evidence>/probe.txt; then run the recovery script.
. "$(dirname "$0")/../lib.sh"
N=$AGX_EXPERIMENTS/$1; X=$2; TAG=$3; PROBE=$4; shift 4
E=$N/evidence/$TAG; mkdir -p "$E"
python3 "$N/verify-launch$X.py" || exit 1
guest "powershell -NoProfile -ExecutionPolicy Bypass -File $GH_WIN\\EXP$X\\ordered-restart$X.ps1" > "$E/restart-ssh.log" 2>&1
grep -q OrderedRestartToFullProfile "$E/restart-ssh.log" || { echo RESTART_REFUSED; cat "$E/restart-ssh.log"; exit 1; }
wait_proxy_ready || { echo NOP_FAIL; exit 1; }
nohup "$N/full-owner$X.sh" > "$E/launcher.log" 2>&1 &
wait_guest_up 70 || { echo SSH_NEVER_UP; exit 2; }
state() {
  guest -t 8 "powershell -NoProfile -Command \"(Get-PnpDevice -InstanceId ACPI\\APPL0002\\0).Problem; Get-Process dwm,explorer -EA 0 | %{\$_.Name+' '+\$_.Id}; [int]((Get-Date)-(Get-CimInstance Win32_OperatingSystem).LastBootUpTime).TotalSeconds; $1\"" | tr -d '\r'
}
state '' | tee "$E/state-start.txt"
bash "$PROBE" "$E" "$@" || true
state "\$p=Get-ItemProperty 'HKLM:\\SYSTEM\\CurrentControlSet\\Enum\\ACPI\\APPL0002\\0\\Device Parameters'; if(\$p.Wom1G3LeafHistory){[IO.File]::WriteAllBytes('$GH_WIN\\EXP$X\\leaf.bin',\$p.Wom1G3LeafHistory);'LEAF'}" | tee "$E/state-end.txt"
snap_receipts "$E" final
guest_get mouse-done.txt "$E/mouse-done.txt" >/dev/null 2>&1 && utf16_cat "$E/mouse-done.txt"
mkdir -p "$E/shots"; guest_get "Shots$TAG/*" "$E/shots/" >/dev/null 2>&1
guest_get "C:/ProgramData/EXP$X-Trace/umd.log" "$E/umd.log" >/dev/null 2>&1
grep -q LEAF "$E/state-end.txt" && guest_get "EXP$X/leaf.bin" "$E/leaf-history.bin" >/dev/null 2>&1
cp "$N/full.log" "$E/full.log" 2>/dev/null
grep -a 'EXP807_SNAPSHOT' "$E/full.log" | sed -E 's/.*seq=([0-9]+).*offset=(0x[0-9a-f]+).*nonzero=([0-9]+).*/\1 \2 \3/' > "$E/scanout.txt"
cat "$E/shots/shots.log" 2>/dev/null; echo "scanout frames: $(wc -l < "$E/scanout.txt")"; tail -5 "$E/scanout.txt"
grep -a '^reject-' "$E/umd.log" 2>/dev/null | cut -d' ' -f1 | sort | uniq -c
echo RUN_DONE
