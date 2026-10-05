$ErrorActionPreference='Stop'
$base='C:\Users\pavel\EXP865'
$root='C:\Users\pavel\EXP865-evidence'
$since=[DateTime]::Parse([string](Get-Content "$base\stage-receipt.json" -Raw|ConvertFrom-Json).Utc).ToUniversalTime()
if([int](Get-PnpDevice -InstanceId 'ACPI\APPL0002\0').Problem -ne 45){throw 'hidden Code45 required'}
$candidates=@()
foreach($dir in @('C:\Windows\LiveKernelReports','C:\ProgramData\Microsoft\Windows\WER\ReportQueue')){
 $candidates+=@(Get-ChildItem $dir -Filter '*.dmp' -File -Recurse -ErrorAction SilentlyContinue|Where-Object {$_.Name -like '*WATCHDOG*' -and $_.LastWriteTimeUtc -ge $since})
}
$receipts=@()
foreach($f in $candidates){
 $name='Live-'+$f.Name;$dest=Join-Path $root $name;$sha=(Get-FileHash $f.FullName).Hash
 if((Test-Path $dest) -and (Get-FileHash $dest).Hash -ne $sha){throw 'watchdog name collision'}
 Copy-Item $f.FullName $dest -Force
 if((Get-FileHash $dest).Hash -ne $sha){throw 'watchdog copy mismatch'}
 $wer=Join-Path $f.DirectoryName 'Report.wer'
 if(Test-Path $wer){Copy-Item $wer (Join-Path $root ($name+'.Report.wer')) -Force}
 $receipts+=[ordered]@{Source=$f.FullName;Name=$name;Bytes=$f.Length;SHA256=$sha;LastWriteTimeUtc=$f.LastWriteTimeUtc.ToString('o')}
}
$receipts|ConvertTo-Json -Depth 5|Set-Content (Join-Path $root 'current-watchdog-receipts.json')
$files=@(Get-ChildItem $root -File|Where-Object Name -ne 'artifact-hashes.json'|ForEach-Object {[ordered]@{Name=$_.Name;Bytes=$_.Length;SHA256=(Get-FileHash $_.FullName).Hash}})
$files|ConvertTo-Json -Depth 5|Set-Content (Join-Path $root 'artifact-hashes.json')
[ordered]@{Watchdogs=$receipts;Files=$files.Count;Bytes=($files|Measure-Object Bytes -Sum).Sum;ManifestSHA256=(Get-FileHash (Join-Path $root 'artifact-hashes.json')).Hash}|ConvertTo-Json -Depth 5
