#!/usr/bin/env bash
set -euo pipefail

if [[ $# -lt 1 || $# -gt 2 || ! "$1" =~ ^[A-Za-z0-9-]+$ || ( $# -eq 2 && "$2" != x64 && "$2" != arm64 ) ]]; then
  echo "usage: $0 RUN_ID [x64|arm64]" >&2
  exit 64
fi
run_id="$1"
architecture="${2:-x64}"
repo_root=$(git rev-parse --show-toplevel)
stage_dir=$(mktemp -d /tmp/ad04-runtime-stage.XXXXXX)
archive="$stage_dir/source.tar.gz"
evidence_dir="$repo_root/investigation/evidence/AD04-runtime-closure/$run_id"
remote_source="C:/Users/pauls/AD04-runtime-src-${run_id}"
remote_archive="C:/Users/pauls/AD04-runtime-${run_id}.tar.gz"
remote_output="C:/Users/pauls/AD04-fullcompiler-001/asahi-runtime-${architecture}-${run_id}"
remote_native="C:/Users/pauls/AD04-fullcompiler-001/asahi-native-${architecture}-${run_id}"
if [[ -e "$evidence_dir" ]]; then
  echo "Fresh local evidence path required: $evidence_dir" >&2
  exit 64
fi
mkdir -p "$evidence_dir"
git -C "$repo_root" ls-files -co --exclude-standard drivers/apple-agx | \
  tar -C "$repo_root" -czf "$archive" -T -
archive_sha256=$(shasum -a 256 "$archive" | awk '{print $1}')
cp "$archive" "$evidence_dir/source.tar.gz"
git -C "$repo_root" rev-parse HEAD > "$evidence_dir/source-head.txt"
git -C "$repo_root" diff --binary HEAD -- drivers/apple-agx | shasum -a 256 > "$evidence_dir/tracked-diff.sha256"
printf '{"run_id":"%s","architecture":"%s","archive_sha256":"%s"}\n' \
  "$run_id" "$architecture" "$archive_sha256" > "$evidence_dir/stage-input.json"
preflight=$(python3 - "$remote_source" "$remote_archive" "$remote_output" "$remote_native" <<'PY'
import base64, sys
source, archive, output, native = sys.argv[1:]
script = f'''$ErrorActionPreference='Stop'
foreach($path in @('{source}','{archive}','{output}','{native}')){{
  if(Test-Path $path){{throw "Fresh remote path required: $path"}}
}}'''
print(base64.b64encode(script.encode('utf-16le')).decode())
PY
)
ssh -o ConnectTimeout=8 -o BatchMode=yes -i /Users/pavel/.ssh/windows_builder pauls@192.168.1.24 \
  powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand "$preflight"
scp -i /Users/pavel/.ssh/windows_builder "$archive" "pauls@192.168.1.24:${remote_archive}"
encoded=$(python3 - "$remote_source" "$remote_archive" "$run_id" "$architecture" "$archive_sha256" <<'PY'
import base64, sys
src, archive, run_id, architecture, archive_sha256 = sys.argv[1:]
script = f'''$ErrorActionPreference='Stop'
if(Test-Path '{src}'){{throw 'Fresh source path required'}}
[void](New-Item -ItemType Directory '{src}')
if((Get-FileHash -LiteralPath '{archive}' -Algorithm SHA256).Hash.ToLowerInvariant() -ne '{archive_sha256}'){{
  throw 'Staged archive SHA-256 mismatch'
}}
tar -xf '{archive}' -C '{src}'
if($LASTEXITCODE){{exit $LASTEXITCODE}}
& '{src}\\drivers\\apple-agx\\mesa\\scripts\\run-asahi-runtime-closure.ps1' -Project '{src}' -RunId '{run_id}' -Architecture '{architecture}' -ArchiveSha256 '{archive_sha256}'
exit $LASTEXITCODE'''
print(base64.b64encode(script.encode('utf-16le')).decode())
PY
)
set +e
ssh -o ConnectTimeout=8 -o BatchMode=yes -i /Users/pavel/.ssh/windows_builder pauls@192.168.1.24 \
  powershell -NoProfile -ExecutionPolicy Bypass -EncodedCommand "$encoded" \
  > "$evidence_dir/remote-console.log" 2>&1
remote_exit=$?
scp -r -i /Users/pavel/.ssh/windows_builder \
  "pauls@192.168.1.24:${remote_output}" "$evidence_dir/remote-output" \
  > "$evidence_dir/fetch.log" 2>&1
fetch_exit=$?
set -e
printf '%s\n' "$archive_sha256"
printf '%s\n' "$evidence_dir"
if [[ $fetch_exit -ne 0 ]]; then
  exit "$fetch_exit"
fi
exit "$remote_exit"
