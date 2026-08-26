param(
    [ValidateSet("Debug", "Release")]
    [string]$Configuration = "Debug"
)

$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$project = Join-Path $root "AppleAgx.vcxproj"

& msbuild $project /m /t:Clean,Build "/p:Configuration=$Configuration" `
    /p:Platform=ARM64 /p:RunCodeAnalysis=true
if ($LASTEXITCODE -ne 0) {
    throw "AppleAgx ARM64 WDK build failed with exit code $LASTEXITCODE"
}
