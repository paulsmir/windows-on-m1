param([string]$Root = 'C:\Users\pauls\AD04-agxindex-derived')
$ErrorActionPreference = 'Stop'
$mesa = 'C:\Users\pauls\AD04-d3d10-frontend-build\mesa'
$sourceCompiler = "$mesa\src\asahi\compiler"
$derivedCompiler = Join-Path $Root 'compiler'
$generated = 'C:\Users\pauls\AD04-asahi-windows-compiler\b5\generated'
$clang = 'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin\clang-cl.exe'
$fourcc = 'C:\Users\pauls\AD04-uatomic\include'
$uatomic = 'C:\Users\pauls\AD04-uatomic\uatomic_clang_compat.h'
$overlayInput = 'C:\Users\pauls\agx_compiler_derived_input.h'
$overlay = Join-Path $Root 'compiler\agx_compiler.h'
$source = Join-Path $derivedCompiler 'agx_compile.c'
if (Test-Path $Root) { Remove-Item -Recurse -Force $Root }
New-Item -ItemType Directory -Force $derivedCompiler | Out-Null
Copy-Item "$sourceCompiler\*" $derivedCompiler -Recurse -Force
if (!(Test-Path $overlayInput)) { throw 'derived agx_compiler.h overlay input missing' }
Copy-Item $overlayInput $overlay -Force
$include = @('/I', $fourcc, '/I', "$Root\compiler", '/I', "$mesa\include", '/I', "$mesa\src", '/I', "$mesa\src\compiler", '/I', "$mesa\src\compiler\nir", '/I', "$mesa\src\asahi\compiler", '/I', "$mesa\src\asahi\libagx", '/I', "$generated\src", '/I', "$generated\src\compiler", '/I', "$generated\src\compiler\nir", '/I', "$generated\src\asahi\compiler", '/I', "$generated\src\asahi\libagx")
$args = @('/nologo', '/c', '/std:c11', '/W3', '/DNDEBUG', '/DHAVE_STRUCT_TIMESPEC', '/DMESA_DEBUG=0', '/DBUILDING_MESA', '/FI', $uatomic) + $include + @($source)
$out = Join-Path $Root 'compile.log'
$process = Start-Process -FilePath $clang -ArgumentList $args -Wait -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $Root 'stdout.log') -RedirectStandardError (Join-Path $Root 'stderr.log')
Get-Content (Join-Path $Root 'stdout.log'), (Join-Path $Root 'stderr.log') | Set-Content $out
Write-Output "COMPILE=$($process.ExitCode)"
exit $process.ExitCode
