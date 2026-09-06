param(
    [Parameter(Mandatory=$true)][string]$PackageDirectory,
    [Parameter(Mandatory=$true)][ValidatePattern('^[0-9A-Fa-f]{40}$')][string]$CertificateThumbprint,
    [string]$WdkRoot = "${env:ProgramFiles(x86)}\Windows Kits\10"
)
$ErrorActionPreference = 'Stop'
$package = (Resolve-Path $PackageDirectory).Path
$sign = Join-Path $WdkRoot 'bin\10.0.26100.0\x64\signtool.exe'
$cat = Join-Path $WdkRoot 'bin\10.0.26100.0\x86\Inf2Cat.exe'
$verify = Join-Path $WdkRoot 'Tools\10.0.26100.0\x64\infverif.exe'
$certificate = Get-Item "Cert:\CurrentUser\My\$CertificateThumbprint"
if (-not $certificate.HasPrivateKey -or $certificate.NotAfter -lt (Get-Date)) {
    throw 'An existing valid code-signing certificate with private key is required'
}
& $verify /w /v (Join-Path $package 'AppleAns.inf')
if ($LASTEXITCODE -ne 0) { throw 'InfVerif failed' }
& $sign sign /v /fd SHA256 /s My /sha1 $CertificateThumbprint (Join-Path $package 'AppleAns.sys')
if ($LASTEXITCODE -ne 0) { throw 'SYS test signing failed' }
# Recreate catalog over the exact final package; Win11 24H2 ARM64, not the WDK
# server-only default. No timestamp/network service or certificate-store changes.
& $cat "/driver:$package" /os:10_GE_ARM64 /uselocaltime
if ($LASTEXITCODE -ne 0) { throw 'Win11 ARM64 catalog generation failed' }
& $sign sign /v /fd SHA256 /s My /sha1 $CertificateThumbprint (Join-Path $package 'AppleAns.cat')
if ($LASTEXITCODE -ne 0) { throw 'Catalog test signing failed' }
& $sign verify /pa /v (Join-Path $package 'AppleAns.sys')
if ($LASTEXITCODE -ne 0) { throw 'SYS Authenticode verification failed' }
& $sign verify /pa /v (Join-Path $package 'AppleAns.cat')
if ($LASTEXITCODE -ne 0) { throw 'Catalog Authenticode verification failed' }
& $sign verify /pa /v /c (Join-Path $package 'AppleAns.cat') (Join-Path $package 'AppleAns.sys')
if ($LASTEXITCODE -ne 0) { throw 'SYS catalog membership verification failed' }
Get-ChildItem $package -File | Get-FileHash -Algorithm SHA256
# This is a test-signing gate; no production/kernel-policy signature is claimed.
# The script never installs a driver or certificate.
