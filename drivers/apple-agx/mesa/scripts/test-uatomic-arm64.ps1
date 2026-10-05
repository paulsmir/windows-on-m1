param([string]$Root = 'C:\Users\pauls\AD04-uatomic')
$clang = 'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin\clang-cl.exe'
$overlay = Join-Path $Root 'uatomic_clang_compat.h'
$source = Join-Path $Root 'uatomic_semantics_test.c'
$object = Join-Path $Root 'uatomic-arm64.obj'
$args = @('--target=aarch64-pc-windows-msvc', '/nologo', '/W3', '/std:c11', '/FI', $overlay, $source, '/c', "/Fo:$object")
$process = Start-Process -FilePath $clang -ArgumentList $args -Wait -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $Root 'arm64.stdout.log') -RedirectStandardError (Join-Path $Root 'arm64.stderr.log')
Write-Output "ARM64_COMPILE=$($process.ExitCode)"
exit $process.ExitCode
