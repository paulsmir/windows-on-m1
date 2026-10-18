param([Parameter(Mandatory=$true)][string]$SourceRoot,
      [Parameter(Mandatory=$true)][string]$EvidenceRoot)
$ErrorActionPreference='Stop'
if(Test-Path $EvidenceRoot){throw 'Evidence directory already exists'}
New-Item -ItemType Directory $EvidenceRoot | Out-Null
$v=Join-Path ${env:ProgramFiles(x86)} 'Microsoft Visual Studio\Installer\vswhere.exe'
$m=& $v -latest -products * -requires Microsoft.Component.MSBuild -find 'MSBuild\**\Bin\MSBuild.exe' | Select-Object -First 1
$t='C:\VS2022Community\VC\Tools\MSVC\14.44.35207'
$k='C:\Program Files (x86)\Windows Kits\10'
$env:PATH="$k\bin\10.0.26100.0\x64;"+$env:PATH
$inc="$t\include%3B$k\Include\10.0.26100.0\ucrt%3B$k\Include\10.0.26100.0\shared%3B$k\Include\10.0.26100.0\um%3B$k\Include\10.0.26100.0\km"
$lib="$t\lib\arm64%3B$k\Lib\10.0.26100.0\ucrt\arm64%3B$k\Lib\10.0.26100.0\um\arm64"
$umd=Join-Path $SourceRoot 'drivers\apple-agx\render-admission\umd'
& $m "$umd\AppleAgxRenderAdmissionUmd.vcxproj" /t:Build /p:Configuration=Release /p:Platform=ARM64 /p:WindowsTargetPlatformVersion=10.0.26100.0 "/p:IncludePath=$inc" "/p:LibraryPath=$lib" /p:RunCodeAnalysis=true /p:SignMode=Off /nr:false /m:2 *> "$EvidenceRoot\build.log"
$code=$LASTEXITCODE
@{buildExit=$code;scope='ARM64 UMD DLL; no installation or hardware test'} | ConvertTo-Json | Set-Content "$EvidenceRoot\result.json"
Get-Content "$EvidenceRoot\build.log" -Tail 18
exit $code
