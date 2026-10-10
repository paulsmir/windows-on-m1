#!/bin/bash
# Builder operations for one experiment directory (same name on host and builder).
#   builder.sh build <dir>            upload build-kmd.ps1 + frozen source, build (long: run in background)
#   builder.sh copy <old dir> <dir>   reuse an unchanged package: copy its artifacts and receipt
#   builder.sh kill <dir>             stop a build of <dir> (processes whose command line names it)
# Fetching the result into the experiment is prepare.sh's first step.
set -eu
. "$(dirname "$0")/../lib.sh"
cmd=$1; shift
case "$cmd" in
build)
  D=$1; N=$AGX_EXPERIMENTS/$D
  for f in build-kmd.ps1 source-manifest.json source.zip; do [ -f "$N/$f" ] || { echo "missing $N/$f (package_source.py)"; exit 1; }; done
  builder "powershell -NoProfile -Command \"Remove-Item -Recurse -Force $BH_WIN\\$D -EA 0; New-Item -ItemType Directory -Force $BH_WIN\\$D | Out-Null\""
  builder_put "$N/build-kmd.ps1" "$N/source-manifest.json" "$N/source.zip" "$D"
  builder "powershell -NoProfile -ExecutionPolicy Bypass -File $BH_WIN\\$D\\build-kmd.ps1" 2>&1 \
    | grep -v '^\s*$' | tee "$N/builder-console.log" | tail -1
  ;;
copy)
  S=$1; D=$2
  builder "powershell -NoProfile -Command \"\$s='$BH_WIN\\$S'; \$d='$BH_WIN\\$D'; New-Item -ItemType Directory -Force \$d | Out-Null; foreach(\$f in 'AppleAgxRenderAdmission.inf','AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll','appleagxrenderadmission.cat','kmd-build-receipt.json','arm64-build.log'){ Copy-Item (Join-Path \$s \$f) (Join-Path \$d \$f) -Force }; Get-ChildItem \$d | ForEach-Object { \$_.Name + ' ' + (Get-FileHash \$_.FullName).Hash.Substring(0,16) }\"" | tr -d '\r'
  ;;
kill)
  D=$1
  builder "powershell -NoProfile -Command \"Get-CimInstance Win32_Process | Where-Object { \$_.CommandLine -match '$D' } | ForEach-Object { 'kill ' + \$_.ProcessId + ' ' + \$_.Name; Stop-Process -Id \$_.ProcessId -Force -EA 0 }; Start-Sleep 3; 'remaining build processes: ' + (Get-CimInstance Win32_Process | Where-Object { \$_.Name -match 'MSBuild|cl.exe|link.exe|python' } | Measure-Object).Count\"" | tr -d '\r'
  ;;
*) echo "usage: builder.sh build|copy|kill ..."; exit 2 ;;
esac
