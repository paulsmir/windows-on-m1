param([Parameter(Mandatory=$true)][string]$PublishedName)
$ErrorActionPreference = "Continue"
verifier /reset
pnputil /remove-device "ACPI\APPL0001\0"
pnputil /delete-driver $PublishedName /uninstall /force
Write-Host "To disable test signing: bcdedit /deletevalue testsigning"
