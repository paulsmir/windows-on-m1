$ErrorActionPreference='Stop'
$vs=& "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -products * -property installationPath
$here=Split-Path -Parent $MyInvocation.MyCommand.Path
cmd /c "`"$vs\VC\Auxiliary\Build\vcvarsall.bat`" x64_arm64 && cd /d `"$here`" && cl /nologo /EHsc /O2 /W3 agx_livedump.cpp /Fe:agx_livedump.exe"
if($LASTEXITCODE){throw "build failed $LASTEXITCODE"}
(Get-FileHash "$here\agx_livedump.exe").Hash
