[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ManifestSha256)
$ErrorActionPreference='Stop'
$b='C:\Users\pavel\EXP929'
$id='ACPI\APPL0002\0'
$key='HKLM:\SYSTEM\CurrentControlSet\Enum\'+$id+'\Device Parameters'
$manifest=Join-Path $b 'live-manifest.json'
if($env:COMPUTERNAME -ne 'J313-WIN' -or (Get-FileHash $manifest).Hash -ne $ManifestSha256){throw 'machine/manifest mismatch'}
$m=Get-Content $manifest -Raw|ConvertFrom-Json
if($m.Experiment -ne 'EXP929' -or !$m.NoReboot -or $m.Version -ne '30.0.929.0'){throw 'live contract mismatch'}
$boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o')
if([Math]::Abs(([DateTime]$boot-[DateTime]$m.ExpectedBoot).TotalSeconds) -gt 1){throw 'boot identity changed'}
$d=Get-PnpDevice -InstanceId $id
$oldInf=(Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_DriverInfPath).Data
$oldVersion=(Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_DriverVersion).Data
if([int]$d.Problem -ne 0 -or $oldInf -ne 'oem5.inf' -or $oldVersion -ne '30.0.928.0'){throw 'old devnode mismatch'}
if((Get-FileHash 'C:\Windows\INF\oem5.inf').Hash -ne $m.OldPackage.'AppleAgxRenderAdmission.inf' -or (Get-FileHash 'C:\Windows\System32\drivers\AppleAgxRenderAdmission.sys').Hash -ne $m.OldPackage.'AppleAgxRenderAdmission.sys' -or (Get-FileHash 'C:\Windows\System32\AppleAgxRenderAdmissionUmd.dll').Hash -ne $m.OldPackage.'AppleAgxRenderAdmissionUmd.dll'){throw 'old package mismatch'}
if((Get-ItemProperty $key -Name G3Armed -ErrorAction SilentlyContinue).G3Armed){throw 'unexpected arm'}
foreach($p in $m.Package.PSObject.Properties){if((Get-FileHash (Join-Path "$b\package" $p.Name)).Hash -ne $p.Value){throw "candidate hash mismatch $($p.Name)"}}
foreach($p in $m.OldPackage.PSObject.Properties){if((Get-FileHash (Join-Path 'C:\Users\pavel\EXP928\package' $p.Name)).Hash -ne $p.Value){throw "rollback artifact mismatch $($p.Name)"}}
$cat=Join-Path "$b\package" 'appleagxrenderadmission.cat'
if((Get-AuthenticodeSignature $cat).SignerCertificate.Thumbprint -ne $m.Signer){throw 'catalog signer mismatch'}
$tool='C:\Users\pavel\J313-tools\signing\signtool-arm64.exe'
if((Get-FileHash $tool).Hash -ne '097bdc4805f0cdcb4c1689a1533b0eb9a6143c3751c421b7ecc26b5c8cd5f0b1'){throw 'verifier mismatch'}
foreach($n in @('AppleAgxRenderAdmission.sys','AppleAgxRenderAdmissionUmd.dll')){
 & $tool verify /pa /c $cat (Join-Path "$b\package" $n) *> (Join-Path $b ('verify-'+$n+'.log'))
 if($LASTEXITCODE){throw "catalog membership failed $n"}
}
$crash='HKLM:\SYSTEM\CurrentControlSet\Control\CrashControl'
$priorAutoReboot=(Get-ItemProperty $crash).AutoReboot
[ordered]@{Utc=[DateTime]::UtcNow.ToString('o');Boot=$boot;OldInf=$oldInf;OldVersion=$oldVersion;OldAutoReboot=$priorAutoReboot;ManifestSha256=$ManifestSha256;NoReboot=$true}|ConvertTo-Json|Set-Content "$b\live-before.json"
# The user explicitly requires this investigation to remain in the same boot.
Set-ItemProperty $crash -Name AutoReboot -Type DWord -Value 0
New-ItemProperty $key -Name G3Armed -PropertyType DWord -Value 1 -Force|Out-Null
try {
 & pnputil.exe /add-driver "$b\package\AppleAgxRenderAdmission.inf" /install *> "$b\pnputil-live-install.log"
 $result=$LASTEXITCODE
} finally {
 # A consumed arm is absent; never leave an unused experiment arm behind.
 if((Get-ItemProperty $key -Name G3Armed -ErrorAction SilentlyContinue).G3Armed -eq 1){Remove-ItemProperty $key -Name G3Armed}
}
$nowBoot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o')
$d=Get-PnpDevice -InstanceId $id
$newInf=(Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_DriverInfPath -ErrorAction SilentlyContinue).Data
$newVersion=(Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_DriverVersion -ErrorAction SilentlyContinue).Data
[ordered]@{Utc=[DateTime]::UtcNow.ToString('o');BootBefore=$boot;BootAfter=$nowBoot;PnPExit=$result;RebootRequired=($result -eq 3010);Problem=[int]$d.Problem;Inf=$newInf;Version=$newVersion;AutoReboot=(Get-ItemProperty $crash).AutoReboot;NoReboot=$true;LoadedIdentityPending=$true}|ConvertTo-Json|Set-Content "$b\live-install-result.json"
Get-Content "$b\live-install-result.json" -Raw
if([Math]::Abs(([DateTime]$nowBoot-[DateTime]$boot).TotalSeconds) -gt 1){throw 'unexpected boot change'}
if($result -ne 0){throw "PnP returned $result; no restart/reboot requested"}
if([int]$d.Problem -ne 0 -or $newVersion -ne $m.Version){throw 'candidate not active; no restart/reboot requested'}
'PNP_UPDATE_RETURNED_SUCCESS_VERIFY_LOADED_KMD_NEXT'
