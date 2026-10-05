param([string]$Root = 'C:\Users\pauls\AD04-lut-guard')
$ErrorActionPreference = 'Continue'
$clang = 'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin\clang-cl.exe'
$mesa = 'C:\Users\pauls\AD04-d3d10-frontend-build\mesa'
$redRoot = Join-Path $Root 'red'
$greenRoot = Join-Path $Root 'green'
$redSource = Join-Path $redRoot 'lut_guard_probe.c'
$greenSource = Join-Path $greenRoot 'lut_guard_probe.c'
New-Item -ItemType Directory -Force $redRoot, $greenRoot | Out-Null
Copy-Item (Join-Path $Root 'lut_guard_probe.c') $redSource -Force
Copy-Item (Join-Path $Root 'lut_guard_probe.c') $greenSource -Force
$overlay = Join-Path $greenRoot 'util'
New-Item -ItemType Directory -Force $overlay | Out-Null
Copy-Item (Join-Path $Root 'util\lut.h') (Join-Path $overlay 'lut.h') -Force
& $clang /nologo /W3 /std:c11 /I "$mesa\src" $redSource "/Fe:$(Join-Path $redRoot 'red.exe')" *> (Join-Path $redRoot 'red.log')
$red = $LASTEXITCODE
& $clang /nologo /W3 /std:c11 /I $overlay /I "$mesa\src" $greenSource "/Fe:$(Join-Path $greenRoot 'green.exe')" *> (Join-Path $greenRoot 'green.log')
$green = $LASTEXITCODE
Write-Output "RED=$red GREEN=$green"
if ($red -eq 0 -or $green -ne 0) { exit 1 }
