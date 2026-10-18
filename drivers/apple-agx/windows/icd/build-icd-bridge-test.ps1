# Build and run the CS 1.6 ICD phase-1 host tests against the installed WDK:
# the GPUVA D3DKMT bridge test (x64, x86; the CS 1.6 ICD is an x86 DLL) and
# the KMD private-ABI layout test (also ARM64, compile-time and on a 64-bit
# host only as a build check). Prints one PASS line per test/architecture
# and the exe SHA-256. Compiled as C++ like the UMD (CompileAsCpp): d3d10.h
# constants are external definitions in C.
param([string[]]$Architectures = @('x64', 'x86'), [string]$Repository = '')
$ErrorActionPreference = 'Stop'
$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$here = Split-Path -Parent $MyInvocation.MyCommand.Path
if (-not $Repository) { $Repository = (Resolve-Path (Join-Path $here '..\..\..\..')).Path }
$includes = "/I`"$Repository\drivers\apple-agx\shared\include`" /I`"$Repository\drivers\apple-agx\render-admission\include`""
function Build([string]$vcvars, [string]$out, [string]$exe, [string]$sources) {
  New-Item -ItemType Directory -Force $out | Out-Null
  cmd /c "`"$vs\VC\Auxiliary\Build\vcvarsall.bat`" $vcvars >nul && cd /d `"$here`" && cl /nologo /TP /EHsc /W4 /WX /wd4201 /Zp8 /D_CRT_SECURE_NO_WARNINGS $includes $sources /Fo`"$out\\`" /Fe:`"$exe`""
  if ($LASTEXITCODE) { throw "build $exe failed $LASTEXITCODE" }
}
foreach ($arch in $Architectures) {
  $vcvars = if ($arch -eq 'x86') { 'x64_x86' } else { 'x64' }
  $out = Join-Path $here "out-$arch"
  $bridge = Join-Path $out 'agx_kmt_gpuva_bridge_test.exe'
  Build $vcvars $out $bridge 'agx_kmt_gpuva_bridge.c agx_kmt_gpuva_bridge_test.c'
  & $bridge; if ($LASTEXITCODE) { throw "bridge test $arch failed $LASTEXITCODE" }
  "$arch bridge exe " + (Get-FileHash $bridge).Hash
  $layout = Join-Path $out 'agx_abi_layout_test.exe'
  Build $vcvars $out $layout 'agx_abi_layout_test.c'
  & $layout; if ($LASTEXITCODE) { throw "layout test $arch failed $LASTEXITCODE" }
}
# ARM64 is the architecture the KMD was validated with: build only.
$out = Join-Path $here 'out-arm64'
Build 'x64_arm64' $out (Join-Path $out 'agx_abi_layout_test.exe') 'agx_abi_layout_test.c'
'arm64 layout build PASS'
