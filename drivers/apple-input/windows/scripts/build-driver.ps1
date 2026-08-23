param([ValidateSet("Debug", "Release")][string]$Configuration = "Debug",
      [ValidateSet("ARM64")][string]$Platform = "ARM64",
      [switch]$CodeAnalysis)
$ErrorActionPreference = "Stop"
$root = Split-Path -Parent $PSScriptRoot
$vswhere = Join-Path ${env:ProgramFiles(x86)} "Microsoft Visual Studio\Installer\vswhere.exe"
if (!(Test-Path $vswhere)) { throw "vswhere.exe was not found" }
$msbuild = & $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find MSBuild\**\Bin\MSBuild.exe | Select-Object -First 1
if (!$msbuild) { throw "MSBuild was not found" }
$properties = "/p:Configuration=$Configuration", "/p:Platform=$Platform"
if ($CodeAnalysis) { $properties += "/p:RunCodeAnalysis=true" }
& $msbuild (Join-Path $root "AppleInput.vcxproj") /m @properties
if ($LASTEXITCODE) { exit $LASTEXITCODE }
& $msbuild (Join-Path $root "tools/AppleInputDiag/AppleInputDiag.vcxproj") /m @properties
if ($LASTEXITCODE) { exit $LASTEXITCODE }
