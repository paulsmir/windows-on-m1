param([string]$Root = 'C:\Users\pauls\AD04-agxindex')
$ErrorActionPreference = 'Continue'
$clang = 'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin\clang-cl.exe'
$source = Join-Path $Root 'agxindex_layout_probe.c'
$results = Join-Path $Root 'results'
New-Item -ItemType Directory -Force $results | Out-Null

function Compile-X64([int]$GccStruct) {
   $tag = if ($GccStruct) { 'gcc-x64' } else { 'default-x64' }
   $exe = Join-Path $results "$tag.exe"
   & $clang /nologo /W3 /std:c11 "/DPROBE_GCC_STRUCT=$GccStruct" $source "/Fe:$exe"
   $compile = $LASTEXITCODE
   $run = -1
   if ($compile -eq 0) { & $exe; $run = $LASTEXITCODE }
   Write-Output "$tag COMPILE=$compile RUN=$run"
   if ($compile -ne 0 -or ($GccStruct -and $run -ne 0)) { exit 1 }
}

function Compile-Arm64([int]$GccStruct) {
   $tag = if ($GccStruct) { 'gcc-arm64' } else { 'default-arm64' }
   $obj = Join-Path $results "$tag.obj"
   & $clang '--target=aarch64-pc-windows-msvc' /nologo /W3 /std:c11 "/DPROBE_GCC_STRUCT=$GccStruct" $source /c "/Fo:$obj"
   $compile = $LASTEXITCODE
   Write-Output "$tag COMPILE=$compile"
   if ($compile -ne 0) { exit 1 }
}

Compile-X64 0
Compile-X64 1
Compile-Arm64 0
Compile-Arm64 1
