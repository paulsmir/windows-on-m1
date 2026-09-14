param(
  [Parameter(Mandatory=$true)][string]$Project,
  [Parameter(Mandatory=$true)][string]$RunId,
  [ValidateSet('x64','arm64')][string]$Architecture='x64',
  [Parameter(Mandatory=$true)][ValidatePattern('^[0-9a-f]{64}$')][string]$ArchiveSha256,
  [string]$NativeSource,
  [uint32]$ExpectedCandidateBuild=0
)
$ErrorActionPreference='Stop'
if($RunId -notmatch '^[A-Za-z0-9-]+$'){ throw 'Invalid RunId' }
$root='C:\Users\pauls\AD04-fullcompiler-001'
$output=Join-Path $root ('asahi-runtime-'+$Architecture+'-'+$RunId)
if(Test-Path $output){ throw 'Fresh RunId required' }
$python=Join-Path $root 'venv\Scripts\python.exe'
$prepareLog=Join-Path $root ('asahi-runtime-'+$Architecture+'-'+$RunId+'.prepare.log')
$temporaryRunnerLog=Join-Path $root ('asahi-runtime-'+$Architecture+'-'+$RunId+'.runner.log')
foreach($path in @($prepareLog,$temporaryRunnerLog)) {
  if(Test-Path $path){throw 'Fresh runner log path required'}
}
$summary=@{project=$Project;run_id=$RunId;architecture=$Architecture;output=$output;
  archive_sha256=$ArchiveSha256;hardware='NOT_RUN';execution='NOT_RUN';exit=1}
$exit=1
try {
  if(!$NativeSource) {
    $NativeSource=Join-Path $root ('asahi-native-'+$Architecture+'-'+$RunId)
    if(Test-Path $NativeSource){throw 'Fresh native source path required'}
    & $python "$Project\drivers\apple-agx\mesa\scripts\build-native-asahi-state.py" --project $Project --architecture $Architecture --output $NativeSource --windows-platform-declarations --native-batch-lifecycle --prepare-only *> $prepareLog
    $summary.prepare_exit=$LASTEXITCODE
    if($LASTEXITCODE){throw "Native source preparation failed: $LASTEXITCODE"}
  }
  $summary.native_source=$NativeSource
  & $python "$Project\drivers\apple-agx\mesa\scripts\build-asahi-runtime-closure.py" --project $Project --architecture $Architecture --native-source $NativeSource --output $output *> $temporaryRunnerLog
  $summary.archive_exit=$LASTEXITCODE
  if($LASTEXITCODE){throw "Native runtime archive failed: $LASTEXITCODE"}
  $manifest=Get-Content (Join-Path $output 'result.json') -Raw | ConvertFrom-Json
  if((Get-FileHash -LiteralPath $manifest.library.path -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifest.library.sha256){
    throw 'Native runtime library changed before executable link'
  }
  $props=Join-Path $output 'NativeRuntime.props'
  if((Get-FileHash -LiteralPath $props -Algorithm SHA256).Hash.ToLowerInvariant() -ne $manifest.props.sha256){
    throw 'Native runtime link inputs changed before executable link'
  }
  $vswhere='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
  $msbuild=& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
  if(!$msbuild){throw 'MSBuild unavailable'}
  $platform=if($Architecture -eq 'arm64'){'ARM64'}else{'x64'}
  $exeRoot=Join-Path $output 'executable'
  [void](New-Item -ItemType Directory $exeRoot)
  $arguments=@("$Project\drivers\apple-agx\render-admission\umd\tests\UmdContractTest.vcxproj",
    '/t:Build','/nr:false','/m:2','/p:Configuration=Release',"/p:Platform=$platform",
    '/p:EnableNativeRuntimeTest=true','/p:EnableMesaPipeFactoryTest=true',
    '/p:EnableMesaD3d10FrontendTest=true',"/p:NativeRuntimeProps=$props",
    '/p:MesaSourceRoot=C:\Users\pauls\AD04-d3d10-frontend-build\mesa',
    '/p:MesaGeneratedRoot=C:\Users\pauls\AD04-asahi-windows-compiler\b5\generated',
    "/p:NativeObjectRoot=$NativeSource","/p:IntDir=$exeRoot\obj\","/p:OutDir=$exeRoot\")
  $arguments | ConvertTo-Json | Set-Content (Join-Path $output 'build-command.json')
  & $msbuild @arguments *> (Join-Path $output 'build.log')
  $summary.build_exit=$LASTEXITCODE
  if($LASTEXITCODE){throw "Integrated native executable link failed: $LASTEXITCODE"}
  $exe=Join-Path $exeRoot 'UmdContractTest.exe'
  $summary.exe_sha256=(Get-FileHash -LiteralPath $exe -Algorithm SHA256).Hash.ToLowerInvariant()
  if($Architecture -eq 'x64') {
    $executionScript=Join-Path $output 'execute-native.py'
    @'
import sys,subprocess,json,hashlib
from pathlib import Path
exe=Path(sys.argv[1]);out=Path(sys.argv[2])
with (out/'test.log').open('wb') as stdout, (out/'test.stderr.log').open('wb') as stderr:
    try:
        result=subprocess.run([str(exe)],cwd=out,stdout=stdout,stderr=stderr,timeout=120)
        code=result.returncode
    except subprocess.TimeoutExpired:
        code='TIMEOUT'
(out/'execution.json').write_text(json.dumps({'exit':code,'exe_sha256':hashlib.sha256(exe.read_bytes()).hexdigest()}))
sys.exit(0 if code==0 else 1)
'@ | Set-Content -Encoding UTF8 $executionScript
    & $python $executionScript $exe $output
    $execution=Get-Content (Join-Path $output 'execution.json') -Raw | ConvertFrom-Json
    $summary.execution=$execution.exit
    if($LASTEXITCODE -ne 0 -or $summary.execution -ne 0){throw "Integrated native executable failed: $($summary.execution)"}
    # A stale vcxproj may accept an unknown /p option. Require the actual native
    # producer test's own receipt before describing this run as executed.
    if(!(Select-String -LiteralPath (Join-Path $output 'test.log') -Pattern '^NATIVE_RUNTIME_EXECUTION:' -Quiet)) {
      throw 'Actual native runtime execution receipt missing'
    }
  }
  # Build the existing real-KMT client against this exact native archive. This
  # is a link gate only; never execute its hardware mode on the builder.
  $clientRoot=Join-Path $output 'native-client'
  [void](New-Item -ItemType Directory $clientRoot)
  $clientArguments=@("$Project\drivers\apple-agx\windows\one-shot\AppleAgxD3dKmRender.vcxproj",
    '/t:Build','/nr:false','/m:2','/p:Configuration=Release',"/p:Platform=$platform",
    '/p:EnableNativeBatch=true',"/p:NativeRuntimeProps=$props",
    "/p:AdmissionExpectedBuild=$ExpectedCandidateBuild",
    "/p:IntDir=$clientRoot\obj\","/p:OutDir=$clientRoot\")
  $clientArguments | ConvertTo-Json | Set-Content (Join-Path $output 'native-client-build-command.json')
  & $msbuild @clientArguments *> (Join-Path $output 'native-client-build.log')
  $summary.native_client_build_exit=$LASTEXITCODE
  $summary.native_client_expected_build=$ExpectedCandidateBuild
  $summary.native_client_execution='NOT_RUN'
  if($LASTEXITCODE){throw "Native KMT client link failed: $LASTEXITCODE"}
  $clientExe=Join-Path $clientRoot 'AppleAgxD3dKmRender.exe'
  $summary.native_client_sha256=(Get-FileHash -LiteralPath $clientExe -Algorithm SHA256).Hash.ToLowerInvariant()
  $summary.native_client_path=$clientExe
  $exit=0
} catch {
  $summary.error=$_.Exception.Message
} finally {
  if(!(Test-Path $output)){[void](New-Item -ItemType Directory $output)}
  foreach($item in @(@($prepareLog,'prepare.log'),@($temporaryRunnerLog,'runner.log'))) {
    if(Test-Path $item[0]){Move-Item -LiteralPath $item[0] -Destination (Join-Path $output $item[1])}
  }
  if($NativeSource) {
    foreach($name in @('inputs.json','result.json')) {
      $path=Join-Path $NativeSource $name
      if(Test-Path $path){Copy-Item -LiteralPath $path -Destination (Join-Path $output ('native-'+$name))}
    }
  }
  $summary.exit=$exit
  $summary | ConvertTo-Json | Set-Content (Join-Path $output 'runner-result.json')
  $summary | ConvertTo-Json
}
if($exit) {
  foreach($name in @('prepare.log','runner.log','build.log','test.log','test.stderr.log','native-client-build.log')) {
    $path=Join-Path $output $name
    if(Test-Path $path){Get-Content -LiteralPath $path -Tail 35}
  }
}
exit $exit
