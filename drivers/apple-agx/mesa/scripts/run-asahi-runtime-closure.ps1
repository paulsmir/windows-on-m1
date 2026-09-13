param(
  [Parameter(Mandatory=$true)][string]$Project,
  [Parameter(Mandatory=$true)][string]$RunId,
  [ValidateSet('x64','arm64')][string]$Architecture='x64',
  [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-f]{64}$')][string]$ArchiveSha256
)
$ErrorActionPreference='Stop'
if($RunId -notmatch '^[A-Za-z0-9-]+$'){ throw 'Invalid RunId' }
$root='C:\Users\pauls\AD04-fullcompiler-001'
$output=Join-Path $root ('asahi-runtime-'+$Architecture+'-'+$RunId)
if(Test-Path $output){ throw 'Fresh RunId required' }
$python=Join-Path $root 'venv\Scripts\python.exe'
$temporaryRunnerLog=Join-Path $root ('asahi-runtime-'+$Architecture+'-'+$RunId+'.runner.log')
if(Test-Path $temporaryRunnerLog){ throw 'Fresh runner log path required' }
& $python "$Project\drivers\apple-agx\mesa\scripts\build-asahi-runtime-closure.py" --project $Project --architecture $Architecture --output $output *> $temporaryRunnerLog
$exit=$LASTEXITCODE
$runnerLog=Join-Path $output 'runner.log'
if(-not (Test-Path $output)){ [void](New-Item -ItemType Directory $output) }
Move-Item -LiteralPath $temporaryRunnerLog -Destination $runnerLog
$summary=@{project=$Project; run_id=$RunId; architecture=$Architecture; output=$output; archive_sha256=$ArchiveSha256; runner_log=$runnerLog; exit=$exit}
$summary | ConvertTo-Json | Set-Content (Join-Path $output 'runner-result.json')
$summary | ConvertTo-Json
Get-Content $runnerLog
exit $exit
