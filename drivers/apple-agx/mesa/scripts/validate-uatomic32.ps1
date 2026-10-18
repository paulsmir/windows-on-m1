$ErrorActionPreference='Stop'
$root='C:\Users\pauls\AD04-fullcompiler-001'
$tool='C:\VS2022Community\VC\Tools\MSVC\14.44.35207'
$kit='C:\Program Files (x86)\Windows Kits\10'
$clang='C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin'
$env:PATH="$clang;$tool\bin\HostX64\x64;"+$env:PATH
$env:INCLUDE="$tool\include;$kit\Include\10.0.26100.0\ucrt;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um"
$env:LIB="$tool\lib\x64;$kit\Lib\10.0.26100.0\ucrt\x64;$kit\Lib\10.0.26100.0\um\x64"
$out=Join-Path $root ('atomic32-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $out | Out-Null
$rows=@()
foreach($arch in @('x64','arm64')) {
  $triple=if($arch -eq 'x64'){'x86_64-pc-windows-msvc'}else{'aarch64-pc-windows-msvc'}
  & "$clang\clang-cl.exe" /nologo /std:c11 /W4 /WX "--target=$triple" /c "$root\uatomic32_semantics_test.c" "/Fo$out\$arch.obj" *> "$out\$arch.log"
  $code=$LASTEXITCODE
  $rows+=@{name="$arch-compile";exit=$code}
  if($code -ne 0){$rows|ConvertTo-Json|Set-Content "$out\result.json";Get-Content "$out\$arch.log";exit $code}
}
& "$clang\clang-cl.exe" /nologo "$out\x64.obj" "/Fe$out\atomic32.exe" *> "$out\link.log"
$rows+=@{name='x64-link';exit=$LASTEXITCODE}
if($LASTEXITCODE -ne 0){$rows|ConvertTo-Json|Set-Content "$out\result.json";exit 1}
& "$out\atomic32.exe"
$rows+=@{name='x64-execute';exit=$LASTEXITCODE}
$rows|ConvertTo-Json|Set-Content "$out\result.json"
Write-Output "EVIDENCE=$out"
Get-Content "$out\result.json"
if(@($rows|Where-Object {$_.exit -ne 0}).Count){exit 1}
