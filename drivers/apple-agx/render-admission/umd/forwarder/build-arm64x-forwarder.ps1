# EXP1820: the ARM64X UMD forwarder. x64 processes on ARM64 Windows load the
# UMD named by UserModeDriverName from System32 like native ones (EXP1800);
# an ARM64 PE cannot load into them. This DLL carries no code: its native
# exports forward to AppleAgxRenderAdmissionUmdArm64.dll and its x64/EC
# exports to AppleAgxRenderAdmissionUmdX64.dll (loader behaviour: EXP1805).
# Run inside VsDevCmd -arch=arm64 -host_arch=x64. Prints FORWARDER_PASS <sha256>.
param(
  [Parameter(Mandatory = $true)][string]$Out,
  [Parameter(Mandatory = $true)][int]$PackageBuild
)
$ErrorActionPreference = 'Stop'
if ($env:VSCMD_ARG_TGT_ARCH -ne 'arm64') { throw 'run inside VsDevCmd -arch=arm64' }
$here = $PSScriptRoot
New-Item -ItemType Directory -Force $Out | Out-Null
$dll = Join-Path $Out 'AppleAgxRenderAdmissionUmd.dll'
# Native tools report through stderr and exit codes; judge the exit codes.
$ErrorActionPreference = 'Continue'
& cl.exe /nologo /c "$here\empty.cpp" "/Fo$Out\empty_arm64.obj" *> "$Out\compile-arm64.log"
if ($LASTEXITCODE) { throw 'ARM64 compile failed' }
& cl.exe /nologo /c /arm64EC "$here\empty.cpp" "/Fo$Out\empty_arm64ec.obj" *> "$Out\compile-arm64ec.log"
if ($LASTEXITCODE) { throw 'ARM64EC compile failed' }
# The package version resource: verify-package-version.ps1 requires the UMD
# name to carry the DriverVer (as the ARM64 UMD it replaces).
& rc.exe /nologo /DAPPLE_AGX_PACKAGE_DLL=1 "/DAPPLE_AGX_VERSION_BUILD=$PackageBuild" "/fo$Out\package-version.res" "$here\..\..\package-version.rc" *> "$Out\rc.log"
if ($LASTEXITCODE) { throw 'resource compile failed' }
& link.exe /lib /machine:arm64 "/def:$here\native.def" "/out:$Out\native.lib" *> "$Out\lib-arm64.log"
if ($LASTEXITCODE) { throw 'ARM64 import library failed' }
& link.exe /lib /machine:x64 "/def:$here\x64.def" "/out:$Out\x64.lib" *> "$Out\lib-x64.log"
if ($LASTEXITCODE) { throw 'x64 import library failed' }
& link.exe /nologo /dll /noentry /machine:arm64x "/defArm64Native:$here\native.def" "/def:$here\x64.def" `
  "$Out\empty_arm64.obj" "$Out\empty_arm64ec.obj" "$Out\package-version.res" "/out:$dll" "$Out\native.lib" "$Out\x64.lib" *> "$Out\link-arm64x.log"
if ($LASTEXITCODE) { Get-Content "$Out\link-arm64x.log" | Select -First 20; throw 'ARM64X link failed' }
& link.exe /dump /exports $dll *> "$Out\exports.log"
$ErrorActionPreference = 'Stop'
'FORWARDER_PASS ' + (Get-FileHash $dll).Hash.ToLowerInvariant()
