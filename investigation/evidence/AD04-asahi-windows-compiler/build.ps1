param([string]$Attempt='b1')
$ErrorActionPreference='Stop'
$root='C:\Users\pauls\AD04-asahi-windows-compiler'
$mesa='C:\Users\pauls\AD04-d3d10-frontend-build\mesa'
$generated=Join-Path $root 'generated'
$output=Join-Path $root $Attempt
if(Test-Path $output){throw 'Preserve existing evidence'}
New-Item -ItemType Directory -Force $output,$generated | Out-Null
tar -xf "$root\generated.tar.gz" -C $generated
if($LASTEXITCODE){throw 'extract failed'}
$tool='C:\VS2022Community\VC\Tools\MSVC\14.44.35207'
$kit='C:\Program Files (x86)\Windows Kits\10\Include\10.0.26100.0'
$env:PATH="$tool\bin\HostX64\x64;"+$env:PATH
$env:INCLUDE="$tool\include;$kit\ucrt;$kit\shared;$kit\um"
$overlay=Join-Path $output 'include\drm-uapi'
New-Item -ItemType Directory -Force $overlay | Out-Null
# The compiler only consumes modifier constants and __u64, not DRM ioctls.
# Preserve the pinned header and all license notices; isolate this derivation.
$fourcc=[IO.File]::ReadAllText("$mesa\include\drm-uapi\drm_fourcc.h")
if(([regex]::Matches($fourcc,'#include "drm.h"')).Count -ne 1){throw 'fourcc source anchor changed'}
$fourcc=$fourcc.Replace('#include "drm.h"',"#include <stdint.h>`ntypedef uint64_t __u64;")
[IO.File]::WriteAllText((Join-Path $overlay 'drm_fourcc.h'),$fourcc)
$inc=@("/I$output\include","/I$mesa\include","/I$mesa\src","/I$mesa\src\compiler","/I$mesa\src\compiler\nir","/I$mesa\src\asahi\compiler","/I$mesa\src\asahi\libagx","/I$generated\src","/I$generated\src\compiler","/I$generated\src\compiler\nir","/I$generated\src\asahi\compiler","/I$generated\src\asahi\libagx")
Push-Location $output
try {
 & "$tool\bin\HostX64\x64\cl.exe" /nologo /c /std:c11 /W3 /DNDEBUG /DHAVE_STRUCT_TIMESPEC /DMESA_DEBUG=0 /DBUILDING_MESA @inc "$mesa\src\asahi\compiler\agx_compile.c" *> 'compile.log'
 $result=$LASTEXITCODE
 Get-Content 'compile.log' -Tail 60
 @{ExitCode=$result;Source='src/asahi/compiler/agx_compile.c';Compiler='MSVC14.44.35207';Target='x64';Scope='compile-only'} | ConvertTo-Json | Set-Content 'result.json'
 exit $result
} finally {Pop-Location}
