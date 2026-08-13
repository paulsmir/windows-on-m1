param([Parameter(Mandatory=$true)][string]$InfPath)
$ErrorActionPreference = "Stop"
if (!(Test-Path $InfPath)) { throw "INF not found: $InfPath" }
bcdedit /set testsigning on
pnputil /add-driver $InfPath /install
Write-Host "Reboot is required before the test-signed driver can load."
