[CmdletBinding()]
param([int]$TargetProcessId,
      [Parameter(Mandatory=$true)][string]$ExpectedImagePath,
      [Parameter(Mandatory=$true)][string]$ExpectedImageSha256,
      [Parameter(Mandatory=$true)][string]$DumpPath,
      [string]$ProcessIdFile, [string]$ReadyPath, [string]$EarlyEvidenceDirectory)
$ErrorActionPreference='Stop'
Add-Type @'
using System;
using System.Runtime.InteropServices;
using Microsoft.Win32.SafeHandles;
public static class J313SdkDump {
 [DllImport("kernel32.dll", SetLastError=true)]
 [return: MarshalAs(UnmanagedType.Bool)]
 public static extern bool IsProcessCritical(IntPtr process, out bool critical);
 [DllImport("dbghelp.dll", SetLastError=true)]
 [return: MarshalAs(UnmanagedType.Bool)]
 public static extern bool MiniDumpWriteDump(IntPtr process, uint id,
     SafeFileHandle file, uint type, IntPtr exception, IntPtr streams, IntPtr callbacks);
}
'@
if($ProcessIdFile){
 if(-not $ReadyPath -or (Test-Path -LiteralPath $ReadyPath)){throw 'observer readiness identity'}
 [DateTime]::UtcNow.ToString('o')|Set-Content -LiteralPath $ReadyPath
 $deadline=[DateTime]::UtcNow.AddSeconds(25)
 while(-not (Test-Path -LiteralPath $ProcessIdFile)){
  if([DateTime]::UtcNow -gt $deadline){throw 'SDK PID not published'}
  Start-Sleep -Milliseconds 100
 }
 $identity=Get-Content -LiteralPath $ProcessIdFile -Raw|ConvertFrom-Json
 $TargetProcessId=[int]$identity.ProcessId
 $process=Get-Process -Id $TargetProcessId
 $handle=$process.Handle
 if($process.StartTime.ToUniversalTime().ToString('o') -ne $identity.StartTimeUtc){throw 'SDK PID reused'}
 Start-Sleep -Seconds 5
}
if($TargetProcessId -le 0 -or $ExpectedImageSha256 -notmatch '^[0-9a-fA-F]{64}$') {throw 'invalid identity'}
if(Test-Path -LiteralPath $DumpPath){throw 'dump exists; inspect instead of overwriting'}
if(-not $process){$process=Get-Process -Id $TargetProcessId}
$actual=[IO.Path]::GetFullPath($process.MainModule.FileName)
$expected=[IO.Path]::GetFullPath($ExpectedImagePath)
if($actual -ne $expected -or (Get-FileHash -LiteralPath $actual).Hash -ne $ExpectedImageSha256){throw 'not the exact owned SDK workload'}
if($process.SessionId -ne 1){throw 'expected console workload'}
# Hold this exact process handle throughout the snapshot; never terminate it.
$handle=$process.Handle
$critical=$false
if(-not [J313SdkDump]::IsProcessCritical($handle,[ref]$critical) -or $critical){throw 'critical-process guard'}
$before=[DateTime]::UtcNow.ToString('o')
$file=[IO.File]::Open($DumpPath,[IO.FileMode]::CreateNew,[IO.FileAccess]::Write,[IO.FileShare]::None)
try {
 # MiniDumpNormal plus MiniDumpWithThreadInfo; no full-memory/token dump.
 if(-not [J313SdkDump]::MiniDumpWriteDump($handle,[uint32]$TargetProcessId,$file.SafeFileHandle,0x1000,[IntPtr]::Zero,[IntPtr]::Zero,[IntPtr]::Zero)){
  $errorCode=[Runtime.InteropServices.Marshal]::GetLastWin32Error()
  throw ('MiniDumpWriteDump failed: 0x{0:x8}' -f $errorCode)
 }
 $file.Flush($true)
} finally {$file.Dispose()}
[ordered]@{UtcBefore=$before;UtcAfter=[DateTime]::UtcNow.ToString('o');ProcessId=$TargetProcessId;Image=$actual;ImageSHA256=$ExpectedImageSha256;Dump=$DumpPath;Bytes=(Get-Item -LiteralPath $DumpPath).Length;SHA256=(Get-FileHash -LiteralPath $DumpPath).Hash;Type=0x1000;Limitation='Stack snapshot is diagnostic; no process termination or display success claim'}|ConvertTo-Json

if($EarlyEvidenceDirectory){
 New-Item -ItemType Directory -Path $EarlyEvidenceDirectory -ErrorAction Stop|Out-Null
 $null=& logman.exe stop EXP801DxgBoot -ets 2>&1
 $stopExit=$LASTEXITCODE
 if($stopExit -ne 0){throw "early ETL stop failed: $stopExit"}
 $files=@(Get-ChildItem C:\Windows\System32\LogFiles\WMI -Filter 'EXP801DxgBoot.etl*')
 foreach($item in $files){Copy-Item -LiteralPath $item.FullName -Destination $EarlyEvidenceDirectory}
 $trace=Join-Path 'C:\ProgramData' ((Split-Path ([IO.Path]::GetDirectoryName($DumpPath)) -Leaf)+'-Trace\umd.log')
 if(Test-Path -LiteralPath $trace){Copy-Item -LiteralPath $trace -Destination (Join-Path $EarlyEvidenceDirectory 'umd.log')}
 $receipts=@(Get-ChildItem -LiteralPath $EarlyEvidenceDirectory -File|ForEach-Object {[ordered]@{Name=$_.Name;Bytes=$_.Length;SHA256=(Get-FileHash -LiteralPath $_.FullName).Hash}})
 [ordered]@{Utc=[DateTime]::UtcNow.ToString('o');Boot=$identity.Boot;TraceStopExit=$stopExit;Files=$receipts}|ConvertTo-Json -Depth 5|Set-Content (Join-Path $EarlyEvidenceDirectory 'receipt.json')
}
