# C53B: builds agx_d3d12_tiled_probe.c (copied into $r) for x64 (run "warp" on the
# builder as the reference) and ARM64 (J313) with MSVC /W4 /WX.
param([string]$r = '')
$builderRoot = if ($env:APPLE_AGX_BUILDER_ROOT) { $env:APPLE_AGX_BUILDER_ROOT } else { 'C:\Users\pauls\APB12E~1\active' }  # builder root: APPLE AGX\active (8.3 alias, no space)
if (!$r) { $r = "$builderRoot\t2d-probe" }
$tool='C:\VS2022Community\VC\Tools\MSVC\14.44.35207'; $kit='C:\Program Files (x86)\Windows Kits\10'; $v='10.0.26100.0'
$env:INCLUDE="$tool\include;$kit\Include\$v\ucrt;$kit\Include\$v\shared;$kit\Include\$v\um"
Set-Location $r
foreach ($a in @(@('x64','Hostx64\x64'),@('arm64','Hostx64\arm64'))) {
  $env:LIB="$tool\lib\$($a[0]);$kit\Lib\$v\ucrt\$($a[0]);$kit\Lib\$v\um\$($a[0])"
  & "$tool\bin\$($a[1])\cl.exe" /nologo /W4 /WX /wd4201 /O2 agx_d3d12_tiled_probe.c "/Feagx_d3d12_tiled_probe_$($a[0]).exe" /link d3d12.lib dxgi.lib dxguid.lib 2>&1 | Select-String -Pattern 'error|warning' | Select -First 15
  "build $($a[0]) rc=$LASTEXITCODE"
}
