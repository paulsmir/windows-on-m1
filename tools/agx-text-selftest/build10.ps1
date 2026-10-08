$ErrorActionPreference='Stop'
$vs=& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$here=Split-Path -Parent $MyInvocation.MyCommand.Path
cmd /c "`"$vs\VC\Auxiliary\Build\vcvarsall.bat`" x64_arm64 && cd /d `"$here`" && cl /nologo /EHsc /O2 /W3 agx_dupcap.cpp /Fe:agx_dupcap.exe"
if($LASTEXITCODE){throw "build failed $LASTEXITCODE"}
(Get-FileHash "$here\agx_dupcap.exe").Hash
