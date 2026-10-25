param([string]$Root = '', [switch]$Arm64)
$builderRoot = if ($env:APPLE_AGX_BUILDER_ROOT) { $env:APPLE_AGX_BUILDER_ROOT } else { 'C:\Users\pauls\APB12E~1\active' }  # builder root: APPLE AGX\active (8.3 alias, no space)
if (!$Root) { $Root = "$builderRoot\AD04-util-lut2" }
$ErrorActionPreference = 'Stop'
$mesa = "$builderRoot\AD04-d3d10-frontend-build\mesa"
$sourceCompiler = "$mesa\src\asahi\compiler"
$derivedCompiler = Join-Path $Root 'compiler'
$generated = "$builderRoot\AD04-asahi-windows-compiler\b5\generated"
$clang = "$builderRoot\AD04-asahi-windows-compiler\llvm20\bin\clang-cl.exe"
$rootInput = "$builderRoot\ad04-util-lut2-inputs"
if (Test-Path $Root) { throw "refusing to reuse existing output root: $Root" }
New-Item -ItemType Directory -Force $derivedCompiler, (Join-Path $Root 'util') | Out-Null
Copy-Item "$sourceCompiler\*" $derivedCompiler -Recurse -Force
Copy-Item "$rootInput\agx_compiler.h" (Join-Path $Root 'compiler\agx_compiler.h') -Force
Copy-Item "$rootInput\agx_pack.c" (Join-Path $Root 'compiler\agx_pack.c') -Force
Copy-Item "$rootInput\lut.h" (Join-Path $Root 'util\lut.h') -Force
$include = @('/I', $Root, '/I', "$builderRoot\AD04-uatomic\include", '/I', "$mesa\include", '/I', "$Root\compiler", '/I', "$mesa\src", '/I', "$mesa\src\compiler", '/I', "$mesa\src\compiler\nir", '/I', "$mesa\src\asahi\compiler", '/I', "$mesa\src\asahi\libagx", '/I', "$generated\src", '/I', "$generated\src\compiler", '/I', "$generated\src\compiler\nir", '/I', "$generated\src\asahi\compiler", '/I', "$generated\src\asahi\libagx")
$target = if ($Arm64) { @('/clang:--target=aarch64-pc-windows-msvc') } else { @() }
$args = @('/nologo', '/c', '/std:c11', '/W3', '/DNDEBUG', '/DHAVE_STRUCT_TIMESPEC', '/DMESA_DEBUG=0', '/DBUILDING_MESA', '/FI', "$builderRoot\AD04-uatomic\uatomic_clang_compat.h") + $include + @((Join-Path $derivedCompiler 'agx_compile.c')) + $target
$process = Start-Process -FilePath $clang -ArgumentList $args -Wait -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $Root 'stdout.log') -RedirectStandardError (Join-Path $Root 'stderr.log')
Get-Content (Join-Path $Root 'stdout.log'), (Join-Path $Root 'stderr.log') | Set-Content (Join-Path $Root 'compile.log')
Write-Output "COMPILE=$($process.ExitCode)"
exit $process.ExitCode
