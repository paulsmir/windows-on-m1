param([Parameter(Mandatory=$true)][string]$PublishedName)

$ErrorActionPreference = "Stop"
$principal = New-Object Security.Principal.WindowsPrincipal(
    [Security.Principal.WindowsIdentity]::GetCurrent())
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) {
    throw "Run this script from an elevated PowerShell window."
}
if ($PublishedName -notmatch '^oem\d+\.inf$') {
    throw "PublishedName must be the exact recorded oemNN.inf identity."
}

pnputil /delete-driver $PublishedName
if ($LASTEXITCODE -ne 0) {
    throw "pnputil failed with exit code $LASTEXITCODE"
}
