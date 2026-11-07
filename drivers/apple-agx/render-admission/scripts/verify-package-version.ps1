param([Parameter(Mandatory=$true)][string]$PackageDirectory)
$ErrorActionPreference = 'Stop'
$inf = Join-Path $PackageDirectory 'AppleAgxRenderAdmission.inf'
$text = Get-Content -Raw $inf
$match = [regex]::Match($text, '(?im)^DriverVer\s*=\s*[^,\r\n]+,\s*(\d+\.\d+\.\d+\.\d+)\s*$')
if (-not $match.Success) { throw 'Missing numeric INF DriverVer' }
$expected = [version]$match.Groups[1].Value
# EXP1820: an ARM64X forwarder package carries the ARM64 and x64 UMDs too.
$names = @('AppleAgxRenderAdmission.sys', 'AppleAgxRenderAdmissionUmd.dll')
foreach ($backing in @('AppleAgxRenderAdmissionUmdArm64.dll', 'AppleAgxRenderAdmissionUmdX64.dll')) {
    if (Test-Path -LiteralPath (Join-Path $PackageDirectory $backing)) { $names += $backing }
}
foreach ($name in $names) {
    $path = Join-Path $PackageDirectory $name
    if (-not (Test-Path -LiteralPath $path -PathType Leaf)) { throw "Missing package binary: $name" }
    $info = [Diagnostics.FileVersionInfo]::GetVersionInfo($path)
    if ([string]::IsNullOrWhiteSpace($info.FileVersion)) { throw "Missing VERSIONINFO: $name" }
    $actual = [version]::new($info.FileMajorPart, $info.FileMinorPart, $info.FileBuildPart, $info.FilePrivatePart)
    if ($actual -ne $expected) { throw "Package version mismatch: $name=$actual INF=$expected" }
}
Write-Output "Package versions coherent: $expected"
