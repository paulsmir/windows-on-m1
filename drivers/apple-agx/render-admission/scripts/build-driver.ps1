param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Release",
    [switch]$MemoryQualification,
    [switch]$ManagementQualification,
    [switch]$RetainedRootQualification,
    [switch]$StopAfterEndpoints,
    [switch]$FirmwareQualification,
    [switch]$BackendQualification,
    [ValidateRange(0,65535)]
    [int]$PackageBuild = 461
)

$ErrorActionPreference = "Stop"
if ($BackendQualification -and ($MemoryQualification -or $ManagementQualification -or $RetainedRootQualification -or $StopAfterEndpoints -or $FirmwareQualification)) {
    throw "BackendQualification must not be combined with an earlier terminal qualification profile"
}
if ($FirmwareQualification -and ($MemoryQualification -or $ManagementQualification -or $RetainedRootQualification -or $StopAfterEndpoints)) {
    throw "FirmwareQualification must not be combined with an earlier terminal qualification profile"
}
$root = Split-Path -Parent $PSScriptRoot
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
    $msbuild = & $vswhere -latest -prerelease -products * -requires Microsoft.Component.MSBuild `
        -find "MSBuild\**\Bin\amd64\MSBuild.exe" | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($msbuild)) {
        throw "A Visual Studio installation with ARM64 MSBuild support was not found"
    }
}

& $msbuild $umdProject /m /t:Clean,Build "/p:Configuration=$Configuration" `
    /p:Platform=ARM64 /p:RunCodeAnalysis=true "/p:AppleAgxVersionBuild=$PackageBuild"
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
& $msbuild $project /m /t:Clean,Build "/p:Configuration=$Configuration" `
    /p:Platform=ARM64 /p:RunCodeAnalysis=true /p:Inf2CatUseLocalTime=true `
    "/p:AppleAgxMemoryQualification=$memoryQualificationValue" "/p:AppleAgxVersionBuild=$PackageBuild" `
    "/p:AppleAgxManagementQualification=$managementQualificationValue" `
    "/p:AppleAgxRetainedRootQualification=$retainedRootValue" `
    "/p:AppleAgxStopAfterEndpoints=$endpointStopValue" `
    "/p:AppleAgxFirmwareQualification=$firmwareQualificationValue" `
    "/p:AppleAgxBackendQualification=$backendQualificationValue"
if ($LASTEXITCODE -ne 0) {
    throw "Clean render-admission ARM64 WDK build failed with exit code $LASTEXITCODE"
}

$package = if ($MemoryQualification) {
    Join-Path $root "build\memory-qualification\$Configuration\AppleAgxRenderAdmission"
} else {
    Join-Path $root "ARM64\$Configuration\AppleAgxRenderAdmission"
}
& (Join-Path $PSScriptRoot 'verify-package-version.ps1') -PackageDirectory $package
