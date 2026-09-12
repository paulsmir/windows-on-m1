param([Parameter(Mandatory=$true)][string]$InputRoot)
$ErrorActionPreference='Stop'
$base='C:\Users\pauls\AD04-fullcompiler-001'
$mesa='C:\Users\pauls\AD04-d3d10-frontend-build\mesa'
$tool='C:\VS2022Community\VC\Tools\MSVC\14.44.35207'
$kit='C:\Program Files (x86)\Windows Kits\10'
$clang='C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin\clang-cl.exe'
$env:PATH="$tool\bin\HostX64\x64;"+$env:PATH
$env:INCLUDE="$tool\include;$kit\Include\10.0.26100.0\ucrt;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um"
$out=Join-Path $base ('reloc-capture-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $out | Out-Null
if((Get-FileHash "$InputRoot\native\agx_pack.h" -Algorithm SHA256).Hash -ne 'aedee39dd8eb0305acdd21512c1334f2923b4b3a99db0dd2ef61912471415ce8'){throw 'native pack header mismatch'}
$files=@('agx_win32_reloc_capture_test.c','agx_win32_reloc_capture.c','agx_win32_transport.c','apple_agx_win32_abi.c','apple_agx_dynamic_job.c')
$results=@()
function Invoke-ExactProcess([string]$File,[string[]]$Arguments,[string]$Log) {
  $process=New-Object System.Diagnostics.Process
  $process.StartInfo.FileName=$File
  $process.StartInfo.Arguments=$Arguments -join ' '
  $process.StartInfo.UseShellExecute=$false
  $process.StartInfo.CreateNoWindow=$true
  $process.StartInfo.RedirectStandardOutput=$true
  $process.StartInfo.RedirectStandardError=$true
  if(!$process.Start()){throw 'Process did not start'}
  $stdout=$process.StandardOutput.ReadToEndAsync()
  $stderr=$process.StandardError.ReadToEndAsync()
  $process.WaitForExit()
  [System.IO.File]::WriteAllText("$Log.stdout.log",$stdout.Result)
  [System.IO.File]::WriteAllText("$Log.stderr.log",$stderr.Result)
  $code=$process.ExitCode
  $process.Dispose()
  return $code
}
foreach($arch in @('x64','arm64')) {
  $env:LIB="$tool\lib\$arch;$kit\Lib\10.0.26100.0\ucrt\$arch;$kit\Lib\10.0.26100.0\um\$arch"
  $triple=if($arch -eq 'x64'){'x86_64-pc-windows-msvc'}else{'aarch64-pc-windows-msvc'}
  $args=@('/nologo','/std:c11','/W4','/WX','/O2',"--target=$triple",'/DHAVE_STRUCT_TIMESPEC',
    '/D_USE_MATH_DEFINES',"/FI$base\uatomic_clang_compat.h", "/I$InputRoot", "/imsvc$InputRoot\native", "/imsvc$mesa\src", "/imsvc$mesa\include")
  $args+=@($files|ForEach-Object {"$InputRoot\$_"})
  $args+=@("/Fe$out\$arch.exe","/Fo$out\\")
  $code=Invoke-ExactProcess $clang $args "$out\$arch"
  $results+=@{name="$arch-build-link";exit=$code;arguments=$args}
  if($code -ne 0){$results|ConvertTo-Json -Depth 5|Set-Content "$out\result.json";Write-Output "FIRST_FAILURE=$arch EVIDENCE=$out";Get-Content "$out\$arch.stderr.log" -Tail 14;exit $code}
  if($arch -eq 'x64') {
    $code=Invoke-ExactProcess "$out\x64.exe" @() "$out\execute"
    $results+=@{name='x64-execute';exit=$code}
    if($code -ne 0){$results|ConvertTo-Json -Depth 5|Set-Content "$out\result.json";Get-Content "$out\execute.stderr.log";exit $code}
  }
}
$results|ConvertTo-Json -Depth 5|Set-Content "$out\result.json"
Get-ChildItem $InputRoot -File|Where-Object Name -NotLike '._*'|Get-FileHash -Algorithm SHA256|Select-Object Path,Hash|ConvertTo-Json|Set-Content "$out\inputs.json"
Get-FileHash "$out\x64.exe","$out\arm64.exe" -Algorithm SHA256|Select-Object Path,Hash|ConvertTo-Json|Set-Content "$out\artifacts.json"
Write-Output "RELOC_BUILD_LINK_X64_ARM64_PASS X64_EXECUTE_PASS EVIDENCE=$out"
