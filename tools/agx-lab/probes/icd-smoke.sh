#!/bin/bash
# Probe: the x86 GL smoke test through the private AGX OpenGL ICD.
# usage: icd-smoke.sh <evidence dir> <local ICD dir> [mode] [frames]
# <ICD dir> holds agx_gl_smoke.exe, Mesa's x86 opengl32.dll, libgallium_wgl.dll
# and the x86 VC runtime; it goes to %USERPROFILE%\icd-smoke with icd-smoke.ps1
# and the guest hashes must match. Modes: '' (colour + depth), nodepth, fbo,
# depthtest. Look for AGX_GL_SMOKE: PASS in smoke-out.txt, reject-* lines in
# icd-umd.log and Wom1G4SubmitFailure in the receipts.
. "$(dirname "$0")/../lib.sh"
E=$1; ICD=$2; MODE=${3:-}; FRAMES=${4:-300}
guest -t 8 "powershell -NoProfile -Command \"New-Item -ItemType Directory -Force $GH_WIN\\icd-smoke | Out-Null\"" >/dev/null 2>&1
upload_guest_tools
for f in "$ICD"/* "$AGX_LAB_DIR/guest/icd-smoke.ps1"; do guest_put "$f" icd-smoke >/dev/null 2>&1 || echo "COPY_FAIL $f" | tee -a "$E/probe.txt"; done
( cd "$ICD" && for f in *.dll *.exe; do printf '%s  %s\n' "$(shasum -a 256 "$f" | cut -d' ' -f1)" "$f"; done ) | sort -k2 > "$E/icd-host-hashes.txt"
guest "powershell -NoProfile -Command \"Get-ChildItem $GH_WIN\\icd-smoke -File | ?{ \$_.Extension -in '.dll','.exe' } | %{ (Get-FileHash \$_.FullName).Hash.ToLower() + '  ' + \$_.Name }\"" \
  | tr -d '\r' | sort -k2 > "$E/icd-guest-hashes.txt"
diff "$E/icd-host-hashes.txt" "$E/icd-guest-hashes.txt" >/dev/null && echo "ICD_HASHES_MATCH" | tee -a "$E/probe.txt" || echo "ICD_HASH_MISMATCH" | tee -a "$E/probe.txt"
snap_receipts "$E" before-smoke
(guest "$GH_WIN\\AppleAgxVsyncTrace.exe 60" > "$E/vsync-smoke.txt" 2>&1 &)
sleep 3
arm_interactive "$GH_WIN\\icd-smoke\\icd-smoke.ps1" "$FRAMES $MODE" | tee -a "$E/probe.txt"
for i in $(seq 1 24); do
  sleep 5
  guest -t 6 "powershell -NoProfile -Command \"Test-Path $GH_WIN\\icd-smoke\\smoke-done.txt\"" 2>/dev/null | tr -d '\r' | grep -q True && break
done
guest_alive || echo "GUEST_DOWN_AFTER_SMOKE" | tee -a "$E/probe.txt"
for f in smoke-out.txt smoke-err.txt smoke-done.txt icd-umd.log agx_gl_smoke.dmp agx_gl_smoke_hang.dmp agx_gl_smoke_abort.dmp; do
  guest_get "icd-smoke/$f" "$E/$f" >/dev/null 2>&1
done
sed 's/^/[smoke] /' "$E/smoke-out.txt" 2>/dev/null | tr -d '\r' | tee -a "$E/probe.txt"
utf16_cat "$E/smoke-done.txt" | sed 's/^/[smoke] /' | tee -a "$E/probe.txt"
wait
snap_receipts "$E" smoke
