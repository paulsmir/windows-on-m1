#!/usr/bin/env bash
# Build and run the Windows UMD contract test (umd/tests/UmdContractTest.vcxproj,
# Release|x64) on the Windows builder from a git archive, and print PASS/FAIL.
#
#   run-umd-contract-test.sh [REV]          archive drivers/apple-agx at REV (default HEAD)
#   run-umd-contract-test.sh --worktree     archive the working tree (tracked files,
#                                           uncommitted edits included) via git stash create
#
# Each run uses a fresh C:\Users\pauls\umdcontract-<rev>-<utc> directory, kept on FAIL
# for debugging and removed on PASS. It never touches the persistent package tree
# (AD04-persistent-dwm-next) and does not start while msbuild/cl/link run on the
# builder (exit 75). Local logs: .local/umd-contract/<run>/.
# Exit status: 0 PASS, 1 FAIL, 64 usage, 75 builder busy, other: transport error.
set -euo pipefail

builder="pauls@192.168.1.24"
key="${UMD_CONTRACT_BUILDER_KEY:-$HOME/.ssh/windows_builder}"
ssh_opts=(-o ConnectTimeout=8 -o BatchMode=yes -o LogLevel=ERROR -i "$key")

worktree=0
rev="HEAD"
case "${1:-}" in
  --worktree) worktree=1; [[ $# -eq 1 ]] || { echo "usage: $0 [REV | --worktree]" >&2; exit 64; } ;;
  -h|--help) sed -n '2,13p' "$0" | sed 's/^# \{0,1\}//'; exit 0 ;;
  "") ;;
  *) rev="$1"; [[ $# -eq 1 ]] || { echo "usage: $0 [REV | --worktree]" >&2; exit 64; } ;;
esac

repo_root=$(git rev-parse --show-toplevel)
head_commit=$(git -C "$repo_root" rev-parse --verify "${rev}^{commit}")
source_commit="$head_commit"
label="${head_commit:0:12}"
if [[ $worktree -eq 1 ]]; then
  # A dangling commit object of the index/worktree; no ref or stash entry is written.
  snapshot=$(git -C "$repo_root" stash create)
  if [[ -n "$snapshot" ]]; then
    source_commit="$snapshot"
    label="${head_commit:0:12}-dirty"
  fi
fi
stamp=$(date -u +%Y%m%dT%H%M%SZ)
run_id="umdcontract-${label}-${stamp}"
evidence_dir="$repo_root/.local/umd-contract/$run_id"
mkdir -p "$evidence_dir"
archive="$evidence_dir/source.tar.gz"
git -C "$repo_root" archive --format=tar "$source_commit" drivers/apple-agx | gzip -n > "$archive"
archive_sha256=$(shasum -a 256 "$archive" | awk '{print $1}')
remote_root="C:/Users/pauls/$run_id"
remote_archive="C:/Users/pauls/$run_id.tar.gz"
{
  printf 'run_id=%s\nhead=%s\nsource_commit=%s\nworktree_snapshot=%s\n' \
    "$run_id" "$head_commit" "$source_commit" "$worktree"
  printf 'archive_sha256=%s\nremote_root=%s\n' "$archive_sha256" "$remote_root"
} > "$evidence_dir/run.txt"

encode() {  # PowerShell -EncodedCommand payload (UTF-16LE base64) from stdin.
  python3 -c 'import base64,sys; print(base64.b64encode(sys.stdin.read().encode("utf-16le")).decode())'
}

preflight=$(encode <<EOF
\$ErrorActionPreference = 'Stop'
\$busy = @(Get-Process msbuild,cl,link -ErrorAction SilentlyContinue)
if (\$busy.Count -gt 0) {
  \$busy | Format-Table Name,Id,StartTime -AutoSize | Out-String | Write-Output
  exit 75
}
foreach (\$path in @('$remote_root','$remote_archive')) {
  if (Test-Path \$path) { throw "Fresh remote path required: \$path" }
}
exit 0
EOF
)
set +e
ssh "${ssh_opts[@]}" "$builder" powershell -NoProfile -ExecutionPolicy Bypass \
  -EncodedCommand "$preflight" > "$evidence_dir/preflight.log" 2>&1
preflight_exit=$?
set -e
if [[ $preflight_exit -eq 75 ]]; then
  cat "$evidence_dir/preflight.log" >&2
  echo "UMD contract test: BUSY (msbuild/cl/link running on the builder; not started)" >&2
  exit 75
elif [[ $preflight_exit -ne 0 ]]; then
  cat "$evidence_dir/preflight.log" >&2
  exit "$preflight_exit"
fi

scp "${ssh_opts[@]}" -q "$archive" "$builder:$remote_archive"
remote=$(encode <<EOF
\$ErrorActionPreference = 'Stop'
if ((Get-FileHash -LiteralPath '$remote_archive' -Algorithm SHA256).Hash.ToLowerInvariant() -ne '$archive_sha256') {
  throw 'Staged archive SHA-256 mismatch'
}
[void](New-Item -ItemType Directory '$remote_root')
tar -xf '$remote_archive' -C '$remote_root'
if (\$LASTEXITCODE) { exit \$LASTEXITCODE }
Remove-Item -LiteralPath '$remote_archive'
& '$remote_root\\drivers\\apple-agx\\render-admission\\umd\\tests\\build-contract-test.ps1' \`
  -Configuration Release -Platform x64 -ResultRoot '$remote_root\\result'
exit \$LASTEXITCODE
EOF
)
set +e
ssh "${ssh_opts[@]}" "$builder" powershell -NoProfile -ExecutionPolicy Bypass \
  -EncodedCommand "$remote" > "$evidence_dir/remote-console.log" 2>&1
remote_exit=$?
for name in build.log run-stdout.log run-stderr.log; do
  scp "${ssh_opts[@]}" -q "$builder:$remote_root/result/$name" "$evidence_dir/$name" 2>/dev/null
done
set -e

# MSBuild repeats each error in its summary; print every distinct line once,
# relative to the remote tree.
remote_root_win=${remote_root//\//\\}
sed_root=$(printf '%s' "$remote_root_win" | sed 's/\\/\\\\/g')
grep -a -E ': (fatal )?error |"(BuildExitCode|ExitCode|ExitStatus|StderrLines|CheckFailures|SHA256|Result)"' \
  "$evidence_dir/remote-console.log" | tr -d '\r' |
  sed -e 's/^ *//' -e 's/^[0-9]*>//' -e "s|${sed_root}\\\\||g" -e 's/ \[[^]]*\.vcxproj\]$//' |
  awk '!seen[$0]++' || true
# Every contract suite reports its failures on stderr (CHECK_FAIL, COMPOSER,
# RUNTIME_OWNER, assert); print them in order.
if [[ -s "$evidence_dir/run-stderr.log" ]]; then
  echo "--- test stderr ($(wc -l < "$evidence_dir/run-stderr.log" | tr -d ' ') lines, first 200)"
  tr -d '\r' < "$evidence_dir/run-stderr.log" | head -n 200
  echo "---"
fi
echo "source: $source_commit (HEAD $head_commit, worktree snapshot: $worktree)"
echo "evidence: $evidence_dir"
echo "builder: $remote_root"
if [[ $remote_exit -eq 0 ]] && grep -a -q '"Result": *"PASS"' "$evidence_dir/remote-console.log"; then
  cleanup=$(printf "Remove-Item -LiteralPath '%s' -Recurse -Force\n" "$remote_root" | encode)
  ssh "${ssh_opts[@]}" "$builder" powershell -NoProfile -ExecutionPolicy Bypass \
    -EncodedCommand "$cleanup" > "$evidence_dir/cleanup.log" 2>&1 ||
    echo "warning: could not remove $remote_root (see cleanup.log)" >&2
  echo "UMD contract test: PASS"
  exit 0
fi
echo "UMD contract test: FAIL (remote exit $remote_exit)"
exit 1
