<#
  Keep the installed Windows bootable in BOTH storage modes (native AppleAns and the m1n1
  vNVMe bridge / stornvme) without a manual switch before each reboot.
    install   : AppleAns Start=0, clear both StartOverrides now, and register the SYSTEM task
                "AppleAnsDualBoot" (at startup, then every 5 minutes) that runs "clear".
    clear     : remove Services\{AppleAns,stornvme}\StartOverride (idempotent; the task action).
    uninstall : remove the task and return to the recovery contract (AppleAns Start=3).
    status    : print the contract state.
  Windows writes StartOverride\0=3 for a boot-start storage driver that controlled no device in
  the current boot (AppleAns on the bridge, stornvme on native ANS) and winload honours it at
  the next boot (ANS010-R6). Clearing it in every boot keeps both drivers boot-start. A boot-start
  driver with no device is loaded and unloaded by PnP without effect.
#>
param([Parameter(Mandatory)][ValidateSet('install','clear','uninstall','status')][string]$Mode)
$ErrorActionPreference = 'Stop'
$svc = 'HKLM:\SYSTEM\CurrentControlSet\Services'
$task = 'AppleAnsDualBoot'
$self = 'C:\ProgramData\AppleAns\ans-dual-boot.ps1'
function Clear-Overrides {
    foreach ($n in 'AppleAns', 'stornvme') {
        if (Test-Path "$svc\$n\StartOverride") { Remove-Item "$svc\$n\StartOverride" -Recurse -Force; "CLEARED_STARTOVERRIDE $n" }
    }
}
function Status {
    foreach ($n in 'AppleAns', 'stornvme') {
        $p = Get-ItemProperty "$svc\$n" -EA SilentlyContinue
        "SERVICE $n Start=$($p.Start) StartOverride=$(Test-Path "$svc\$n\StartOverride")"
    }
    "IMAGEPATH $((Get-ItemProperty "$svc\AppleAns" -EA SilentlyContinue).ImagePath)"
    $t = Get-ScheduledTask -TaskName $task -EA SilentlyContinue
    "TASK $task present=$([bool]$t) state=$($t.State)"
}
switch ($Mode) {
    'clear' { Clear-Overrides }
    'install' {
        foreach ($n in 'AppleAns', 'stornvme') { if (-not (Test-Path "$svc\$n")) { throw "service $n missing" } }
        if ((Get-ItemProperty "$svc\stornvme").Start -ne 0) { throw 'stornvme is not boot-start' }
        $image = (Get-ItemProperty "$svc\AppleAns").ImagePath
        if (-not (Test-Path ($image -replace '^\\SystemRoot', $env:SystemRoot))) { throw "AppleAns ImagePath missing: $image" }
        if ((Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0003\0' -EA SilentlyContinue).Service -ne 'AppleAns') {
            throw 'persisted APPL0003 devnode missing (ANS010-PREP3)' }
        New-Item -ItemType Directory -Force (Split-Path $self) | Out-Null
        Copy-Item -Force $PSCommandPath $self
        $action = New-ScheduledTaskAction -Execute 'powershell.exe' -Argument "-NoProfile -ExecutionPolicy Bypass -File `"$self`" clear"
        $boot = New-ScheduledTaskTrigger -AtStartup
        $boot.Delay = 'PT1M'
        $boot.Repetition = (New-ScheduledTaskTrigger -Once -At (Get-Date) -RepetitionInterval (New-TimeSpan -Minutes 5)).Repetition
        $principal = New-ScheduledTaskPrincipal -UserId 'SYSTEM' -LogonType ServiceAccount -RunLevel Highest
        $settings = New-ScheduledTaskSettingsSet -AllowStartIfOnBatteries -DontStopIfGoingOnBatteries -ExecutionTimeLimit (New-TimeSpan -Minutes 2)
        Register-ScheduledTask -TaskName $task -Action $action -Trigger $boot -Principal $principal -Settings $settings -Force | Out-Null
        Set-ItemProperty "$svc\AppleAns" -Name Start -Value 0
        Clear-Overrides
    }
    'uninstall' {
        Unregister-ScheduledTask -TaskName $task -Confirm:$false -EA SilentlyContinue
        Set-ItemProperty "$svc\AppleAns" -Name Start -Value 3
        if (Test-Path "$svc\stornvme\StartOverride") { Remove-Item "$svc\stornvme\StartOverride" -Recurse -Force; "CLEARED_STARTOVERRIDE stornvme" }
    }
}
Status
"DUAL_BOOT_$($Mode.ToUpper())_OK"
