[CmdletBinding()]
param(
    [Parameter(Mandatory=$true)][ValidatePattern('^EXP[0-9]+$')][string]$Experiment,
    [Parameter(Mandatory=$true)][string]$ExpectedBootUtc,
    [Parameter(Mandatory=$true)][ValidateRange(1,2147483647)][int]$ExpectedDwmId,
    [Parameter(Mandatory=$true)][string]$ExpectedPackageVersion
)
$ErrorActionPreference='Stop'
if($env:COMPUTERNAME -ne 'J313-WIN'){throw 'wrong machine'}
$base=Join-Path 'C:\Users\pavel' $Experiment
if(!(Test-Path $base -PathType Container)){throw 'experiment directory absent'}
$beforePath=Join-Path $base 'dwm-reinit-before.json'
$afterPath=Join-Path $base 'dwm-reinit-after.json'
if((Test-Path $beforePath) -or (Test-Path $afterPath)){throw 'one-shot receipt already exists'}
$id='ACPI\APPL0002\0'
$boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o')
$device=Get-PnpDevice -InstanceId $id
$version=(Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_DriverVersion).Data
$arm=(Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters' -Name G3Armed -ErrorAction SilentlyContinue).G3Armed
if($boot -ne $ExpectedBootUtc -or [int]$device.Problem -ne 0 -or $arm -or
   $version -ne $ExpectedPackageVersion){throw 'original adapter identity mismatch'}
$dwms=@(Get-Process dwm | Where-Object SessionId -eq 1)
if($dwms.Count -ne 1 -or $dwms[0].Id -ne $ExpectedDwmId){throw 'console DWM identity changed'}
$old=$dwms[0]
$explorers=@(Get-Process explorer | Where-Object SessionId -eq 1 | Select-Object -ExpandProperty Id | Sort-Object)
if($explorers.Count -eq 0){throw 'console Explorer absent'}

Add-Type -TypeDefinition @'
using System;
using System.Runtime.InteropServices;
public static class ExpDwmReinitialize {
    [DllImport("kernel32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool IsProcessCritical(IntPtr process,
        [MarshalAs(UnmanagedType.Bool)] out bool critical);
    [DllImport("kernel32.dll", SetLastError=true)]
    [return: MarshalAs(UnmanagedType.Bool)]
    public static extern bool TerminateProcess(IntPtr process, uint exitCode);
    [DllImport("kernel32.dll", SetLastError=true)]
    public static extern uint WaitForSingleObject(IntPtr process, uint milliseconds);
}
'@
# Hold this process object's handle through the query and termination. A new
# process reusing the numeric PID must never become the target.
$handle=$old.Handle
$critical=$false
if(![ExpDwmReinitialize]::IsProcessCritical($handle,[ref]$critical)){
    throw ('critical-status query failed: '+[Runtime.InteropServices.Marshal]::GetLastWin32Error())
}
$before=[ordered]@{
    Utc=[DateTime]::UtcNow.ToString('o');Experiment=$Experiment;Boot=$boot;
    OldDwmId=$old.Id;OldDwmStart=$old.StartTime.ToUniversalTime().ToString('o');
    Session=$old.SessionId;Critical=$critical;ExplorerIds=$explorers;
    DriverVersion=$version;Problem=[int]$device.Problem;
    Action='Explicit single compositor reinitialization; not driver reset'
}
$before|ConvertTo-Json -Depth 4|Set-Content -Encoding UTF8 $beforePath
if($critical){throw 'critical process: no termination performed'}
if($old.HasExited){throw 'original DWM already exited; do not target replacement'}
if(![ExpDwmReinitialize]::TerminateProcess($handle,1)){
    throw ('termination failed: '+[Runtime.InteropServices.Marshal]::GetLastWin32Error())
}
$wait=[ExpDwmReinitialize]::WaitForSingleObject($handle,10000)
$new=$null
$deadline=[DateTime]::UtcNow.AddSeconds(60)
if($wait -eq 0){
    do {
        $candidates=@(Get-Process dwm -ErrorAction SilentlyContinue | Where-Object {
            $_.SessionId -eq 1 -and $_.Id -ne $ExpectedDwmId
        })
        if($candidates.Count -eq 1){$new=$candidates[0];break}
        Start-Sleep -Milliseconds 500
    } while([DateTime]::UtcNow -lt $deadline)
}
$afterBoot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o')
$afterExplorers=@(Get-Process explorer -ErrorAction SilentlyContinue | Where-Object SessionId -eq 1 | Select-Object -ExpandProperty Id | Sort-Object)
$problem=[int](Get-PnpDevice -InstanceId $id).Problem
$sameShell=(($explorers -join ',') -eq ($afterExplorers -join ','))
$result=if($wait -ne 0){'OLD_PROCESS_EXIT_NOT_OBSERVED'}elseif(!$new){'REPLACEMENT_NOT_OBSERVED'}elseif($afterBoot -ne $boot -or !$sameShell -or $problem -ne 0){'ENVIRONMENT_CHANGED'}else{'REINITIALIZED'}
$after=[ordered]@{
    Utc=[DateTime]::UtcNow.ToString('o');Experiment=$Experiment;Boot=$afterBoot;
    OldDwmId=$ExpectedDwmId;WaitResult=$wait;Result=$result;
    NewDwmId=$(if($new){$new.Id}else{$null});
    NewDwmStart=$(if($new){$new.StartTime.ToUniversalTime().ToString('o')}else{$null});
    ExplorerIds=$afterExplorers;SameShell=$sameShell;Problem=$problem;
    Limitation='A replacement PID proves process reinitialization, not rendered output'
}
$after|ConvertTo-Json -Depth 4|Set-Content -Encoding UTF8 $afterPath
$old.Dispose()
$after|ConvertTo-Json -Depth 4
if($result -ne 'REINITIALIZED'){throw 'single reinitialization incomplete; collect evidence, do not retry'}
