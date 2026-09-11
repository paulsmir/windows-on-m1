param([Parameter(Mandatory=$true)][string]$PackageDirectory)
$ErrorActionPreference = 'Stop'
$verify = Join-Path $PSScriptRoot 'verify-package-version.ps1'
& $verify -PackageDirectory $PackageDirectory
$testDir = Join-Path ([IO.Path]::GetTempPath()) ('AppleAgxVersionTest-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $testDir | Out-Null
try {
    foreach ($name in @('AppleAgxRenderAdmission.inf','AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll')) {
        Copy-Item (Join-Path $PackageDirectory $name) $testDir
    }
    $inf = Join-Path $testDir 'AppleAgxRenderAdmission.inf'
    $original = Get-Content -Raw $inf
    $wrong = [regex]::Replace($original, '(?im)^(DriverVer\s*=\s*[^,\r\n]+,)\s*[^\r\n]+', '${1}1.2.3.4')
    Set-Content -LiteralPath $inf -Value $wrong
    $rejected = $false
    try { & $verify -PackageDirectory $testDir } catch {
        if ($_.Exception.Message -notlike 'Package version mismatch:*') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw 'Verifier accepted mismatched INF' }
    Set-Content -LiteralPath $inf -Value $original
    Remove-Item -LiteralPath (Join-Path $testDir 'AppleAgxRenderAdmission.sys')
    $rejected = $false
    try { & $verify -PackageDirectory $testDir } catch {
        if ($_.Exception.Message -notlike 'Missing package binary:*') { throw }
        $rejected = $true
    }
    if (-not $rejected) { throw 'Verifier accepted missing KMD' }
    Write-Output 'Package version tests PASS: matching artifact / mismatched INF / missing KMD'
} finally {
    Remove-Item -LiteralPath $testDir -Recurse -Force
}
