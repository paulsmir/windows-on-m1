param([string]$Root = 'C:\Users\pauls\AD04-util-lut2', [switch]$Arm64)
$ErrorActionPreference = 'Stop'
$mesa = 'C:\Users\pauls\AD04-d3d10-frontend-build\mesa'
$sourceCompiler = "$mesa\src\asahi\compiler"
$derivedCompiler = Join-Path $Root 'compiler'
$generated = 'C:\Users\pauls\AD04-asahi-windows-compiler\b5\generated'
$clang = 'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin\clang-cl.exe'
$rootInput = 'C:\Users\pauls\ad04-util-lut2-inputs'
if (Test-Path $Root) { throw "refusing to reuse existing output root: $Root" }
New-Item -ItemType Directory -Force $derivedCompiler, (Join-Path $Root 'util') | Out-Null
Copy-Item "$sourceCompiler\*" $derivedCompiler -Recurse -Force
Copy-Item "$rootInput\agx_compiler.h" (Join-Path $Root 'compiler\agx_compiler.h') -Force
Copy-Item "$rootInput\agx_pack.c" (Join-Path $Root 'compiler\agx_pack.c') -Force
Copy-Item "$rootInput\lut.h" (Join-Path $Root 'util\lut.h') -Force
$include = @('/I', $Root, '/I', 'C:\Users\pauls\AD04-uatomic\include', '/I', "$mesa\include", '/I', "$Root\compiler", '/I', "$mesa\src", '/I', "$mesa\src\compiler", '/I', "$mesa\src\compiler\nir", '/I', "$mesa\src\asahi\compiler", '/I', "$mesa\src\asahi\libagx", '/I', "$generated\src", '/I', "$generated\src\compiler", '/I', "$generated\src\compiler\nir", '/I', "$generated\src\asahi\compiler", '/I', "$generated\src\asahi\libagx")
$target = if ($Arm64) { @('/clang:--target=aarch64-pc-windows-msvc') } else { @() }
$args = @('/nologo', '/c', '/std:c11', '/W3', '/DNDEBUG', '/DHAVE_STRUCT_TIMESPEC', '/DMESA_DEBUG=0', '/DBUILDING_MESA', '/FI', 'C:\Users\pauls\AD04-uatomic\uatomic_clang_compat.h') + $include + @((Join-Path $derivedCompiler 'agx_compile.c')) + $target
$process = Start-Process -FilePath $clang -ArgumentList $args -Wait -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $Root 'stdout.log') -RedirectStandardError (Join-Path $Root 'stderr.log')
Get-Content (Join-Path $Root 'stdout.log'), (Join-Path $Root 'stderr.log') | Set-Content (Join-Path $Root 'compile.log')
Write-Output "COMPILE=$($process.ExitCode)"
exit $process.ExitCode
