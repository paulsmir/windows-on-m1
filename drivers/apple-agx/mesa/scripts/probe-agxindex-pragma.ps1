param([string]$Root = 'C:\Users\pauls\AD04-agxindex-pragma')
$ErrorActionPreference = 'Continue'
$clang = 'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin\clang-cl.exe'
$source = Join-Path $Root 'agxindex_pragma_probe.c'
$results = Join-Path $Root 'results'
New-Item -ItemType Directory -Force $results | Out-Null

function X64([int]$Pragma) {
   $tag = if ($Pragma) { 'pragma-x64' } else { 'default-x64' }
   $exe = Join-Path $results "$tag.exe"
   & $clang /nologo /W3 /std:c11 "/DPROBE_PRAGMA=$Pragma" $source "/Fe:$exe" *> (Join-Path $results "$tag.log")
   $compile = $LASTEXITCODE
   $run = -1
   if ($compile -eq 0) { & $exe; $run = $LASTEXITCODE }
   Write-Output "$tag COMPILE=$compile RUN=$run"
}

function Arm64([int]$Pragma) {
   $tag = if ($Pragma) { 'pragma-arm64' } else { 'default-arm64' }
   $obj = Join-Path $results "$tag.obj"
   & $clang '--target=aarch64-pc-windows-msvc' /nologo /W3 /std:c11 "/DPROBE_PRAGMA=$Pragma" $source /c "/Fo:$obj" *> (Join-Path $results "$tag.log")
   Write-Output "$tag COMPILE=$LASTEXITCODE"
}

X64 0
X64 1
Arm64 0
Arm64 1
