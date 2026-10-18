$ErrorActionPreference='Stop'
$root='C:\Users\pauls\AD04-fullcompiler-001'
$tool='C:\VS2022Community\VC\Tools\MSVC\14.44.35207'
$kit='C:\Program Files (x86)\Windows Kits\10'
$clang='C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin'
$env:INCLUDE="$tool\include;$kit\Include\10.0.26100.0\ucrt;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um"
$out=Join-Path $root ('fpcr-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $out | Out-Null
$args=@('/nologo','/std:c11','/W4','/WX','/O2','--target=aarch64-pc-windows-msvc','/c',"$root\fpcr_compile_test.c","/Fo$out\fpcr.obj")
$p=Start-Process "$clang\clang-cl.exe" -ArgumentList $args -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$out\compile.log" -RedirectStandardError "$out\compile.stderr.log"
$code=$p.ExitCode
if($code -eq 0) {
  & "$clang\llvm-objdump.exe" -d "$out\fpcr.obj" > "$out\disassembly.txt"
  $disassembly=Get-Content "$out\disassembly.txt" -Raw
  if($LASTEXITCODE -ne 0 -or $disassembly -notmatch 'mrs\s+x[0-9]+,\s*FPCR' -or $disassembly -notmatch 'msr\s+FPCR,\s*x[0-9]+'){$code=1}
}
@{exit=$code;compile_only=$true;runtime_register_access=$false}|ConvertTo-Json|Set-Content "$out\result.json"
Write-Output "FPCR_EXIT=$code EVIDENCE=$out"
if($code -ne 0){Get-Content "$out\compile.stderr.log" -Tail 12}else{Get-Content "$out\disassembly.txt"}
exit $code
