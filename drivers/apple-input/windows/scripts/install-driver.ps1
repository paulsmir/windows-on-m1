param(
    [Parameter(Mandatory=$true)][string]$InfPath,
    [switch]$PublishKeyboard,
    [switch]$PublishTrackpad
)
$ErrorActionPreference = "Stop"
if (!(Test-Path $InfPath)) { throw "INF not found: $InfPath" }
bcdedit /set testsigning on
pnputil /add-driver $InfPath /install
$parameters = "HKLM:\SYSTEM\CurrentControlSet\Services\AppleInput\Parameters"
$transportOnly = if ($PublishKeyboard -or $PublishTrackpad) { 0 } else { 1 }
New-Item -Path $parameters -Force | Out-Null
New-ItemProperty -Path $parameters -Name TransportOnly -PropertyType DWord `
    -Value $transportOnly -Force | Out-Null
New-ItemProperty -Path $parameters -Name PublishKeyboard -PropertyType DWord `
    -Value ([int]$PublishKeyboard.IsPresent) -Force | Out-Null
New-ItemProperty -Path $parameters -Name PublishTrackpad -PropertyType DWord `
    -Value ([int]$PublishTrackpad.IsPresent) -Force | Out-Null
pnputil /restart-device "ACPI\APPL0001\0"
Write-Host "TransportOnly=$transportOnly"
Write-Host "PublishKeyboard=$([int]$PublishKeyboard.IsPresent)"
Write-Host "PublishTrackpad=$([int]$PublishTrackpad.IsPresent)"
Write-Host "Reboot is required before the test-signed driver can load."
