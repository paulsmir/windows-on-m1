param([string]$Attempt = 'b3')
$ErrorActionPreference = 'Stop'
$root = 'C:\Users\pauls\AD04-asahi-windows-compiler'
$mesa = 'C:\Users\pauls\AD04-d3d10-frontend-build\mesa'
$output = Join-Path $root $Attempt
$generated = Join-Path $output 'generated'
$archive = Join-Path $root 'generated.tar.gz'
if (Test-Path $output) { throw "$Attempt output already exists; preserve evidence" }
if ((Get-FileHash (Join-Path $root 'LLVM-20.1.8-win64.exe') -Algorithm SHA256).Hash -ne '3197846A2B19063687DD56E93E34CD941E3548D907F23A6131571321BDF9FE7B') { throw 'LLVM archive hash mismatch' }
New-Item -ItemType Directory -Force $output, $generated | Out-Null
tar -xf $archive -C $generated
if ($LASTEXITCODE) { throw 'generated source extraction failed' }
$tool = 'C:\VS2022Community\VC\Tools\MSVC\14.44.35207'
$kit = 'C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0'
$env:INCLUDE = "$tool\include;$kit\ucrt;$kit\shared;$kit\um"
$overlay = Join-Path $output 'include\drm-uapi'
New-Item -ItemType Directory -Force $overlay | Out-Null
$fourcc = [IO.File]::ReadAllText("$mesa\include\drm-uapi\drm_fourcc.h")
if (([regex]::Matches($fourcc, '#include "drm.h"')).Count -ne 1) { throw 'fourcc source anchor changed' }
$fourcc = $fourcc.Replace('#include "drm.h"', "#include <stdint.h>`ntypedef uint64_t __u64;")
[IO.File]::WriteAllText((Join-Path $overlay 'drm_fourcc.h'), $fourcc)
$inc = @("/I$output\include", "/I$mesa\include", "/I$mesa\src", "/I$mesa\src\compiler", "/I$mesa\src\compiler\nir", "/I$mesa\src\asahi\compiler", "/I$mesa\src\asahi\libagx", "/I$generated\src", "/I$generated\src\compiler", "/I$generated\src\compiler\nir", "/I$generated\src\asahi\compiler", "/I$generated\src\asahi\libagx")
Push-Location $output
try {
  $arguments = @('/nologo', '/c', '/std:c11', '/W3', '/DNDEBUG', '/DHAVE_STRUCT_TIMESPEC', '/DMESA_DEBUG=0', '/DBUILDING_MESA') + $inc + @("$mesa\src\asahi\compiler\agx_compile.c")
  $process = Start-Process -FilePath "$root\llvm20\bin\clang-cl.exe" -ArgumentList $arguments -Wait -PassThru -NoNewWindow -RedirectStandardOutput 'compile.stdout.log' -RedirectStandardError 'compile.stderr.log'
  $result = $process.ExitCode
  Get-Content 'compile.stdout.log', 'compile.stderr.log' | Set-Content 'compile.log'
  & "$root\llvm20\bin\clang-cl.exe" --version *> 'clang-version.log'
  @{ ExitCode = $result; Compiler = 'LLVM clang-cl 20.1.8'; Target = 'x86_64-pc-windows-msvc'; Source = 'src/asahi/compiler/agx_compile.c'; SourceChanged = $false; HardwareUsed = $false } | ConvertTo-Json | Set-Content 'result.json'
  exit $result
} finally { Pop-Location }
