param([Parameter(Mandatory=$true)][string]$PublishedName)
$ErrorActionPreference = "Continue"
$parameters = "HKLM:\SYSTEM\CurrentControlSet\Services\AppleInput\Parameters"
New-Item -Path $parameters -Force | Out-Null
New-ItemProperty -Path $parameters -Name TransportOnly -PropertyType DWord `
    -Value 1 -Force | Out-Null
verifier /reset
pnputil /remove-device "ACPI\APPL0001\0"
pnputil /delete-driver $PublishedName /uninstall /force
Write-Host "To disable test signing: bcdedit /deletevalue testsigning"
