[CmdletBinding()]
param([Parameter(Mandatory=$true)][int]$TargetProcessId,
      [Parameter(Mandatory=$true)][string]$ExpectedImagePath,
      [Parameter(Mandatory=$true)][string]$ExpectedImageSha256,
      [Parameter(Mandatory=$true)][string]$DumpPath)
$ErrorActionPreference='Stop'
if($TargetProcessId -le 0 -or $ExpectedImageSha256 -notmatch '^[0-9a-fA-F]{64}$') {throw 'invalid identity'}
if(Test-Path -LiteralPath $DumpPath){throw 'dump exists; inspect instead of overwriting'}
$process=Get-Process -Id $TargetProcessId
$actual=[IO.Path]::GetFullPath($process.MainModule.FileName)
$expected=[IO.Path]::GetFullPath($ExpectedImagePath)
if($actual -ne $expected -or (Get-FileHash -LiteralPath $actual).Hash -ne $ExpectedImageSha256){throw 'not the exact owned SDK workload'}
if($process.SessionId -ne 1){throw 'expected console workload'}
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
