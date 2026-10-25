# D3D9On12 (EXP1874B): link the WOW64 (x86) build of the D3D user-mode
# driver WITH the D3D12 native owner (AppleAgxRenderAdmissionUmd32.dll,
# registered as UserModeDriverNameWow). A 32-bit D3D9 application on J313
# runs through D3D9On12 on this DLL's OpenAdapter12; build-umd-x86.ps1 links
# the owner-less D3D12 entry. Same sources and definitions as the x64 UMD
# (build-umd-x64.ps1), compiled with the x86 Asahi
# runtime closure's compiler flags (its result.json) and linked against the x86
# Mesa archives of the same projected source. Prints COMPILE_PASS, LINK_PASS <sha256>.
param(
  [Parameter(Mandatory = $true)][string]$Repository,
  [Parameter(Mandatory = $true)][string]$Closure,
  [Parameter(Mandatory = $true)][string]$Out,
  [string]$MesaBuild = '',
  [int]$PackageBuild = 0,
  [string]$MesaSource = ''
)
$builderRoot = if ($env:APPLE_AGX_BUILDER_ROOT) { $env:APPLE_AGX_BUILDER_ROOT } else { 'C:\Users\pauls\APB12E~1\active' }  # builder root: APPLE AGX\active (8.3 alias, no space)
if (!$MesaBuild) { $MesaBuild = "$builderRoot\X86B1-mesa-configure\build" }
if (!$MesaSource) { $MesaSource = "$builderRoot\EXP1811-x64-mesa-projected-source\mesa" }
$ErrorActionPreference = 'Stop'
# x86 closure of the EXP1811 projected source (X86B1 Meson build).
$mesa = $MesaSource
$build = $MesaBuild
$tool = 'C:\VS2022Community\VC\Tools\MSVC\14.44.35207'
$kit = 'C:\Program Files (x86)\Windows Kits\10'
$clang = "$builderRoot\AD04-asahi-windows-compiler\llvm20\bin"
$env:PATH = "$clang;$tool\bin\HostX64\x86;$tool\bin\HostX64\x64;" + $env:PATH
$env:INCLUDE = "$tool\include;$kit\Include\10.0.26100.0\ucrt;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um"
$env:LIB = "$tool\lib\x86;$kit\Lib\10.0.26100.0\ucrt\x86;$kit\Lib\10.0.26100.0\um\x86"
New-Item -ItemType Directory -Force $Out | Out-Null
$result = Get-Content (Join-Path $Closure 'result.json') -Raw | ConvertFrom-Json
if ($result.exit -ne 0 -or $result.architecture -ne 'x86') { throw 'closure is not a passing x86 build' }
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
$dll = Join-Path $Out 'AppleAgxRenderAdmissionUmd32.dll'
# /OPT:REF as the ARM64 native-frontend link (OptimizeReferences): /DEBUG alone keeps
# unreferenced dxbc-spirv code whose frames (30748 B) fail STACK_FRAMES (EXP1875 attempt 3)
$link = @('/nologo', '/DLL', '/machine:x86', '/DEBUG', '/OPT:REF', "/PDB:$Out\AppleAgxRenderAdmissionUmd32.pdb",
  "/DEF:$agx\render-admission\umd\AppleAgxRenderAdmissionUmd.def", "/IMPLIB:$Out\AppleAgxRenderAdmissionUmd32.lib",
  "/OUT:$dll", $res) + $objects + @((Join-Path $Closure 'native_runtime.lib')) + $libs +
  @('ws2_32.lib', 'synchronization.lib', 'kernel32.lib', 'user32.lib', 'gdi32.lib', 'advapi32.lib',
    'shell32.lib', 'ole32.lib', 'oleaut32.lib', 'uuid.lib')
& "$clang\lld-link.exe" @link *> "$Out\link.log"
$code = $LASTEXITCODE
Get-Content "$Out\link.log" | Select-String 'undefined symbol|duplicate symbol|error' | Select -First 40 | ForEach-Object { $_.Line.Substring(0, [Math]::Min(260, $_.Line.Length)) }
if ($code) { throw "link failed $code" }
'LINK_PASS ' + (Get-FileHash $dll).Hash
