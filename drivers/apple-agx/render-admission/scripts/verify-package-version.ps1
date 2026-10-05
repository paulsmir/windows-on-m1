param([Parameter(Mandatory=$true)][string]$PackageDirectory)
$ErrorActionPreference = 'Stop'
$inf = Join-Path $PackageDirectory 'AppleAgxRenderAdmission.inf'
$text = Get-Content -Raw $inf
$match = [regex]::Match($text, '(?im)^DriverVer\s*=\s*[^,\r\n]+,\s*(\d+\.\d+\.\d+\.\d+)\s*$')
if (-not $match.Success) { throw 'Missing numeric INF DriverVer' }
$expected = [version]$match.Groups[1].Value
foreach ($name in @('AppleAgxRenderAdmission.sys', 'AppleAgxRenderAdmissionUmd.dll')) {
    $path = Join-Path $PackageDirectory $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing package binary: $name" }
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($path)
    if ([string]::IsNullOrWhiteSpace($info.FileVersion)) { throw "Missing VERSIONINFO: $name" }
    $actual = [version]::new($info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart, $info.FilePrivatePart)
    if ($actual -ne $expected) { throw "Package version mismatch: $name=$actual INF=$expected" }
}
Write-Output "Package versions coherent: $expected"
