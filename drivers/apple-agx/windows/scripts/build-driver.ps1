param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$PowerQualification,
    [switch]$MmioQualification,
    [switch]$LifecycleQualification
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$project = Join-Path $root "AppleAgx.vcxproj"
$qualification = if ($PowerQualification) { "true" } else { "false" }
$mmioQualification = if ($MmioQualification) { "true" } else { "false" }
$lifecycleQualification = if ($LifecycleQualification) { "true" } else { "false" }

& msbuild $project /m /t:Clean,Build "/p:Configuration=$Configuration" `
    /p:Platform=ARM64 /p:RunCodeAnalysis=true `
    "/p:AppleAgxPowerQualification=$qualification" `
    "/p:AppleAgxMmioQualification=$mmioQualification" `
    "/p:AppleAgxLifecycleQualification=$lifecycleQualification"
if ($LASTEXITCODE -ne 0) {
    throw "AppleAgx ARM64 WDK build failed with exit code $LASTEXITCODE"
}
