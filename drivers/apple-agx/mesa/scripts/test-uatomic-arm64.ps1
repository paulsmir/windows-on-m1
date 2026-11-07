param([string]$Root = '')
$builderRoot = if ($env:APPLE_AGX_BUILDER_ROOT) { $env:APPLE_AGX_BUILDER_ROOT } else { 'C:\Users\pauls\APB12E~1\active' }  # builder root: APPLE AGX\active (8.3 alias, no space)
if (!$Root) { $Root = "$builderRoot\AD04-uatomic" }
$clang = "$builderRoot\AD04-asahi-windows-compiler\llvm20\bin\clang-cl.exe"
$overlay = Join-Path $Root 'uatomic_clang_compat.h'
$source = Join-Path $Root 'uatomic_semantics_test.c'
$object = Join-Path $Root 'uatomic-arm64.obj'
$args = @('--target=aarch64-pc-windows-msvc', '/nologo', '/W3', '/std:c11', '/FI', $overlay, $source, '/c', "/Fo:$object")
$process = Start-Process -FilePath $clang -ArgumentList $args -Wait -PassThru -NoNewWindow -RedirectStandardOutput (Join-Path $Root 'arm64.stdout.log') -RedirectStandardError (Join-Path $Root 'arm64.stderr.log')
Write-Output "ARM64_COMPILE=$($process.ExitCode)"
exit $process.ExitCode
