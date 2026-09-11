param([string]$Root = 'C:\Users\pauls\AD04-offt')
$ErrorActionPreference = 'Stop'
$clang = 'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin\clang-cl.exe'
$mesa = 'C:\Users\pauls\AD04-d3d10-frontend-build\mesa'
$source = Join-Path $Root 'offt_internal_probe.c'
foreach ($target in @('', '/clang:--target=aarch64-pc-windows-msvc')) {
   $tag = if ($target) { 'arm64' } else { 'x64' }
   $obj = Join-Path $Root "$tag.obj"
   $args = @('/nologo', '/W3', '/std:c11', '/I', "$mesa\include", '/I', "$mesa\src", $source, '/c', "/Fo:$obj")
   if ($target) { $args += $target }
   & $clang @args
   Write-Output "$tag COMPILE=$LASTEXITCODE"
   if ($LASTEXITCODE -ne 0) { exit 1 }
}
