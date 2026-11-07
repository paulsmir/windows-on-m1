# GravityMark run in the interactive session. Existing positional parameters
# retain their order. For FHD pass -Width 1920 -Height 1080 explicitly.
# -Mode FULL (default): the standard benchmark, unchanged arguments and score.
# -Mode PERF / FAST: the same benchmark arguments, closed at the mode's timeout
# (GravityMark 1.89 has no frame-count or duration option); the window closes
# normally so the UMD trace is complete. Analyse with tools/agx_gm_perf.py:
# PERF = --skip 10 --window 60 (keeps the order of full scores, ~0.7x their
# change), FAST = --skip 5 --window 20 (errors/metrics only, no speed claim).
[CmdletBinding()]
param([string]$Tag='gm',[string]$Api='d3d12',[int]$Asteroids=10000,[int]$Fullscreen=0,[int]$Timeout=300,[int]$Fl111=0,[string]$ValidateRefs='1',
      [ValidateRange(1,2147483647)][int]$Width=1280,
      [ValidateRange(1,2147483647)][int]$Height=720,
      # 2026-11-02: macOS/leaderboard scores are temporal AA off (macOS FHD 100k: -temporal 1 4397, 0 7814)
      [ValidateSet(0,1)][int]$Temporal=1,
      [ValidateSet('FULL','PERF','FAST')][string]$Mode='FULL')
# Load ~20 s + shader-compile anchor ~22 s after the first present, then skip + window + margin.
$modeTimeout=@{FULL=$Timeout;PERF=100;FAST=55}
if($Mode -ne 'FULL' -and -not $PSBoundParameters.ContainsKey('Timeout')){$Timeout=$modeTimeout[$Mode]}
$d='C:\Users\pavel\gravitymark\runs'; New-Item -ItemType Directory -Force $d | Out-Null
Remove-Item "$d\$Tag-*" -ErrorAction SilentlyContinue
$env:APPLE_AGX_UMD_TRACE_FILE="$d\$Tag-umd.log"
# EXP1920: a hung process loses its buffered trace tail; write every line at once (UMD a8cf4104+, ignored before)
$env:APPLE_AGX_UMD_TRACE_SYNC='1'
# FL 11_1 is reported only with APPLE_AGX_D3D11_FL111=1 until it is validated
if($Fl111){$env:APPLE_AGX_D3D11_FL111='1'}
# val1 (2026-10-24): the opt-in submit-reference validator names a reference missing from a submission
# ('1' = record, 'refuse' = also refuse the submission); '0' or '' leaves it off
# always set both explicitly: an inherited value must not leak into a '0' run (Codex exp1940)
if($ValidateRefs -eq 'refuse'){$env:APPLE_AGX_UMD_VALIDATE_REFS='1';$env:APPLE_AGX_UMD_VALIDATE_REFS_REFUSE='1'}elseif($ValidateRefs -and $ValidateRefs -ne '0'){$env:APPLE_AGX_UMD_VALIDATE_REFS='1';$env:APPLE_AGX_UMD_VALIDATE_REFS_REFUSE='0'}else{$env:APPLE_AGX_UMD_VALIDATE_REFS='0';$env:APPLE_AGX_UMD_VALIDATE_REFS_REFUSE='0'}
$bin='C:\Program Files\GravityMark\bin'
$a=@('-temporal',"$Temporal",'-fps','1','-info','1','-benchmark','1','-close','1','-count','1',"-$Api",'-asteroids',"$Asteroids",'-times',"$d\$Tag-times.txt",'-image',"$d\$Tag-image.png")
# Preserve app-selected fullscreen dimensions unless a size was requested.
$sized = -not $Fullscreen -or $PSBoundParameters.ContainsKey('Width') -or $PSBoundParameters.ContainsKey('Height')
if($sized){$a+=@('-width',"$Width",'-height',"$Height")}
if($Fullscreen){$a+=@('-fullscreen','1','-screen','0')}
# Record requested settings and the actual arguments before launch. The app's
# output remains the authority for the display mode it successfully creates.
[ordered]@{Tag=$Tag;Mode=$Mode;Timeout=$Timeout;Api=$Api;Asteroids=$Asteroids;DimensionsSpecified=$sized;Width=$(if($sized){$Width}else{$null});Height=$(if($sized){$Height}else{$null});Fullscreen=$Fullscreen;Arguments=$a} |
  ConvertTo-Json -Depth 3 | Out-File -Encoding utf8 -ErrorAction Stop "$d\$Tag-launch.json"
$t0=Get-Date
$p=Start-Process -FilePath "$bin\GravityMark.exe" -WorkingDirectory $bin -ArgumentList $a -PassThru -RedirectStandardOutput "$d\$Tag-out.txt" -RedirectStandardError "$d\$Tag-err.txt"
# EXP1919: a kill at the timeout lost the buffered UMD trace tail (the removal cause). Close the window after a
# "device removed" error (or at the timeout) so the process exits normally and flushes; kill only 15 s later.
$state=$null
while(-not $p.WaitForExit(1000)){
  $removed=(Test-Path "$d\$Tag-err.txt") -and (Select-String -Path "$d\$Tag-err.txt" -Pattern 'device removed' -Quiet)
  if($removed -or ((Get-Date)-$t0).TotalSeconds -ge $Timeout){
    $state=if($removed){'REMOVED'}else{'TIMEOUT'}
    [void]$p.CloseMainWindow()
    if($p.WaitForExit(15000)){ $state+=' CLOSED' } else { $p.Kill(); $state+=' KILLED' }
    break } }
if(-not $state){ $state='EXIT '+$p.ExitCode }
$wer=Get-WinEvent -FilterHashtable @{LogName='Application';Id=1000;StartTime=$t0} -ErrorAction SilentlyContinue | %{ ($_.Message -split "`n")[0..3] -join ' ' }
"$state seconds $([int]((Get-Date)-$t0).TotalSeconds) wer $($wer.Count)" | Out-File "$d\$Tag-done.txt"
$wer | Out-File -Append "$d\$Tag-done.txt"
# PERF/FAST end at the timeout by design: a normal close there is the expected result.
$ok = if($Mode -eq 'FULL'){ $state -like 'EXIT *' } else { $state -eq 'TIMEOUT CLOSED' -or $state -like 'EXIT *' }
[ordered]@{Tag=$Tag;Mode=$Mode;Timeout=$Timeout;State=$state;Ok=[bool]$ok;Wer=@($wer).Count;Seconds=[int]((Get-Date)-$t0).TotalSeconds} |
  ConvertTo-Json | Out-File -Encoding utf8 "$d\$Tag-mode.json"
