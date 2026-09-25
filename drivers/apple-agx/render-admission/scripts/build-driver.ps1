param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$MemoryQualification,
    [switch]$ManagementQualification,
    [switch]$RetainedRootQualification,
    [switch]$StopAfterEndpoints,
    [switch]$FirmwareQualification,
    [switch]$BackendQualification,
    [switch]$SubmitQualification,
    [switch]$GpuvaB1Qualification,
    [switch]$GpuvaG3Qualification,
    [switch]$BltProbeQualification,
    [switch]$VisibleScanoutQualification,
    [switch]$VisibleAgxQualification,
    [switch]$UmdAdmissionTrace,
    [switch]$NativeFrontend,
    [ValidateSet(0,16,64)]
    [int]$GpuvaG1bPageProfile = 0,
    [switch]$GpuvaG1bAllocationHint,
    [switch]$Incremental,
    [string]$SourceManifestPath,
    [string]$NativeRuntimeProps,
    [string]$MesaSourceRoot = 'C:\Users\pauls\AD04-d3d10-frontend-build\mesa',
    [string]$MesaGeneratedRoot = 'C:\Users\pauls\AD04-asahi-windows-compiler\b5\generated',
    [ValidateRange(0,65535)]
    [int]$PackageBuild = 461
)

$ErrorActionPreference = "Stop"
if ($GpuvaB1Qualification -and ($MemoryQualification -or $ManagementQualification -or $RetainedRootQualification -or $StopAfterEndpoints -or $FirmwareQualification -or $BackendQualification -or $SubmitQualification -or $VisibleScanoutQualification -or $VisibleAgxQualification -or $GpuvaG1bPageProfile -ne 0 -or $GpuvaG1bAllocationHint)) {
    throw "GpuvaB1Qualification requires a standalone WDDM3.0 physical candidate"
}
if ($GpuvaG3Qualification -and ($GpuvaB1Qualification -or $MemoryQualification -or $ManagementQualification -or $RetainedRootQualification -or $StopAfterEndpoints -or $FirmwareQualification -or $BackendQualification -or $SubmitQualification -or $VisibleScanoutQualification -or $VisibleAgxQualification -or $GpuvaG1bPageProfile -eq 0)) {
    throw "GpuvaG3Qualification requires a standalone WDDM3.2 GpuMmu candidate"
}
if ($BltProbeQualification -and -not $GpuvaG3Qualification) {
    throw "BltProbeQualification requires GpuvaG3Qualification"
}
if ($BackendQualification -and ($MemoryQualification -or $ManagementQualification -or $RetainedRootQualification -or $StopAfterEndpoints -or $FirmwareQualification)) {
    throw "BackendQualification must not be combined with an earlier terminal qualification profile"
}
if ($FirmwareQualification -and ($MemoryQualification -or $ManagementQualification -or $RetainedRootQualification -or $StopAfterEndpoints)) {
    throw "FirmwareQualification must not be combined with an earlier terminal qualification profile"
}
if ($SubmitQualification -and ($MemoryQualification -or $ManagementQualification -or $RetainedRootQualification -or $StopAfterEndpoints -or $FirmwareQualification -or $BackendQualification)) {
    throw "SubmitQualification must be a full-production-only discriminator"
}
if ($VisibleScanoutQualification -and ($MemoryQualification -or $ManagementQualification -or $RetainedRootQualification -or $StopAfterEndpoints -or $FirmwareQualification -or $BackendQualification -or $SubmitQualification)) {
    throw "VisibleScanoutQualification must be the only qualification profile"
}
if ($VisibleAgxQualification -and ($MemoryQualification -or $ManagementQualification -or $RetainedRootQualification -or $StopAfterEndpoints -or $FirmwareQualification -or $BackendQualification -or $SubmitQualification -or $VisibleScanoutQualification)) {
    throw "VisibleAgxQualification must be the only qualification profile"
}
$root = Split-Path -Parent $PSScriptRoot
if ($GpuvaG3Qualification) {
    if ([string]::IsNullOrWhiteSpace($SourceManifestPath)) {
        throw 'GpuvaG3Qualification requires a committed source manifest'
    }
    & (Join-Path $PSScriptRoot 'verify-committed-sources.ps1') `
        -ManifestPath $SourceManifestPath -RepositoryRoot (Resolve-Path (Join-Path $root '..\..\..')).Path
}
$project = Join-Path $root "AppleAgxRenderAdmission.vcxproj"
$umdProject = Join-Path $root "umd\AppleAgxRenderAdmissionUmd.vcxproj"
$msbuildCommand = Get-Command msbuild -ErrorAction SilentlyContinue
if ($null -ne $msbuildCommand) {
    $msbuild = $msbuildCommand.Source
} else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "MSBuild is not on PATH and vswhere.exe was not found"
    }
    $msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild `
        -find "MSBuild\**\Bin\amd64\MSBuild.exe" | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($msbuild)) {
        throw "A Visual Studio installation with ARM64 MSBuild support was not found"
    }
}

$umdAdmissionTraceValue = if ($UmdAdmissionTrace) { "true" } else { "false" }
$nativeFrontendValue = if ($NativeFrontend) { "true" } else { "false" }
if ($NativeFrontend -and
    ([string]::IsNullOrWhiteSpace($NativeRuntimeProps) -or
     -not (Test-Path -LiteralPath $NativeRuntimeProps) -or
     -not (Test-Path -LiteralPath $MesaSourceRoot) -or
     -not (Test-Path -LiteralPath $MesaGeneratedRoot))) {
    throw "NativeFrontend requires existing NativeRuntimeProps, MesaSourceRoot, and MesaGeneratedRoot"
}
$pinnedWindowsSdkDir = 'C:/Program Files (x86)/Windows Kits/10/'
$pinnedWdkProperties = @(
    '/p:WindowsTargetPlatformVersion=10.0.26100.0',
    "/p:WDKContentRoot=$pinnedWindowsSdkDir",
    "/p:WindowsSdkDir=$pinnedWindowsSdkDir",
    "/p:UniversalCRTSdkDir=$pinnedWindowsSdkDir",
    ("/p:UniversalCRT_IncludePath={0}Include/10.0.26100.0/ucrt" -f $pinnedWindowsSdkDir),
    ("/p:UniversalCRT_LibraryPath_arm64={0}Lib/10.0.26100.0/ucrt/arm64" -f $pinnedWindowsSdkDir),
    ("/p:WindowsSDK_LibraryPath_ARM64={0}Lib/10.0.26100.0/um/arm64" -f $pinnedWindowsSdkDir)
)
$buildTarget = if ($Incremental) { '/t:Build' } else { '/t:Clean,Build' }
& $msbuild $umdProject /m $buildTarget "/p:Configuration=$Configuration" `
    /p:Platform=ARM64 /p:RunCodeAnalysis=true "/p:AppleAgxVersionBuild=$PackageBuild" `
    "/p:AppleAgxUmdAdmissionTrace=$umdAdmissionTraceValue" `
    "/p:EnableNativeFrontend=$nativeFrontendValue" `
    "/p:NativeRuntimeProps=$NativeRuntimeProps" `
    "/p:MesaSourceRoot=$MesaSourceRoot" "/p:MesaGeneratedRoot=$MesaGeneratedRoot" `
    @pinnedWdkProperties
if ($LASTEXITCODE -ne 0) {
    throw "Clean render-admission ARM64 UMD build failed with exit code $LASTEXITCODE"
}

$umd = Join-Path $root "umd\ARM64\$Configuration\AppleAgxRenderAdmissionUmd.dll"
if (-not (Test-Path $umd)) {
    throw "Clean render-admission ARM64 UMD output was not found: $umd"
}
Copy-Item -Force $umd (Join-Path $root "AppleAgxRenderAdmissionUmd.dll")

$memoryQualificationValue = if ($MemoryQualification) { "true" } else { "false" }
$managementQualificationValue = if ($ManagementQualification) { "true" } else { "false" }
$retainedRootValue = if ($RetainedRootQualification) { "true" } else { "false" }
$endpointStopValue = if ($StopAfterEndpoints) { "true" } else { "false" }
$firmwareQualificationValue = if ($FirmwareQualification) { "true" } else { "false" }
$backendQualificationValue = if ($BackendQualification) { "true" } else { "false" }
$submitQualificationValue = if ($SubmitQualification) { "true" } else { "false" }
$gpuvaB1QualificationValue = if ($GpuvaB1Qualification) { "true" } else { "false" }
$gpuvaG3QualificationValue = if ($GpuvaG3Qualification) { "true" } else { "false" }
$bltProbeQualificationValue = if ($BltProbeQualification) { "true" } else { "false" }
$visibleScanoutQualificationValue = if ($VisibleScanoutQualification) { "true" } else { "false" }
$visibleAgxQualificationValue = if ($VisibleAgxQualification) { "true" } else { "false" }
$gpuvaG1bAllocationHintValue = if ($GpuvaG1bAllocationHint) { "1" } else { "0" }
& $msbuild $project /m $buildTarget "/p:Configuration=$Configuration" `
    /p:Platform=ARM64 /p:RunCodeAnalysis=true /p:Inf2CatUseLocalTime=true `
    "/p:AppleAgxMemoryQualification=$memoryQualificationValue" "/p:AppleAgxVersionBuild=$PackageBuild" `
    "/p:AppleAgxManagementQualification=$managementQualificationValue" `
    "/p:AppleAgxRetainedRootQualification=$retainedRootValue" `
    "/p:AppleAgxStopAfterEndpoints=$endpointStopValue" `
    "/p:AppleAgxFirmwareQualification=$firmwareQualificationValue" `
    "/p:AppleAgxBackendQualification=$backendQualificationValue" `
    "/p:AppleAgxSubmitQualification=$submitQualificationValue" `
    "/p:AppleAgxGpuvaB1Qualification=$gpuvaB1QualificationValue" `
    "/p:AppleAgxGpuvaG3Qualification=$gpuvaG3QualificationValue" `
    "/p:AppleAgxBltProbeQualification=$bltProbeQualificationValue" `
    "/p:AppleAgxVisibleScanoutQualification=$visibleScanoutQualificationValue" `
    "/p:AppleAgxVisibleAgxQualification=$visibleAgxQualificationValue" `
    "/p:AppleAgxGpuvaG1bPageProfile=$GpuvaG1bPageProfile" `
    "/p:AppleAgxGpuvaG1bAllocationHint=$gpuvaG1bAllocationHintValue" `
    @pinnedWdkProperties
if ($LASTEXITCODE -ne 0) {
    throw "Clean render-admission ARM64 WDK build failed with exit code $LASTEXITCODE"
}

$package = if ($MemoryQualification) {
    Join-Path $root "build\memory-qualification\$Configuration\AppleAgxRenderAdmission"
} else {
    Join-Path $root "ARM64\$Configuration\AppleAgxRenderAdmission"
}
& (Join-Path $PSScriptRoot 'verify-package-version.ps1') -PackageDirectory $package
