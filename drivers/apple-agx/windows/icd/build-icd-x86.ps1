# CS 1.6 ICD plan, phase 4: link the x86 AGX OpenGL ICD (libgallium_wgl.dll)
# from the x86 Mesa GL stack (configure-x86-nir.ps1 / build-x86-nir.ps1), the
# x86 Asahi runtime closure (build-asahi-runtime-closure.py --architecture
# x86), the umd_* device/screen code, the GPUVA D3DKMT bridge and the AGX WGL
# target. Compiler flags and include directories are the ones that built the
# closure (its result.json), so one ABI and one copy of NIR/util result.
param(
  [Parameter(Mandatory = $true)][string]$Repository,
  [Parameter(Mandatory = $true)][string]$Closure,
  [Parameter(Mandatory = $true)][string]$Out,
  # EXP1182: the x86 Mesa build the closure was built from (b_ndebug builds).
  [string]$MesaBuild = 'C:\Users\pauls\AD04-fullcompiler-001\nir-x86'
)
$ErrorActionPreference = 'Stop'
$mesa = 'C:\Users\pauls\AD04-d3d10-frontend-build\mesa'
$build = $MesaBuild
$tool = 'C:\VS2022Community\VC\Tools\MSVC\14.44.35207'
$kit = 'C:\Program Files (x86)\Windows Kits\10'
$clang = 'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin'
$env:PATH = "$clang;$tool\bin\HostX64\x86;$tool\bin\HostX64\x64;" + $env:PATH
$env:INCLUDE = "$tool\include;$kit\Include\10.0.26100.0\ucrt;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um"
$env:LIB = "$tool\lib\x86;$kit\Lib\10.0.26100.0\ucrt\x86;$kit\Lib\10.0.26100.0\um\x86"
New-Item -ItemType Directory -Force $Out | Out-Null
$result = Get-Content (Join-Path $Closure 'result.json') -Raw | ConvertFrom-Json
if ($result.exit -ne 0 -or $result.architecture -ne 'x86') { throw 'closure is not a passing x86 build' }
$cflags = @($result.compiler.c_flags) + @('/DAPPLE_AGX_UMD_NATIVE_FRONTEND=1', '/DADMISSION_UMD_PIPE_FACTORY_TEST=1')
$cppflags = @($result.compiler.cpp_flags) + @('/DAPPLE_AGX_UMD_NATIVE_FRONTEND=1', '/DADMISSION_UMD_PIPE_FACTORY_TEST=1')
$agx = Join-Path $Repository 'drivers\apple-agx'
$includes = @($result.compiler.includes) + @(
  "$mesa\src\mesa", "$build\src\mesa", "$mesa\src\gallium\frontends\wgl",
  "$agx\render-admission\umd\src", "$agx\render-admission\umd\include",
  "$agx\render-admission\include", "$agx\shared\include", "$agx\mesa\winsys",
  "$agx\windows\icd") | ForEach-Object { "/I$_" }
$cpp = @('render-admission\umd\src\umd_runtime_device.c', 'render-admission\umd\src\umd_win32_screen.c',
  'render-admission\umd\src\umd_draw_composer.c', 'render-admission\umd\src\umd_asahi_batch_adapter.c',
  'render-admission\umd\src\umd_asahi_owner.c', 'render-admission\umd\src\umd_gpuva_windows.c',
  'windows\icd\agx_kmt_gpuva_bridge.c', 'windows\icd\agx_wgl_icd.cpp')
$c = @('render-admission\umd\src\umd_resource_lifetime.c', 'render-admission\umd\src\direct_flip_contract.c',
  'render-admission\src\render_win32_transport.c', 'render-admission\src\render_allocation.c',
  'shared\src\apple_agx_win32_abi.c', 'mesa\winsys\agx_win32_transport.c',
  'mesa\winsys\agx_win32_screen.c', 'mesa\winsys\agx_win32_native_bo.c',
  'mesa\winsys\agx_win32_construction_address.c', 'mesa\winsys\agx_win32_native_device.c',
  'mesa\winsys\agx_win32_reloc_capture.c', 'windows\icd\agx_wgl_glthread.c',
  'windows\icd\agx_wgl_glthread_mesa.c', 'windows\icd\agx_wgl_draw_merge.c',
  'windows\icd\agx_wgl_draw_hook.c')
$objects = @()
# Windows PowerShell turns native stderr (clang warnings) into terminating
# errors under 'Stop'; native failures are judged by exit codes below.
$ErrorActionPreference = 'Continue'
foreach ($unit in @($cpp | ForEach-Object { @{ Path = $_; Cpp = $true } }) + @($c | ForEach-Object { @{ Path = $_; Cpp = $false } })) {
  $src = Join-Path $agx $unit.Path
  $obj = Join-Path $Out (([IO.Path]::GetFileNameWithoutExtension($src)) + '.obj')
  $flags = if ($unit.Cpp) { $cppflags + '/TP' } else { $cflags + '/TC' }
  & "$clang\clang-cl.exe" @flags @includes /c $src "/Fo$obj" *> "$obj.log"
  if ($LASTEXITCODE) { Get-Content "$obj.log" | Select-String 'error' | Select -First 12; throw "compile failed: $($unit.Path)" }
  $objects += $obj
}
'COMPILE_PASS ' + $objects.Count
$libs = @('src\gallium\auxiliary\libgallium.a', 'src\compiler\nir\libnir.a', 'src\compiler\libcompiler.a',
  'src\util\libmesa_util.a', 'src\util\libmesa_util_clflush.a', 'src\util\libmesa_util_clflushopt.a',
  'src\util\libmesa_util_simd.a', 'src\util\blake3\libblake3.a', 'src\c11\impl\libmesa_util_c11.a',
  'src\compiler\glsl\libglsl.a', 'src\compiler\glsl\glcpp\libglcpp.a', 'src\mesa\libmesa.a',
  'src\mesa\libmesa_sse41.a', 'src\compiler\spirv\libvtn.a', 'src\mesa\glapi\glapi\libglapi_bridge.a',
  'src\mesa\glapi\shared-glapi\libglapi.a', 'src\gallium\auxiliary\libgalliumvl_stub.a',
  'src\gallium\frontends\wgl\libwgl.a', 'src\util\libxmlconfig.a') | ForEach-Object { Join-Path $build $_ }
$dll = Join-Path $Out 'libgallium_wgl.dll'
$link = @('/nologo', '/DLL', '/machine:x86', '/DEBUG', "/PDB:$Out\libgallium_wgl.pdb",
  "/DEF:$agx\windows\icd\agx_wgl_icd.def", "/IMPLIB:$Out\libgallium_wgl.lib",
  "/WHOLEARCHIVE:$build\src\gallium\frontends\wgl\libwgl.a", "/OUT:$dll") + $objects +
  @((Join-Path $Closure 'native_runtime.lib')) + $libs +
  @('ws2_32.lib', 'synchronization.lib', 'kernel32.lib', 'user32.lib', 'gdi32.lib', 'advapi32.lib',
    'shell32.lib', 'ole32.lib', 'oleaut32.lib', 'uuid.lib')
& "$clang\lld-link.exe" @link *> "$Out\link.log"
$code = $LASTEXITCODE
Get-Content "$Out\link.log" | Select-String 'undefined symbol|duplicate symbol|error' | Select -First 40 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(260, $_.Line.Length)) }
if ($code) { throw "link failed $code" }
'LINK_PASS ' + (Get-FileHash $dll).Hash
