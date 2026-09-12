$ErrorActionPreference='Stop'
$root='C:\Users\pauls\AD04-fullcompiler-001'
$tool='C:\VS2022Community\VC\Tools\MSVC\14.44.35207'
$kit='C:\Program Files (x86)\Windows Kits\10'
$clang='C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin'
$env:PATH="$root\venv\Scripts;$clang;$tool\bin\HostX64\x64;"+$env:PATH
$env:INCLUDE="$tool\include;$kit\Include\10.0.26100.0\ucrt;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um"
$env:LIB="$tool\lib\x64;$kit\Lib\10.0.26100.0\ucrt\x64;$kit\Lib\10.0.26100.0\um\x64"
$out=Join-Path $root ('build-nir-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $out | Out-Null
$args=@('-C',"$root\nir-x64",'-j','4','-k','1','src/compiler/nir/libnir.a','src/compiler/libcompiler.a','src/util/libmesa_util.a')
$p=Start-Process -FilePath "$root\venv\Scripts\ninja.exe" -ArgumentList $args -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$out\stdout.log" -RedirectStandardError "$out\stderr.log"
@{command='BUILD_NIR_LIBRARIES';exit=$p.ExitCode;output=$out;arguments=$args}|ConvertTo-Json|Set-Content "$out\result.json"
Write-Output "BUILD_EXIT=$($p.ExitCode) EVIDENCE=$out"
Get-Content "$out\stdout.log" -Tail 18
Get-Content "$out\stderr.log" -Tail 4
exit $p.ExitCode
