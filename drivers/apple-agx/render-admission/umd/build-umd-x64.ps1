# G2 HW3b / D3D: link a diagnostic-only x64 build of the D3D user-mode driver
# (AppleAgxRenderAdmissionUmdX64.dll, packaged as UmdX64; the copy each
# experiment directory carried until EXP1875 is this file).
# EXP1222: opengl32's PresentBuffers creates a D3D11 device in the calling
# process for a window's redirection surface, and 32-bit D3D9/D3D11 games
# need the same driver. Same sources and definitions as the ARM64 UMD
# (AppleAgxRenderAdmissionUmd.vcxproj with the native frontend and the GPUVA
# winsys), compiled with the x64 Asahi runtime closure's compiler flags (its
# result.json, as build-icd-x86.ps1 does), so one ABI and one copy of
# NIR/util result. Prints COMPILE_PASS, LINK_PASS <sha256>.
param(
  [Parameter(Mandatory = $true)][string]$Repository,
  [Parameter(Mandatory = $true)][string]$Closure,
  [Parameter(Mandatory = $true)][string]$Out,
  [string]$MesaBuild = 'C:\Users\pauls\AD04-fullcompiler-001\nir-x64',
  [int]$PackageBuild = 0,
  [string]$MesaSource = 'C:\Users\pauls\EXP1811-x64-mesa-projected-source\mesa'
)
$ErrorActionPreference = 'Stop'
# Fresh x64 closure (EXP1811 source, EXP1813 build, EXP1817 runtime).
$mesa = $MesaSource
$build = $MesaBuild
$tool = 'C:\VS2022Community\VC\Tools\MSVC\14.44.35207'
$kit = 'C:\Program Files (x86)\Windows Kits\10'
$clang = 'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin'
$env:PATH = "$clang;$tool\bin\HostX64\x64;" + $env:PATH
$env:INCLUDE = "$tool\include;$kit\Include\10.0.26100.0\ucrt;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um"
$env:LIB = "$tool\lib\x64;$kit\Lib\10.0.26100.0\ucrt\x64;$kit\Lib\10.0.26100.0\um\x64"
New-Item -ItemType Directory -Force $Out | Out-Null
$result = Get-Content (Join-Path $Closure 'result.json') -Raw | ConvertFrom-Json
if ($result.exit -ne 0 -or $result.architecture -ne 'x64') { throw 'closure is not a passing x64 build' }
# AppleAgxRenderAdmissionUmd.vcxproj: EnableNativeFrontend, EnableGpuvaWinsys.
$defines = @('/DAPPLE_AGX_UMD_NATIVE_FRONTEND=1', '/DADMISSION_UMD_PIPE_FACTORY_TEST=1',
  '/DAPPLE_AGX_GPUVA_WINSYS=1', '/DAPPLE_AGX_EXP907_FRAME_RECEIPT=1',
  '/DAPPLE_AGX_D3D12_NATIVE_OWNER=1', '/DAPPLE_AGX_UMD_ADMISSION_TRACE=1', '/DNOMINMAX=1')
# C44: a closure with the DXIL frontend (agx_dxil_to_nir) also brings the
# dxil-spirv libraries (its NativeRuntime.props defines APPLE_AGX_D3D12_DXIL)
$resultText = Get-Content (Join-Path $Closure 'result.json') -Raw
$dxil = $resultText -match 'agx_dxil_to_nir\.c'
if ($dxil) { $defines += '/DAPPLE_AGX_D3D12_DXIL=1' }
$cflags = @($result.compiler.c_flags) + $defines
$cppflags = @($result.compiler.cpp_flags) + $defines + @('/std:c++17')
$agx = Join-Path $Repository 'drivers\apple-agx'
$includes = @($result.compiler.includes) + @(
  "$mesa\include", "$mesa\src", "$mesa\src\gallium\include", "$mesa\src\gallium\auxiliary",
  "$build\src", "$agx\render-admission\umd\src", "$agx\render-admission\umd\include",
  "$agx\render-admission\include", "$agx\shared\include", "$agx\mesa\winsys",
  "$agx\mesa\dxil", "$agx\mesa\dxbc") | ForEach-Object { "/I$_" }
$cpp = @('render-admission\umd\src\umd_runtime_device.c', 'render-admission\umd\src\umd_win32_screen.c',
  'render-admission\umd\src\umd_draw_composer.c', 'render-admission\umd\src\umd_asahi_batch_adapter.c',
  'render-admission\umd\src\umd_asahi_owner.c', 'render-admission\umd\src\umd_gpuva_windows.c',
  'mesa\winsys\agx_d3d10_windows.cpp', 'mesa\winsys\agx_d3d12_windows.cpp', 'd3d12\agx_d3d12_ddi.cpp',
  'd3d12\agx_d3d12_zero_cache_library.cpp', 'd3d12\agx_d3d12_shader_owned_core.cpp', 'd3d12\agx_shader_input_provider.cpp')
$c = @('render-admission\umd\src\umd.c', 'render-admission\umd\src\umd_resource_lifetime.c',
  'render-admission\umd\src\direct_flip_contract.c', 'render-admission\src\render_win32_transport.c',
  'render-admission\src\render_allocation.c', 'shared\src\apple_agx_win32_abi.c',
  'mesa\winsys\agx_win32_transport.c', 'mesa\winsys\agx_win32_screen.c',
  'mesa\winsys\agx_win32_native_bo.c', 'mesa\winsys\agx_win32_construction_address.c',
  'mesa\winsys\agx_win32_native_device.c', 'mesa\winsys\agx_win32_reloc_capture.c',
  'mesa\winsys\agx_win32_gpuva.c', 'mesa\dxbc\agx_legacy_program_frame.c', 'mesa\dxil\agx_dxil_program.c')
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
# The package version resource (DriverVer coherence, as the ARM64 UMD).
$res = Join-Path $Out 'package-version.res'
& "$clang\llvm-rc.exe" /nologo /DAPPLE_AGX_PACKAGE_DLL=1 "/DAPPLE_AGX_VERSION_BUILD=$PackageBuild" `
  "/I$agx\render-admission" "/FO$res" "$agx\render-admission\package-version.rc" *> "$Out\rc.log"
if ($LASTEXITCODE) { Get-Content "$Out\rc.log" | Select -First 12; throw 'resource compile failed' }
'COMPILE_PASS ' + $objects.Count
$libs = @('src\gallium\auxiliary\libgallium.a', 'src\compiler\nir\libnir.a', 'src\compiler\libcompiler.a',
  'src\util\libmesa_util.a', 'src\util\libmesa_util_clflush.a', 'src\util\libmesa_util_clflushopt.a',
  'src\util\libmesa_util_simd.a', 'src\util\blake3\libblake3.a', 'src\c11\impl\libmesa_util_c11.a',
  'src\compiler\spirv\libvtn.a', 'src\gallium\auxiliary\libgalliumvl_stub.a',
  'src\util\libxmlconfig.a') | ForEach-Object { Join-Path $build $_ }
if ($dxil) { $libs += @($result.libraries | ForEach-Object { $_.path } | Where-Object { $_ -match 'c4[49]-dxspv-' }) } # c49: the patched dxil-spirv (EXP1858)
'DXIL ' + $dxil + ' libraries ' + $libs.Count
$dll = Join-Path $Out 'AppleAgxRenderAdmissionUmdX64.dll'
# /OPT:REF as the ARM64 native-frontend link (OptimizeReferences) and the WOW64
# link: /DEBUG alone keeps the unreferenced dxbc-spirv DXBC path (30 KB frames)
$link = @('/nologo', '/DLL', '/machine:x64', '/DEBUG', '/OPT:REF', "/PDB:$Out\AppleAgxRenderAdmissionUmdX64.pdb",
  "/DEF:$agx\render-admission\umd\AppleAgxRenderAdmissionUmd.def", "/IMPLIB:$Out\AppleAgxRenderAdmissionUmdX64.lib",
  "/OUT:$dll", $res) + $objects + @((Join-Path $Closure 'native_runtime.lib')) + $libs +
  @('ws2_32.lib', 'synchronization.lib', 'kernel32.lib', 'user32.lib', 'gdi32.lib', 'advapi32.lib',
    'shell32.lib', 'ole32.lib', 'oleaut32.lib', 'uuid.lib')
& "$clang\lld-link.exe" @link *> "$Out\link.log"
$code = $LASTEXITCODE
Get-Content "$Out\link.log" | Select-String 'undefined symbol|duplicate symbol|error' | Select -First 40 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(260, $_.Line.Length)) }
if ($code) { throw "link failed $code" }
'LINK_PASS ' + (Get-FileHash $dll).Hash
