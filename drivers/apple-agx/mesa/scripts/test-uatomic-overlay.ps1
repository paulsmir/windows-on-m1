param([string]$Root = 'C:\Users\pauls\AD04-uatomic')
$ErrorActionPreference = 'Continue'
$clang = 'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin\clang-cl.exe'
$overlay = Join-Path $Root 'uatomic_clang_compat.h'
$sources = @('uatomic_semantics_test.c', 'uatomic_mesa_semantics_test.c')
$compile = 0
$run = 0
foreach ($name in $sources) {
   $source = Join-Path $Root $name
   $exe = Join-Path $Root ($name -replace '\.c$', '.exe')
   $args = @('/nologo', '/W3', '/std:c11', '/FI', $overlay, '/I', 'C:\Users\pauls\AD04-d3d10-frontend-build\mesa\src', '/I', 'C:\Users\pauls\AD04-d3d10-frontend-build\mesa\include', $source, "/Fe:$exe")
   & $clang @args
   if ($LASTEXITCODE -ne 0) { $compile = $LASTEXITCODE; break }
   & $exe
   if ($LASTEXITCODE -ne 0) { $run = $LASTEXITCODE; break }
}
Write-Output "COMPILE=$compile"
Write-Output "RUN=$run"
if ($compile -ne 0 -or $run -ne 0) { exit 1 }
