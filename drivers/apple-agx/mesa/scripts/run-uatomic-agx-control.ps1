param([string]$Root = 'C:\Users\pauls\AD04-uatomic')
$ErrorActionPreference = 'Continue'
$clang = 'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin\clang-cl.exe'
$mesa = 'C:\Users\pauls\AD04-d3d10-frontend-build\mesa'
$generated = 'C:\Users\pauls\AD04-asahi-windows-compiler\b5\generated'
$overlay = Join-Path $Root 'uatomic_clang_compat.h'
$source = "$mesa\src\asahi\compiler\agx_compile.c"
$include = @('/I', "$Root\include", '/I', "$mesa\include", '/I', "$mesa\src", '/I', "$mesa\src\compiler", '/I', "$mesa\src\compiler\nir", '/I', "$mesa\src\asahi\compiler", '/I', "$mesa\src\asahi\libagx", '/I', "$generated\src", '/I', "$generated\src\compiler", '/I', "$generated\src\compiler\nir", '/I', "$generated\src\asahi\compiler", '/I', "$generated\src\asahi\libagx")
$args = @('/nologo', '/c', '/std:c11', '/W3', '/DNDEBUG', '/DHAVE_STRUCT_TIMESPEC', '/DMESA_DEBUG=0', '/DBUILDING_MESA', '/FI', $overlay) + $include + @($source)
$stdout = Join-Path $Root 'agx-control.stdout.log'
$stderr = Join-Path $Root 'agx-control.stderr.log'
$process = Start-Process -FilePath $clang -ArgumentList $args -Wait -PassThru -NoNewWindow -RedirectStandardOutput $stdout -RedirectStandardError $stderr
Get-Content $stdout, $stderr | Set-Content (Join-Path $Root 'agx-control.log')
Write-Output "COMPILE=$($process.ExitCode)"
exit $process.ExitCode
