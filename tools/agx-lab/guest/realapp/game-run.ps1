# Real-application run (EXP1876G origin): run an application on the installed package and record liveness, screenshots and WER.
# Either -Exe (started directly, with APPLE_AGX_UMD_TRACE_FILE set for it) or -SteamApp (steam.exe -applaunch;
# the trace then comes from the user-level variable that trace-env-on.ps1 sets). -ProcName is the process to watch.
param([Parameter(Mandatory=$true)][string]$Dir,[Parameter(Mandatory=$true)][string]$TraceDir,[Parameter(Mandatory=$true)][string]$Tag,[Parameter(Mandatory=$true)][string]$ProcName,[string]$Exe='',[string]$SteamApp='',
      [string]$AppArgs='',[int]$Ticks=12,[int]$TickSeconds=10)
$ErrorActionPreference='Stop'
$b=$Dir;$trace=$TraceDir
$r=[ordered]@{Utc=[DateTime]::UtcNow.ToString('o');Tag=$Tag;Stage='PREFLIGHT';Pid=$null;Samples=@();Error=$null;ExitCode=$null;Wer=@();CloseRequested=$null;Terminated=$null}
$owned=$null
try {
 if([int](Get-PnpDevice -InstanceId 'ACPI\APPL0002\0').Problem -ne 0){throw 'PACKAGE_GATE'}
 New-Item -ItemType Directory -Force $trace | Out-Null
 $start=[DateTime]::Now
 $r.Stage='LAUNCH'
 if($Exe){
  $env:APPLE_AGX_UMD_TRACE_FILE="$trace\$Tag-umd.log"
  $dir=Split-Path $Exe -Parent
  $p=if($AppArgs){Start-Process $Exe -ArgumentList $AppArgs -WorkingDirectory $dir -PassThru}else{Start-Process $Exe -WorkingDirectory $dir -PassThru}
  $owned=$p.Id
 } else {
  $sa="-applaunch $SteamApp"; if($AppArgs){$sa="$sa $AppArgs"}
  Start-Process 'C:\Program Files (x86)\Steam\steam.exe' -ArgumentList $sa | Out-Null
  for($w=0;$w -lt 60 -and !$owned;$w++){Start-Sleep -Seconds 2;$q=Get-Process -Name $ProcName -ErrorAction SilentlyContinue|Select -First 1;if($q){$owned=$q.Id}}
  if(!$owned){throw "PROCESS_NOT_STARTED $ProcName"}
 }
 $r.Pid=$owned;$r.Stage='MONITOR'
 Add-Type -AssemblyName System.Drawing,System.Windows.Forms
 for($i=0;$i -lt $Ticks;$i++){
  Start-Sleep -Seconds $TickSeconds
  $live=Get-Process -Id $owned -ErrorAction SilentlyContinue
  $r.Samples+=@{ElapsedSeconds=($i+1)*$TickSeconds;Alive=[bool]$live;WindowTitle=if($live){[string]$live.MainWindowTitle}else{''};WorkingSetMB=if($live){[int]($live.WorkingSet64/1MB)}else{0}}
  try{$bounds=[System.Windows.Forms.Screen]::PrimaryScreen.Bounds
   $bmp=New-Object System.Drawing.Bitmap $bounds.Width,$bounds.Height
   $g=[System.Drawing.Graphics]::FromImage($bmp);$g.CopyFromScreen($bounds.Location,[System.Drawing.Point]::Empty,$bounds.Size)
   $bmp.Save((Join-Path $b ('shot-{0}-{1:D3}.png' -f $Tag,(($i+1)*$TickSeconds))),[System.Drawing.Imaging.ImageFormat]::Png);$g.Dispose();$bmp.Dispose()
  }catch{}
  if(!$live){break}
 }
 if($owned -and ($live=Get-Process -Id $owned -ErrorAction SilentlyContinue)){
  $r.CloseRequested=[bool]$live.CloseMainWindow()
  for($w=0;$w -lt 15 -and (Get-Process -Id $owned -ErrorAction SilentlyContinue);$w++){Start-Sleep -Seconds 1}
  $r.Terminated=$false
  if(Get-Process -Id $owned -ErrorAction SilentlyContinue){$r.Terminated=$true;Stop-Process -Id $owned -Force -ErrorAction SilentlyContinue}
 }
 try{if($p){$p.Refresh();if($p.HasExited){$r.ExitCode=[int64]$p.ExitCode}}}catch{}
 $r.Wer=@(Get-WinEvent -FilterHashtable @{LogName='Application';StartTime=$start} -ErrorAction SilentlyContinue|Where-Object {$_.ProviderName -in 'Application Error','Application Hang'}|Select-Object -First 4|ForEach-Object {[string]$_.Message})
 $r.Stage='MONITOR_DONE'
} catch {$r.Error=[string]$_.Exception.Message;$r.Stage='ERROR'} finally {
 if($owned -and (Get-Process -Id $owned -ErrorAction SilentlyContinue)){Stop-Process -Id $owned -Force -ErrorAction SilentlyContinue}
 $r.EndUtc=[DateTime]::UtcNow.ToString('o')
 [IO.File]::WriteAllText((Join-Path $b "game-$Tag.json"),($r|ConvertTo-Json -Depth 4))
}
