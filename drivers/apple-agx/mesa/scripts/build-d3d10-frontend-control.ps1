param([ValidateSet('b2','b3','b4')][string]$Attempt='b2', [ValidateSet('x64','ARM64')][string]$Architecture='x64')
$ErrorActionPreference = 'Stop'
$root = 'C:\Users\pauls\AD04-d3d10-frontend-build'
$mesa = Join-Path $root 'mesa'
$output = Join-Path $root "$Architecture-$Attempt"
$archive = Join-Path $root 'mesa-source.tar.gz'
if ((Get-FileHash $archive -Algorithm SHA256).Hash -ne '780C2AAF500803A109B9B41BD5E3D49D1100B7CBBAC46A79181EA31EE13192C9') { throw 'pinned Mesa archive mismatch' }
if (Test-Path $output) { throw 'preserve previous build output' }
New-Item -ItemType Directory -Force $mesa,$output | Out-Null
tar -xf $archive -C $mesa
if ($LASTEXITCODE) { throw 'source extraction failed' }
$tool = 'C:\VS2022Community\VC\Tools\MSVC\14.44.35207'
$kit = 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0'
$compilerBin = if ($Architecture -eq 'ARM64') { "$tool\bin\HostX86\arm64" } else { "$tool\bin\HostX64\x64" }
$env:PATH = "$compilerBin;" + $env:PATH
$env:INCLUDE = "$tool\include;$kit\ucrt;$kit\shared;$kit\um;$kit\km"
$generated = 'C:\Users\pauls\AD03-pipe-screen\.local\accelerated-desktop-ad03\mesa-build\src'
if (-not (Test-Path $generated)) { throw 'proven generated Mesa headers unavailable' }
$include = @("/I$mesa\include","/I$mesa\include\winddk","/I$mesa\src","/I$mesa\src\gallium\include","/I$mesa\src\gallium\auxiliary","/I$generated")
$frontend = Join-Path $mesa 'src\gallium\frontends\d3d10umd'
$cpp = @('Adapter','Debug','Device','Draw','DxgiFns','Format','InputAssembly','OutputMerger','Query','Rasterizer','Resource','Shader','ShaderDump') | ForEach-Object { Join-Path $frontend ($_ + '.cpp') }
# D3DKMT.cpp software device shims and d3d10_gdi.c software target are excluded.
$c = @('ShaderParse','ShaderTGSI') | ForEach-Object { Join-Path $frontend ($_ + '.c') }
Push-Location $output
try {
    & "$compilerBin\cl.exe" /nologo /c /std:c++17 /W3 /DNDEBUG /DMESA_DEBUG=0 @include @cpp *>&1 | Tee-Object 'cpp-build.log'
    if ($LASTEXITCODE) { throw 'frontend C++ compilation failed' }
    & "$compilerBin\cl.exe" /nologo /c /std:c11 /W3 /DNDEBUG /DMESA_DEBUG=0 @include @c *>&1 | Tee-Object 'c-build.log'
    if ($LASTEXITCODE) { throw 'frontend C compilation failed' }
    $objects = @(Get-ChildItem '*.obj' | ForEach-Object FullName)
    & "$compilerBin\lib.exe" /nologo '/OUT:MesaD3D10Frontend.lib' @objects *>&1 | Tee-Object 'lib-build.log'
    if ($LASTEXITCODE) { throw 'static library build failed' }
    Get-FileHash 'MesaD3D10Frontend.lib' -Algorithm SHA256 | ConvertTo-Json
} finally { Pop-Location }
