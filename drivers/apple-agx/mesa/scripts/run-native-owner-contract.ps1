param(
    [Parameter(Mandatory=$true)][string]$Project,
    [Parameter(Mandatory=$true)][string]$NativeRoot,
    [Parameter(Mandatory=$true)][string]$ResultRoot,
    [ValidateSet('x64','arm64')][string]$Architecture='x64',
    [switch]$EnableNativeStateTest
)
$ErrorActionPreference='Stop'
if ((Test-Path $NativeRoot) -or (Test-Path $ResultRoot)) { throw 'Use fresh result paths' }
[void](New-Item -ItemType Directory $ResultRoot)
$python='C:\Users\pauls\AD04-fullcompiler-001\venv\Scripts\python.exe'
$nativeStateArgs=@()
if($EnableNativeStateTest){$nativeStateArgs+= '--native-state-test'}
& $python "$Project\drivers\apple-agx\mesa\scripts\build-native-asahi-state.py" --output $NativeRoot --windows-platform-declarations --project $Project --architecture $Architecture @nativeStateArgs *> "$ResultRoot\native.log"
if ($LASTEXITCODE) { Get-Content "$ResultRoot\native.log" -Tail 50; exit $LASTEXITCODE }
$vswhere='C:\Program Files (x86)\Microsoft Visual Studio\Installer\vswhere.exe'
$msbuild=& $vswhere -latest -products '*' -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
if (!$msbuild) { throw 'MSBuild unavailable' }
$platform=if($Architecture -eq 'arm64') {'ARM64'} else {'x64'}
$arguments=@("$Project\drivers\apple-agx\render-admission\umd\tests\UmdContractTest.vcxproj",
    '/t:Build','/nr:false','/m:2','/p:Configuration=Release',"/p:Platform=$platform",
    '/p:EnableNativePoolTest=true',"/p:NativeObjectRoot=$NativeRoot",
    "/p:IntDir=$ResultRoot\obj\","/p:OutDir=$ResultRoot\")
if($EnableNativeStateTest){$arguments+= '/p:EnableNativeStateTest=true'}
$arguments | ConvertTo-Json | Set-Content "$ResultRoot\build-command.json"
& $msbuild @arguments *> "$ResultRoot\build.log"
$buildExit=$LASTEXITCODE
$result=@{architecture=$Architecture;build_exit=$buildExit;execution='NOT_RUN';hardware='NOT_RUN'}
if ($buildExit -eq 0) {
    $exe="$ResultRoot\UmdContractTest.exe"
    $result.exe_sha256=(Get-FileHash $exe -Algorithm SHA256).Hash.ToLowerInvariant()
    if($Architecture -eq 'x64') {
        # Native stderr is test evidence, not a PowerShell terminating error.
        $test=Start-Process -FilePath $exe -NoNewWindow -Wait -PassThru -RedirectStandardOutput "$ResultRoot\test.log" -RedirectStandardError "$ResultRoot\test.stderr.log"
        $result.execution=$test.ExitCode
    }
}
$result | ConvertTo-Json | Set-Content "$ResultRoot\result.json"
$result | ConvertTo-Json
if($buildExit) { Get-Content "$ResultRoot\build.log" -Tail 40; exit $buildExit }
if($result.execution -ne 'NOT_RUN' -and $result.execution -ne 0) {
    Get-Content "$ResultRoot\test.log" -Tail 40
    Get-Content "$ResultRoot\test.stderr.log" -Tail 40
    exit $result.execution
}
