param([Parameter(Mandatory=$true)][string]$SourceRoot,
      [Parameter(Mandatory=$true)][string]$EvidenceRoot,
      [ValidateSet('x64','ARM64')][string]$Platform='x64')
$ErrorActionPreference='Stop'
if(Test-Path $EvidenceRoot){throw 'Evidence directory already exists'}
New-Item -ItemType Directory $EvidenceRoot | Out-Null
$vswhere=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild=& $vswhere -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if(!$msbuild){throw 'MSBuild unavailable'}
$project=Join-Path $SourceRoot 'drivers\apple-agx\render-admission\umd\tests\UmdContractTest.vcxproj'
& $msbuild $project /t:Build /p:Configuration=Release "/p:Platform=$Platform" /m:2 /nr:false /nologo *> "$EvidenceRoot\build.log"
$result=@{platform=$Platform;buildExit=$LASTEXITCODE;execution='NOT_RUN'}
if($result.buildExit -eq 0) {
  $exe=Join-Path (Split-Path $project) "$Platform\Release\UmdContractTest.exe"
  Copy-Item $exe "$EvidenceRoot\UmdContractTest.exe"
  $result.sha256=(Get-FileHash $exe -Algorithm SHA256).Hash.ToLowerInvariant()
  if($Platform -eq 'x64') {
    $test=Start-Process $exe -PassThru -Wait -NoNewWindow -RedirectStandardOutput "$EvidenceRoot\test.log" -RedirectStandardError "$EvidenceRoot\test-error.log"
    $result.execution=$test.ExitCode
  }
}
$result | ConvertTo-Json | Set-Content "$EvidenceRoot\result.json"
$result | ConvertTo-Json
if($result.buildExit -ne 0){Get-Content "$EvidenceRoot\build.log" | Select-String 'error ' | Select-Object -First 8; exit $result.buildExit}
if($Platform -eq 'x64' -and $result.execution -ne 0){Get-Content "$EvidenceRoot\test-error.log" -Tail 15; exit $result.execution}
