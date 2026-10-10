#!/bin/bash
# Probe: CPU/wait profile of the two loads that separate driver cost from
# win32k/DWM behaviour (EXP1145): Notepad typing and the charmap same-size drag,
# each with a context-switch ETW capture. usage: cpu-profile.sh <evidence dir>
. "$(dirname "$0")/../lib.sh"
E=$1
upload_guest_tools
snap_receipts "$E" nolayout
guest "powershell -NoProfile -ExecutionPolicy Bypass -File $GH_WIN\\notepad-reset.ps1" | tr -d '\r' | tee -a "$E/probe.txt"
profile_load "$E" nptype load-notepad-type.ps1 nptype-done.txt cpu-typing
guest_kill notepad
profile_load "$E" gdiresize load-charmap-resize.ps1 gdi-fast-done.txt cpu-resize
guest_kill charmap
post_vid "$E"
snap_receipts "$E" end
