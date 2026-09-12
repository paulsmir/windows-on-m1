param([ValidateSet('x64','arm64')][string]$Architecture='x64')
$ErrorActionPreference='Stop'
$root='C:\Users\pauls\AD04-fullcompiler-001'
$tool='C:\VS2022Community\VC\Tools\MSVC\14.44.35207'
$kit='C:\Program Files (x86)\Windows Kits\10'
$clang='C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin'
$env:PATH="$clang;$tool\bin\HostX64\x64;"+$env:PATH
$env:INCLUDE="$tool\include;$kit\Include\10.0.26100.0\ucrt;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um"
$env:LIB="$tool\lib\$Architecture;$kit\Lib\10.0.26100.0\ucrt\$Architecture;$kit\Lib\10.0.26100.0\um\$Architecture"
& "$root\venv\Scripts\python.exe" "$root\build-full-asahi.py" --architecture $Architecture
exit $LASTEXITCODE
