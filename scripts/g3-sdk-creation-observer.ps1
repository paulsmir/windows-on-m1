[CmdletBinding()]
param([Parameter(Mandatory=$true)][string]$ExperimentDirectory,
      [Parameter(Mandatory=$true)][string]$ExpectedBoot,
      [Parameter(Mandatory=$true)][string]$HelperSHA256)
$ErrorActionPreference='Stop'
$b=[IO.Path]::GetFullPath($ExperimentDirectory)
$boot=(Get-CimInstance Win32_OperatingSystem).LastBootUpTime.ToUniversalTime().ToString('o')
if($boot -ne $ExpectedBoot -or [Diagnostics.Process]::GetCurrentProcess().SessionId -ne 1){throw 'console boot/session mismatch'}
$exe=Join-Path $b 'FullscreenSdkPresentProbe.exe'
$helper=Join-Path $b 'g3-sdk-stall-minidump.ps1'
$sha='e1d5875c6d383212361e13be4342ff0dbd86fc53ac79a085143b444556c05aa7'
if((Get-FileHash $exe).Hash -ne $sha -or (Get-FileHash $helper).Hash -ne $HelperSHA256){throw 'exact workload/helper hash'}
$ready=Join-Path $b 'observer-ready.txt';$pidFile=Join-Path $b 'sdk-pid.json';$dump=Join-Path $b 'sdk-creation.dmp'
foreach($f in @($ready,$pidFile,$dump)){if(Test-Path -LiteralPath $f){throw 'existing one-shot state'}}
$args='-NoProfile -ExecutionPolicy Bypass -File "'+$helper+'" -ExpectedImagePath "'+$exe+'" -ExpectedImageSha256 '+$sha+' -DumpPath "'+$dump+'" -ProcessIdFile "'+$pidFile+'" -ReadyPath "'+$ready+'" -EarlyEvidenceDirectory "'+(Join-Path $b 'early-evidence')+'" -StageTracePath "'+(Join-Path 'C:\ProgramData' ((Split-Path $b -Leaf)+'-Trace\umd.log'))+'"'
$observer=Start-Process powershell.exe -ArgumentList $args -PassThru -RedirectStandardOutput (Join-Path $b 'observer.out') -RedirectStandardError (Join-Path $b 'observer.err')
$deadline=[DateTime]::UtcNow.AddSeconds(20)
while(-not (Test-Path -LiteralPath $ready)){
 if($observer.HasExited -or [DateTime]::UtcNow -gt $deadline){throw 'observer not ready; no SDK launch'}
 Start-Sleep -Milliseconds 100
}
$client=Start-Process $exe -PassThru -RedirectStandardOutput (Join-Path $b 'fullscreen.out') -RedirectStandardError (Join-Path $b 'fullscreen.err')
try {
$record=[ordered]@{ProcessId=$client.Id;StartTimeUtc=$client.StartTime.ToUniversalTime().ToString('o');Boot=$boot;ExeSHA256=$sha;ObserverPID=$observer.Id}
$record|ConvertTo-Json|Set-Content (Join-Path $b 'sdk-pid.tmp')
Move-Item (Join-Path $b 'sdk-pid.tmp') $pidFile
if(-not $observer.WaitForExit(43000)){throw 'observer did not finish; collect existing files without retry'}
$observer.Refresh()
[ordered]@{Utc=[DateTime]::UtcNow.ToString('o');Boot=$boot;SdkPID=$client.Id;ObserverPID=$observer.Id;ObserverExit=$observer.ExitCode;ClientExited=$client.HasExited;Files=@(Get-ChildItem $b -File|Where-Object Name -in @('sdk-creation.dmp','observer.out','observer.err','sdk-pid.json','sdk-creation.dmp.checkpoint.json')|ForEach-Object {[ordered]@{Name=$_.Name;Bytes=$_.Length;SHA256=(Get-FileHash $_.FullName).Hash}})}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $b 'snapshot-receipt.json')
} finally {
 # The wrapper owns this held process object; never leave it running on a
 # receipt/observer failure. No other process or driver is terminated.
 if(-not $client.HasExited){
  if($client.StartTime.ToUniversalTime().ToString('o') -ne $record.StartTimeUtc -or [IO.Path]::GetFullPath($client.MainModule.FileName) -ne [IO.Path]::GetFullPath($exe)){throw 'owned SDK stop guard'}
  $client.Kill();$client.WaitForExit(5000)|Out-Null
 }
}
[ordered]@{Utc=[DateTime]::UtcNow.ToString('o');ProcessId=$client.Id;Exit=$client.ExitCode;Action='Owned diagnostic SDK ended after snapshot';Files=@(foreach($n in @('fullscreen.out','fullscreen.err')){$p=Join-Path $b $n;[ordered]@{Name=$n;Bytes=(Get-Item $p).Length;SHA256=(Get-FileHash $p).Hash}})}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $b 'fullscreen-receipt.json')
exit 0
