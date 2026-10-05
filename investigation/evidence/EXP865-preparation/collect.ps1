$ErrorActionPreference='Stop'
. 'C:\Users\pavel\EXP865\etl-contract.ps1'
$root='C:\Users\pavel\EXP865-evidence'
New-Item -ItemType Directory -Force $root|Out-Null
$probeExit=$null
$id='ACPI\APPL0002\0'
$key='HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters'
$props=Get-ItemProperty $key
$receipts=[ordered]@{}
foreach($p in $props.PSObject.Properties){if($p.Name -match '^Wom1'){$receipts[$p.Name]=if($p.Value -is [byte[]]){[Convert]::ToBase64String($p.Value)}else{$p.Value}}}
if($props.PSObject.Properties.Name -contains 'Wom1G3CopyQueryFailure' -and !$receipts.Contains('Wom1G3CopyQueryFailure')){throw 'QUERY receipt capture failed'}
if($props.PSObject.Properties.Name -contains 'Wom1G3CopyQueryFailure'){
 $query=[byte[]]$props.Wom1G3CopyQueryFailure
 if($query.Length -ne 168){throw "QUERY v3 receipt length $($query.Length)"}
 [IO.File]::WriteAllBytes((Join-Path $root 'Wom1G3CopyQueryFailure.bin'),$query)
}
$staged=@(Get-WindowsDriver -Online -All|Where-Object OriginalFileName -match 'AppleAgxRenderAdmission\.inf$')
$inf=if($staged.Count -eq 1){$staged[0].Driver}else{$null}
$files=@('C:\Windows\System32\drivers\AppleAgxRenderAdmission.sys','C:\Windows\System32\AppleAgxRenderAdmissionUmd.dll')
if($inf){$files+=Join-Path $env:windir "INF\$inf"}
$hashes=@($files|Where-Object {Test-Path $_}|ForEach-Object {Get-FileHash $_|Select-Object Path,Hash})
$visible=@(Get-PnpDevice -PresentOnly -ErrorAction SilentlyContinue|Where-Object InstanceId -eq $id)
$os=Get-CimInstance Win32_OperatingSystem;$physical=(Get-CimInstance Win32_ComputerSystem).TotalPhysicalMemory;$memory=(Get-CimInstance Win32_OperatingSystem).FreePhysicalMemory
$report=[ordered]@{TotalPhysicalMemory=$physical;FreePhysicalMemoryKB=$memory;Problem=[int](Get-PnpDevice -InstanceId $id).Problem;StartStage=$props.Wom1StartStage;StartStatus=$props.Wom1StartStatus;UTC=[DateTime]::UtcNow.ToString('o');Boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime;Visible=$visible.Count;Arm=$props.G3Armed;ProbeExit=$probeExit;Receipts=$receipts;Staged=$inf;Hashes=$hashes;Cpu=(Get-CimInstance Win32_ComputerSystem).NumberOfLogicalProcessors;Storage=@(Get-CimInstance Win32_DiskDrive|Select-Object Model,Status);Usb=@(Get-PnpDevice -PresentOnly -Class USB|Select-Object Status,FriendlyName);Service=(Get-CimInstance Win32_SystemDriver -Filter "Name='AppleAgxAdmission'" -ErrorAction SilentlyContinue|Select-Object Name,State)}
$report|ConvertTo-Json -Depth 8|Set-Content (Join-Path $root 'state.json')
& reg.exe export 'HKLM\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0' (Join-Path $root 'devnode.reg') /y|Out-Null
& reg.exe export 'HKLM\SYSTEM\CurrentControlSet\Control\GraphicsDrivers' (Join-Path $root 'GraphicsDrivers.reg') /y|Out-Null
& reg.exe export 'HKLM\SYSTEM\CurrentControlSet\Services\AppleAgxAdmission' (Join-Path $root 'AppleAgxAdmission.reg') /y|Out-Null
& wevtutil.exe epl Microsoft-Windows-DxgKrnl-Admin (Join-Path $root 'DxgKrnl-Admin.evtx') /ow:true
& wevtutil.exe epl System (Join-Path $root 'System.evtx') /ow:true
& wevtutil.exe epl Application (Join-Path $root 'Application.evtx') /ow:true
$session='EXP801DxgBoot'
$logger="HKLM:\SYSTEM\CurrentControlSet\Control\WMI\Autologger\$session"
$null=& logman.exe stop $session -ets 2>&1
$traceStopExit=$LASTEXITCODE
if(Test-Path $logger){Set-ItemProperty -Path $logger -Name Start -Value 0;Remove-Item -Path $logger -Recurse -Force}
$etl=@(Get-ChildItem C:\Windows\System32\LogFiles\WMI -Filter "$session.etl*" -ErrorAction SilentlyContinue)
$version=(Get-PnpDeviceProperty -InstanceId $id -KeyName DEVPKEY_Device_DriverVersion -ErrorAction SilentlyContinue).Data
$original=Test-OriginalArmedBoot $visible.Count $report.Problem $props.G3Armed $version
if($original -and $traceStopExit -ne 0){throw 'original ETL session could not be stopped; do not reboot before evidence decision'}
$bootUtc=$os.LastBootUpTime.ToUniversalTime().ToString('o')
$traceFiles=@()
foreach($item in $etl){
 $prefix=if($original){'original-'}else{'recovery-'}
 $name=$prefix+$item.Name;$dest=Join-Path $root $name
 Copy-Item -LiteralPath $item.FullName -Destination $dest -Force
 $traceFiles+=[ordered]@{Name=$name;SHA256=(Get-FileHash $dest).Hash;Bytes=(Get-Item $dest).Length}
}
$origin=[ordered]@{Experiment='EXP865';Utc=[DateTime]::UtcNow.ToString('o');BootUtc=$bootUtc;OriginalArmedBoot=$original;Problem=$report.Problem;Visible=$visible.Count;DriverVersion=$version;Etls=$traceFiles;HeaderBootTimeVerification='Required before attributing events';Limitation=$(if($original){'Captured and stopped in original Code0 boot before ordered reboot'}else{'Original armed boot ETL unavailable from this recovery collection; early loss may have prevented capture and AutoLogger may have overwritten it. Recovery ETL is not original evidence.'})}
$originName=if($original){'original-etl-receipt.json'}else{'recovery-etl-receipt.json'}
$origin|ConvertTo-Json -Depth 6|Set-Content (Join-Path $root $originName)
if($original){Assert-OriginalEtlReceipt ([pscustomobject]$origin) $bootUtc $root}

$dumps=@(Get-ChildItem C:\Windows\Minidump -Filter '*.dmp' -ErrorAction SilentlyContinue|Where-Object LastWriteTimeUtc -gt ([DateTime]::UtcNow.AddHours(-4))|Sort-Object LastWriteTimeUtc -Descending|Select-Object -First 2)
foreach($item in $dumps){Copy-Item -LiteralPath $item.FullName -Destination $root -Force}
$umdTrace='C:\ProgramData\EXP865-Trace\umd.log'
if(Test-Path $umdTrace){Copy-Item -LiteralPath $umdTrace -Destination (Join-Path $root 'umd.log') -Force}
& pnputil.exe /enum-devices /instanceid 'ACPI\APPL0002\0' /resources *> (Join-Path $root 'pnputil-resources.txt')
Copy-Item C:\Windows\INF\setupapi.dev.log (Join-Path $root 'setupapi.dev.log') -Force
Get-PnpDeviceProperty -InstanceId $id | Format-List * | Out-File (Join-Path $root 'pnp-properties.txt')
$transition=Get-Content 'C:\Users\pavel\EXP865\stage-receipt.json' -Raw|ConvertFrom-Json
$stageUtc=[DateTime]::Parse([string]$transition.Utc).ToUniversalTime()
$wer=@(Get-ChildItem 'C:\CrashDumps' -File -ErrorAction SilentlyContinue|Where-Object {($_.Name -like 'dwm.exe.*.dmp' -or $_.Name -like 'explorer.exe.*.dmp') -and $_.LastWriteTimeUtc -ge $stageUtc})
foreach($item in $wer){Copy-Item -LiteralPath $item.FullName -Destination (Join-Path $root ("WER-"+$item.Name)) -Force}
$wer|Select-Object Name,Length,LastWriteTimeUtc|ConvertTo-Json -Depth 4|Set-Content (Join-Path $root 'wer-dumps.json')
$kernel=@(Get-Item 'C:\Windows\MEMORY.DMP' -ErrorAction SilentlyContinue)+@(Get-ChildItem 'C:\Windows\Minidump' -Filter '*.dmp' -ErrorAction SilentlyContinue)
$dumpReceipts=@()
foreach($f in $kernel|Where-Object LastWriteTimeUtc -ge $stageUtc){
 $dest=Join-Path $root ('Kernel-'+$f.Name);Copy-Item $f.FullName $dest -Force
 $sha=(Get-FileHash $f.FullName).Hash;if((Get-FileHash $dest).Hash -ne $sha){throw 'kernel dump copy mismatch'}
 $dumpReceipts+=[ordered]@{Source=$f.FullName;Name=('Kernel-'+$f.Name);Bytes=$f.Length;SHA256=$sha;LastWriteTimeUtc=$f.LastWriteTimeUtc}
}
$dumpReceipts|ConvertTo-Json -Depth 4|Set-Content (Join-Path $root 'kernel-dumps.json')

$artifact=@(Get-ChildItem $root -File|ForEach-Object {[ordered]@{Name=$_.Name;Bytes=$_.Length;SHA256=(Get-FileHash $_.FullName).Hash}})
$artifact|ConvertTo-Json -Depth 4|Set-Content (Join-Path $root 'artifact-hashes.json')
Get-Content (Join-Path $root 'state.json')
Get-Content (Join-Path $root 'artifact-hashes.json')
