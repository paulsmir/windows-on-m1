param(
    [string]$Configuration = "Release"
)

$ErrorActionPreference = "Stop"
$project = Join-Path $PSScriptRoot "UmdContractTest.vcxproj"
$msbuildCommand = Get-Command msbuild -ErrorAction SilentlyContinue
if ($null -ne $msbuildCommand) {
    $msbuild = $msbuildCommand.Source
} else {
    $vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
    if (-not (Test-Path $vswhere)) {
        throw "MSBuild is not on PATH and vswhere.exe was not found"
    }
    $msbuild = & $vswhere -latest -prerelease -products * `
        -requires Microsoft.Component.MSBuild `
        -find "MSBuild\**\Bin\amd64\MSBuild.exe" | Select-Object -First 1
    if ([string]::IsNullOrWhiteSpace($msbuild)) {
        throw "A Visual Studio installation with x64 WDK MSBuild support was not found"
    }
}

& $msbuild $project /m /t:Clean,Build "/p:Configuration=$Configuration" /p:Platform=x64
if ($LASTEXITCODE -ne 0) {
    throw "UMD contract test build failed with exit code $LASTEXITCODE"
}
$binary = Join-Path $PSScriptRoot "x64\$Configuration\UmdContractTest.exe"
if (-not (Test-Path $binary)) {
    throw "UMD contract test output was not found: $binary"
}
& $binary
if ($LASTEXITCODE -ne 0) {
    throw "UMD contract test failed with $LASTEXITCODE contract violations"
}
[ordered]@{
    Binary = $binary
    SHA256 = (Get-FileHash $binary -Algorithm SHA256).Hash
    Result = "PASS"
} | ConvertTo-Json
