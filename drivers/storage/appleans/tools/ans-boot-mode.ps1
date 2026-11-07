<#
  Switch the installed Windows between boot modes (ANS010-R7 contract).
    native   : next boot uses AppleAns on native ANS (Start=0, no StartOverride).
    recovery : next boot uses stornvme on the m1n1 vNVMe bridge (AppleAns Start=3, stornvme not demoted).
    status   : print the contract state only.
  Windows writes Services\<drv>\StartOverride\0=3 for a boot-start storage driver that controlled no
  device in the current boot (AppleAns in recovery, stornvme in native); winload honours it, so each
  switch removes the override of the driver the next boot needs. Idempotent; run elevated.
#>
param([Parameter(Mandatory)][ValidateSet('native','recovery','status')][string]$Mode)
$ErrorActionPreference = 'Stop'
$svc = 'HKLM:\SYSTEM\CurrentControlSet\Services'
function Override($name) { Test-Path "$svc\$name\StartOverride" }
function Clear-Override($name) { if (Override $name) { Remove-Item "$svc\$name\StartOverride" -Recurse -Force; "CLEARED_STARTOVERRIDE $name" } }
function Status {
    foreach ($n in 'AppleAns', 'stornvme') {
        $p = Get-ItemProperty "$svc\$n" -EA SilentlyContinue
        "SERVICE $n Start=$($p.Start) StartOverride=$(Override $n)"
    }
    $pkg = Get-ChildItem HKLM:\SYSTEM\DriverDatabase\DriverPackages | ? PSChildName -like 'appleans.inf_*'
    "PACKAGE $($pkg.PSChildName) records=$($pkg.SubKeyCount)"
    "DEVNODE ACPI\APPL0003\0 service=$((Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0003\0' -EA SilentlyContinue).Service)"
}
if ($Mode -ne 'status') {
    foreach ($n in 'AppleAns', 'stornvme') { if (-not (Test-Path "$svc\$n")) { throw "service $n missing" } }
    if ($Mode -eq 'native') {
        $pkg = Get-ChildItem HKLM:\SYSTEM\DriverDatabase\DriverPackages | ? PSChildName -like 'appleans.inf_*'
        if (-not $pkg -or $pkg.SubKeyCount -lt 3) { throw 'AppleAns package records missing (ANS010-PREP2)' }
        if ((Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0003\0' -EA SilentlyContinue).Service -ne 'AppleAns') {
            throw 'persisted APPL0003 devnode missing (ANS010-PREP3)' }
        Set-ItemProperty "$svc\AppleAns" -Name Start -Value 0
        Clear-Override 'AppleAns'
    } else {
        Set-ItemProperty "$svc\AppleAns" -Name Start -Value 3
    }
    # stornvme must never stay demoted: it is the recovery boot controller driver.
    Clear-Override 'stornvme'
    if ((Get-ItemProperty "$svc\stornvme").Start -ne 0) { throw 'stornvme is not boot-start' }
}
Status
"BOOT_MODE_$($Mode.ToUpper())_OK"
