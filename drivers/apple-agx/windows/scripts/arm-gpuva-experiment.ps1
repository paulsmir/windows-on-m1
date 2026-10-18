[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$DurabilityManifestPath)

$ErrorActionPreference = 'Stop'
if ($env:COMPUTERNAME -ne 'J313-WIN') { throw 'wrong guest' }
$principal = [Security.Principal.WindowsPrincipal]::new([Security.Principal.WindowsIdentity]::GetCurrent())
if (!$principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)) { throw 'administrator required' }
$manifest = Get-Content -LiteralPath $DurabilityManifestPath -Raw | ConvertFrom-Json
if ($manifest.Package.State -ne 'Staged' -or [int]$manifest.Package.G3Armed -ne 1) {
    throw 'arm requires exact staged package and G3Armed=1 manifest'
}
$key = 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'
New-Item -Path $key -Force | Out-Null
New-ItemProperty -Path $key -Name G3Armed -PropertyType DWord -Value 1 -Force | Out-Null
& (Join-Path $PSScriptRoot 'durable-transition.ps1') -Mode Commit -ManifestPath $DurabilityManifestPath
