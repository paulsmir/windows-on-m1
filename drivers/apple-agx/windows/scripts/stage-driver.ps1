param([Parameter(Mandatory=$true)][string]$InfPath)

$ErrorActionPreference = "Stop"
$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Run this script from an elevated PowerShell window."
}

$resolvedInf = (Resolve-Path -LiteralPath $InfPath).Path
$packageDirectory = Split-Path -Parent $resolvedInf
$catalog = Join-Path $packageDirectory "AppleAgx.cat"
if (!(Test-Path -LiteralPath $catalog)) {
    throw "Catalog not found beside INF: $catalog"
}

$signature = Get-AuthenticodeSignature -FilePath $catalog
if ($signature.Status -ne "Valid" -or !$signature.SignerCertificate) {
    throw "AppleAgx.cat does not have a valid trusted signature."
}

Write-Host "Signer: $($signature.SignerCertificate.Subject)"
Write-Host "Thumbprint: $($signature.SignerCertificate.Thumbprint)"
pnputil /add-driver $resolvedInf
if ($LASTEXITCODE -ne 0) {
    throw "pnputil failed with exit code $LASTEXITCODE"
}

Write-Host "Package staged only; APPL0002 was not installed or restarted."
Write-Host "Record the Published Name (oemNN.inf) printed above for rollback."
