$ErrorActionPreference='Stop'
$base='C:\Users\pavel\EXP866'
$id='ACPI\APPL0002\0'
$key='HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'
$infHash='b2c7e2993d6d92eabf301d9db6bf640fa4c1ffe3cea145c47a3e8bc3c587ce21'
$expected=@{
 'AppleAgxRenderAdmission.inf'=$infHash
 'AppleAgxRenderAdmission.sys'='813a0c746cb200c79a96ead7571bec10768fd3dd7123da40448d4a5f9b426cd1'
 'AppleAgxRenderAdmissionUmd.dll'='1010eb9b13ee7e58f623fb7dd10ccba780b2b71b0b1558d948a68d86444def50'
 'appleagxrenderadmission.cat'='3833fd3953072c84fbed4f49ba4e563d6eac325e80a80adcdd3460eb31d65827'
}
if($env:COMPUTERNAME -ne 'J313-WIN'){throw 'wrong guest'}
foreach($n in $expected.Keys){if((Get-FileHash (Join-Path $base "package\$n")).Hash -ne $expected[$n]){throw "package mismatch $n"}}
if((Get-FileHash (Join-Path $base 'autologger-stage.ps1')).Hash -ne '7debd29c21663ee290a471a56a43dbc5dc8b93b8e8ea1b746fa487ae094912ab'){throw 'autologger script mismatch'}
$d=Get-PnpDevice -InstanceId $id
$staged=@(Get-WindowsDriver -Online -All|Where-Object OriginalFileName -match 'AppleAgxRenderAdmission\.inf$')
if([int]$d.Problem -ne 28 -or $staged.Count -ne 1 -or $staged[0].Driver -notmatch '^oem\d+\.inf$'){throw 'partial stage identity mismatch'}
$inf=$staged[0].Driver
if((Get-FileHash (Join-Path $env:windir "INF\$inf")).Hash -ne $infHash){throw 'staged INF mismatch'}
$bound=(Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_DriverInfPath -ErrorAction SilentlyContinue).Data
$service=(Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_Service -ErrorAction SilentlyContinue).Data
if($bound -or $service -or (Get-ItemProperty $key -Name G3Armed -ErrorAction SilentlyContinue).G3Armed){throw 'already bound or armed'}
if(Test-Path 'HKLM:\SYSTEM\CurrentControlSet\Control\WMI\Autologger\EXP801DxgBoot'){throw 'logger already configured'}
foreach($n in @('dwm.exe','explorer.exe')){if(!(Test-Path "HKLM:\SOFTWARE\Microsoft\Windows\Windows Error Reporting\LocalDumps\$n")){throw "WER missing $n"}}
$thumb='E9BE15BD2A184BFABA0C8035B3C620C58037A241'
foreach($store in @('Root','TrustedPublisher')){if(!(Test-Path "Cert:\LocalMachine\$store\$thumb")){throw "signer missing $store"}}
if((Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors -ne 8){throw 'CPU mismatch'}
& (Join-Path $base 'autologger-stage.ps1')
if(!$?){throw 'logger failed'}
New-ItemProperty -Path $key -Name G3Armed -PropertyType DWord -Value 1 -Force|Out-Null
if((Get-ItemProperty $key -Name G3Armed).G3Armed -ne 1){throw 'arm failed'}
[ordered]@{Utc=[DateTime]::UtcNow.ToString('o');Inf=$inf;InfHash=$infHash;Problem=[int]$d.Problem;Arm=1;Cpu=8;Logger=(Get-ItemProperty 'HKLM:\SYSTEM\CurrentControlSet\Control\WMI\Autologger\EXP801DxgBoot').Start}|ConvertTo-Json|Set-Content (Join-Path $base 'staged.json')
[ordered]@{Utc=[DateTime]::UtcNow.ToString('o');Experiment='EXP866';Inf=$inf;InfSha256=$infHash;Arm=1;OrderedShutdown='PowerOff';NextProfile='cold-full-owner';ManifestSha256=(Get-FileHash (Join-Path $base 'hardware-manifest.json')).Hash.ToLowerInvariant()}|ConvertTo-Json -Compress|Set-Content (Join-Path $base 'stage-receipt.json')
Get-Content (Join-Path $base 'stage-receipt.json')
& shutdown.exe /s /t 10
if($LASTEXITCODE){throw 'ordered shutdown rejected'}
