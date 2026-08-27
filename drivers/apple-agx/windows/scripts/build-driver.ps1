param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug",
    [switch]$PowerQualification,
    [switch]$MmioQualification,
    [switch]$LifecycleQualification,
    [switch]$FirmwareQualification,
    [switch]$PoweredStatusQualification,
    [switch]$RtkitQualification,
    [switch]$UatSnapshotQualification,
    [switch]$Wddm26AbiQualification
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$project = Join-Path $root "AppleAgx.vcxproj"
$qualification = if ($PowerQualification) { "true" } else { "false" }
$mmioQualification = if ($MmioQualification) { "true" } else { "false" }
$lifecycleQualification = if ($LifecycleQualification) { "true" } else { "false" }
$firmwareQualification = if ($FirmwareQualification) { "true" } else { "false" }
$poweredStatusQualification = if ($PoweredStatusQualification) { "true" } else { "false" }
$rtkitQualification = if ($RtkitQualification) { "true" } else { "false" }
$uatSnapshotQualification = if ($UatSnapshotQualification) { "true" } else { "false" }
$wddm26AbiQualification = if ($Wddm26AbiQualification) { "true" } else { "false" }

& msbuild $project /m /t:Clean,Build "/p:Configuration=$Configuration" `
    /p:Platform=ARM64 /p:RunCodeAnalysis=true `
    "/p:AppleAgxPowerQualification=$qualification" `
    "/p:AppleAgxMmioQualification=$mmioQualification" `
    "/p:AppleAgxLifecycleQualification=$lifecycleQualification" `
    "/p:AppleAgxFirmwareQualification=$firmwareQualification" `
    "/p:AppleAgxPoweredStatusQualification=$poweredStatusQualification" `
    "/p:AppleAgxRtkitQualification=$rtkitQualification" `
    "/p:AppleAgxUatSnapshotQualification=$uatSnapshotQualification" `
    "/p:AppleAgxWddm26AbiQualification=$wddm26AbiQualification"
if ($LASTEXITCODE -ne 0) {
    throw "AppleAgx ARM64 WDK build failed with exit code $LASTEXITCODE"
}
