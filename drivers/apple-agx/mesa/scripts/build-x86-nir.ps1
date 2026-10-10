# Build the x86 Mesa tree configured by configure-x86-nir.ps1 (all targets,
# including libgallium_wgl/opengl32 softpipe controls and the static GL/NIR
# libraries the x86 ICD links). Writes ninja output beside the build.
$ErrorActionPreference='Stop'
$root='C:\Users\pauls\AD04-fullcompiler-001'
$tool='C:\VS2022Community\VC\Tools\MSVC\14.44.35207'
$kit='C:\Program Files (x86)\Windows Kits\10'
$clang='C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin'
$env:PATH="$root\winflex;$root\venv\Scripts;$clang;$tool\bin\HostX64\x86;$tool\bin\HostX64\x64;"+$env:PATH
$env:INCLUDE="$tool\include;$kit\Include\10.0.26100.0\ucrt;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um"
$env:LIB="$tool\lib\x86;$kit\Lib\10.0.26100.0\ucrt\x86;$kit\Lib\10.0.26100.0\um\x86"
$log="$root\nir-x86\ninja-build.log"
& "$root\venv\Scripts\ninja.exe" -C "$root\nir-x86" -k 0 *> $log
$code=$LASTEXITCODE
"NINJA_EXIT=$code"
Select-String -Path $log -Pattern 'error:|FAILED:' | Select -First 20 | %{ $_.Line.Substring(0,[Math]::Min(300,$_.Line.Length)) }
Get-Content $log -Tail 3
exit $code
