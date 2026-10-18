$ErrorActionPreference='Stop'
$b='C:\Users\pavel\EXP930';$id='ACPI\APPL0002\0';$k='HKLM:\SYSTEM\CurrentControlSet\Enum\'+$id+'\Device Parameters'
$d=Get-PnpDevice -InstanceId $id;$p=Get-ItemProperty $k
$boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o')
if([Math]::Abs(([DateTime]$boot-[DateTime]'2026-10-02T17:06:29.4893080Z').TotalSeconds) -gt 1 -or [int]$d.Problem -ne 43 -or $p.Wom1StartStage -ne 1 -or [uint32]$p.Wom1StartStatus -ne 3221225659 -or $p.G3Armed -or $p.Wom1GpuvaArmBuild -ne 930 -or $p.Wom1GpuvaArmPhase -ne 2){throw 'failed-before-GPU checkpoint changed'}
$m=Get-Content "$b\live-manifest.json" -Raw|ConvertFrom-Json
if((Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_DriverVersion).Data -ne '30.0.930.0' -or (Get-FileHash 'C:\Windows\System32\drivers\AppleAgxRenderAdmission.sys').Hash -ne $m.Package.'AppleAgxRenderAdmission.sys'){throw '930 identity mismatch'}
New-ItemProperty $k -Name G3Armed -PropertyType DWord -Value 1 -Force|Out-Null
if((Get-ItemProperty $k).G3Armed -ne 1){throw 'arm not written'}
[ordered]@{Utc=[DateTime]::UtcNow.ToString('o');Boot=$boot;Problem=43;Stage=1;Status=3221225659;Arm=1;Action='Restart exact930 devnode only; no OS reboot'}|ConvertTo-Json|Set-Content "$b\rearm-before.json"
& pnputil.exe /restart-device $id *> "$b\pnputil-device-restart.log"
$code=$LASTEXITCODE
$d=Get-PnpDevice -InstanceId $id;$p=Get-ItemProperty $k
[ordered]@{Utc=[DateTime]::UtcNow.ToString('o');Boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o');Exit=$code;Problem=[int]$d.Problem;Stage=$p.Wom1StartStage;Status=$p.Wom1StartStatus;Arm=$p.G3Armed;Version=(Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_DriverVersion).Data;ArmBuild=$p.Wom1GpuvaArmBuild;ArmPhase=$p.Wom1GpuvaArmPhase;ArmStatus=$p.Wom1GpuvaArmStatus;ArmHypercall=$p.Wom1GpuvaArmHypercall;ArmGeneration=$p.Wom1GpuvaArmGeneration;NoReboot=$true}|ConvertTo-Json|Set-Content "$b\rearm-result.json"
Get-Content "$b\rearm-result.json" -Raw
if($code){throw "device restart returned $code; no reboot requested"}
