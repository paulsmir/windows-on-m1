$ErrorActionPreference='Stop'
$root='C:\Users\pauls\AD04-fullcompiler-001'
$tool='C:\VS2022Community\VC\Tools\MSVC\14.44.35207'
$kit='C:\Program Files (x86)\Windows Kits\10'
$clang='C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin'
$env:PATH="$root\winflex;$root\venv\Scripts;$clang;$tool\bin\HostX64\x64;"+$env:PATH
$env:INCLUDE="$tool\include;$kit\Include\10.0.26100.0\ucrt;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um"
$env:LIB="$tool\lib\x64;$kit\Lib\10.0.26100.0\ucrt\x64;$kit\Lib\10.0.26100.0\um\x64"
if((Get-FileHash "$root\win_flex_bison-2.5.25.zip" -Algorithm SHA256).Hash -ne '8d324b62be33604b2c45ad1dd34ab93d722534448f55a16ca7292de32b6ac135'){throw 'winflex archive hash mismatch'}
if(!(Test-Path "$root\winflex")){Expand-Archive "$root\win_flex_bison-2.5.25.zip" "$root\winflex"}
# Select upstream's full graphics compiler dependency graph. Only libnir,
# libcompiler and libmesa_util are built; softpipe is not built or linked.
# No deployed driver capability or renderer selection is changed.
$out=Join-Path $root ('full-nir-config-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $out | Out-Null
$args=@('setup','--reconfigure',"$root\nir-x64",'C:\Users\pauls\AD04-d3d10-frontend-build\mesa','-Dgallium-drivers=softpipe','-Dopengl=true')
$p=Start-Process "$root\venv\Scripts\meson.exe" -ArgumentList $args -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$out\stdout.log" -RedirectStandardError "$out\stderr.log"
@{exit=$p.ExitCode;arguments=$args;scope='Full NIR compiler dependencies only; no softpipe build/link'}|ConvertTo-Json|Set-Content "$out\result.json"
Write-Output "CONFIGURE_EXIT=$($p.ExitCode) EVIDENCE=$out"
Get-Content "$out\stdout.log" -Tail 12
Get-Content "$out\stderr.log" -Tail 4
exit $p.ExitCode
