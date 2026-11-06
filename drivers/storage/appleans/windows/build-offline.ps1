param(
    [string]$VisualStudioRoot,
    [switch]$NativeReadOnly,
    # ANS008: write-capable package (still writes only inside the broker's EL2 window).
    [switch]$NativeWrite,
    [ValidateRange(0,3)][int]$QualificationStop = 0,
    [string]$WdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10"
)
$ErrorActionPreference = 'Stop'
$kit = '10.0.26100.0'
if (-not (Test-Path (Join-Path $WdkRoot "Include\$kit\km\storport.h"))) {
    throw 'Pinned WDK 10.0.26100.0 storport.h is missing'
}
if ([string]::IsNullOrWhiteSpace($VisualStudioRoot)) {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
    $instances = & $vswhere -all -prerelease -products '*' -property installationPath
    $VisualStudioRoot = $instances | Where-Object {
        Test-Path (Join-Path $_ 'MSBuild\Microsoft\VC\v170\Platforms\ARM64\PlatformToolsets\WindowsKernelModeDriver10.0\Toolset.props')
    } | Select-Object -First 1
}
if ([string]::IsNullOrWhiteSpace($VisualStudioRoot)) {
    throw 'An existing Visual Studio ARM64 WDK toolset is required; no tools are installed by this script'
}
$msbuild = Join-Path $VisualStudioRoot 'MSBuild\Current\Bin\amd64\MSBuild.exe'
# Forward slashes prevent Windows PowerShell's trailing-backslash native-argument
# quoting issue. Per-build properties do not alter the host's global WDK selection.
$nativeProperty = '/p:AppleAnsNativeReadOnly=' + $NativeReadOnly.IsPresent.ToString().ToLowerInvariant()
if ($NativeWrite -and -not $NativeReadOnly) { throw '-NativeWrite requires the native profile (-NativeReadOnly)' }
$writeProperty = '/p:AppleAnsNativeWrite=' + $(if ($NativeWrite) { '1' } else { '0' })
$wdkProperty = '/p:WDKContentRoot=' + $WdkRoot.Replace('\','/').TrimEnd('/') + '/'
& $msbuild (Join-Path $PSScriptRoot 'AppleAns.vcxproj') /m:1 /t:Rebuild `
    /p:Configuration=Release /p:Platform=ARM64 /p:RunCodeAnalysis=true `
    /p:SignMode=Off $nativeProperty $writeProperty "/p:AppleAnsQualificationStop=$QualificationStop" $wdkProperty /p:WDKBuildFolder=10.0.26100.0 `
    /p:ImportDirectoryBuildProps=false /p:ImportDirectoryBuildTargets=false
if ($LASTEXITCODE -ne 0) { throw "ARM64 WDK build failed: $LASTEXITCODE" }
# Signing is separate and must use an explicitly selected existing certificate.
# This script never creates certificates, installs a package, or contacts a device.
