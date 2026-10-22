#!/bin/bash
# Prepare and stage a hardware experiment from the previous one.
# usage: [AGX_M1N1=<macho>] prepare.sh <prev num> <prev dir> <num> <dir> "<purpose>"
#
# Needs the builder directory <dir> (builder.sh build or copy). Steps: fetch and
# verify the package against its build receipt; clone the launch, stage and
# cleanup scripts from <prev dir>; record the guest boot identity; write the
# manifests; upload; compare host and guest hashes; stage the package on the
# guest (the driver binds at the next ordered restart); refresh the hardware
# manifest; run verify-launch; derive the recovery scripts. Read the output: the
# run must not start unless the receipt hashes, HASHES_MATCH and the verify-launch
# PASS line are all present.
set -e
. "$(dirname "$0")/../lib.sh"
P=$1; PD=$2; X=$3; D=$4; PUR="$5"; E=$AGX_EXPERIMENTS; N=$E/$D; HERE=$(dirname "$0")
mkdir -p "$N"
for f in AppleAgxRenderAdmission.inf AppleAgxRenderAdmission.sys AppleAgxRenderAdmissionUmd.dll appleagxrenderadmission.cat kmd-build-receipt.json arm64-build.log; do
  builder_get "$D/$f" "$N/" 2>/dev/null || true
done
grep -aE '[0-9]+ Warning\(s\)|[0-9]+ Error\(s\)' "$N/arm64-build.log" | tail -2 | tr -s ' '
mkdir -p "$N/package" "$N/evidence"; cp "$N"/*.inf "$N"/*.sys "$N"/*.dll "$N"/*.cat "$N/package/"
python3 -c "
import json,hashlib;from pathlib import Path;N=Path('$N');r=json.load(open(N/'kmd-build-receipt.json',encoding='utf-8-sig'))
[print(n,h[:8]) for n,h in r['ArtifactSha256'].items() if hashlib.sha256((N/n).read_bytes()).hexdigest()==h or exit('MISMATCH '+n)]"
python3 "$HERE/clone_experiment.py" "$P" "$PD" "$X" "$D" | tail -1
guest "powershell -NoProfile -Command \"New-Item -ItemType Directory -Force $GH_WIN\\EXP$X\\package,$GH_WIN\\EXP$X\\cleanup$X | Out-Null\""
guest_put "$N/boot$X.ps1" "EXP$X" >/dev/null 2>&1
guest "powershell -NoProfile -ExecutionPolicy Bypass -File $GH_WIN\\EXP$X\\boot$X.ps1" | tr -d '\r' | tail -1 > "$N/evidence/boot-identity.json"
cat "$N/evidence/boot-identity.json"
python3 "$HERE/write_manifests.py" "$P" "$PD" "$X" "$D" "$PUR"
# AGX_M1N1=<macho>: launch this experiment with another m1n1 than the previous one
# (full-owner, manifest and hardware manifest; the hardware manifest is re-hashed below).
if [ -n "${AGX_M1N1:-}" ]; then
  [ -f "$AGX_M1N1" ] || { echo "AGX_M1N1 missing: $AGX_M1N1"; exit 1; }
  OLD_M1N1=$(python3 -c "import json;print(json.load(open('$N/manifest.json'))['M1n1'])")
  sed -i '' "s#$OLD_M1N1#$AGX_M1N1#g" "$N/full-owner$X.sh" "$N/manifest.json" "$N/hardware-manifest.json"
  echo "M1N1 $OLD_M1N1 -> $AGX_M1N1 $(shasum -a 256 "$AGX_M1N1" | cut -c1-16)"
fi
cd "$N"
for f in manifest.json stage$X.ps1 ordered-restart$X.ps1 watch-dwm.ps1 pinned.cer package/* cleanup$X/*; do
  d=$(dirname "$f"); [ "$d" = . ] && d=
  guest_put "$f" "EXP$X${d:+/$d}" >/dev/null 2>&1 || echo "FAIL $f"
done
guest "powershell -NoProfile -Command \"gci $GH_WIN\\EXP$X -Recurse -File | %{\$_.FullName.Replace('$GH_WIN\\EXP$X\\','')+' '+(Get-FileHash \$_.FullName).Hash.ToLower()}\"" \
  | tr -d '\r' | sed 's#\\#/#g' | sort > evidence/guest-hashes.txt
for f in manifest.json stage$X.ps1 ordered-restart$X.ps1 watch-dwm.ps1 pinned.cer boot$X.ps1 package/* cleanup$X/*; do
  printf '%s %s\n' "$f" "$(shasum -a 256 "$f" | cut -d' ' -f1)"
done | sort > evidence/host-hashes.txt
diff evidence/host-hashes.txt evidence/guest-hashes.txt || { echo HASH_MISMATCH_NOT_STAGED; exit 1; }
echo HASHES_MATCH "$(wc -l < evidence/host-hashes.txt)"
guest "powershell -NoProfile -ExecutionPolicy Bypass -File $GH_WIN\\EXP$X\\stage$X.ps1" > stage-ssh.log 2>&1; tail -2 stage-ssh.log
guest_get "EXP$X/stage-receipt.json" stage-receipt.json >/dev/null 2>&1
python3 -c "
from pathlib import Path;import json,hashlib
D=Path('$N');hw=json.loads((D/'hardware-manifest.json').read_text())
hw['Artifacts']={k:hashlib.sha256(Path(k).read_bytes()).hexdigest() for k in hw['Artifacts']}
(D/'hardware-manifest.json').write_text(json.dumps(hw,indent=2))"
python3 "verify-launch$X.py"; shasum -a 256 manifest.json hardware-manifest.json "full-owner$X.sh" | cut -c1-16
# Recovery scripts: the previous experiment's, renamed to this one.
mkdir -p "$N/evidence/rec${X}h" "$N/evidence/rec$X"
sed "s#$PD#$D#g; s/rec${P}h/rec${X}h/g; s/rec$P/rec$X/g; s/EXP$P/EXP$X/g; s/cleanup$P/cleanup$X/g" \
  "$E/$PD/evidence/rec${P}h/recover.sh" > "$N/evidence/rec${X}h/recover.sh"
sed "s#$PD#$D#g; s/rec${P}h/rec${X}h/g" "$E/$PD/evidence/rec${P}h/launch-hidden.sh" > "$N/evidence/rec${X}h/launch-hidden.sh"
chmod +x "$N/evidence/rec${X}h/"*.sh
echo "recovery references:"; grep -o 'EXP[0-9][0-9]*[^ /]*' "$N/evidence/rec${X}h/"*.sh | sort -u
