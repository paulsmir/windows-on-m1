param([string]$Root = 'C:\Users\pauls\AD04-lut')
$ErrorActionPreference = 'Continue'
$clang = 'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin\clang-cl.exe'
$source = Join-Path $Root 'lut_feature_probe.c'
$results = Join-Path $Root 'results'
New-Item -ItemType Directory -Force $results | Out-Null
& $clang /nologo /W3 /std:c11 $source "/Fe:$(Join-Path $results 'x64.exe')" *> (Join-Path $results 'x64.log')
$x64Compile = $LASTEXITCODE
$x64Run = -1
if ($x64Compile -eq 0) { & (Join-Path $results 'x64.exe'); $x64Run = $LASTEXITCODE }
Write-Output "X64_COMPILE=$x64Compile RUN=$x64Run"
& $clang '--target=aarch64-pc-windows-msvc' /nologo /W3 /std:c11 /c $source "/Fo:$(Join-Path $results 'arm64.obj')" *> (Join-Path $results 'arm64.log')
Write-Output "ARM64_COMPILE=$LASTEXITCODE"
if ($x64Compile -ne 0 -or $x64Run -ne 0 -or $LASTEXITCODE -ne 0) { exit 1 }
