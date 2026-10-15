# Build the ARM64 Mesa tree configured by configure-arm64-nir.ps1.
# Cross-built executables are linked but cannot run on the x64 builder.
param([string]$Name='nir-arm64')
$ErrorActionPreference='Stop'
$root='C:\Users\pauls\AD04-fullcompiler-001'
$tool='C:\VS2022Community\VC\Tools\MSVC\14.44.35207'
$kit='C:\Program Files (x86)\Windows Kits\10'
$clang='C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin'
$env:PATH="$root\winflex;$root\venv\Scripts;$clang;$tool\bin\HostX64\x64;"+$env:PATH
$env:INCLUDE="$tool\include;$kit\Include\10.0.26100.0\ucrt;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um"
$env:LIB="$tool\lib\arm64;$kit\Lib\10.0.26100.0\ucrt\arm64;$kit\Lib\10.0.26100.0\um\arm64"
$log="$root\$Name\ninja-build.log"
& "$root\venv\Scripts\ninja.exe" -C "$root\$Name" -k 0 *> $log
$code=$LASTEXITCODE
"NINJA_EXIT=$code"
Select-String -Path $log -Pattern 'error:|FAILED:' | Select -First 20 | %{ $_.Line.Substring(0,[Math]::Min(300,$_.Line.Length)) }
Get-Content $log -Tail 3
exit $code
