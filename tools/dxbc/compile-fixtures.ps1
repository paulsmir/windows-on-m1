# Compile tests/fixtures/dxbc/*.hlsl with the Windows SDK's fxc into the
# objects (.dxbc) and listings (.asm) that tests/test_dxbc_parse.py compares.
# Run on a Windows host with the SDK installed:
#   powershell -File compile-fixtures.ps1 -Directory <copy of tests/fixtures/dxbc>
param([string]$Directory = (Join-Path $PSScriptRoot '..\..\tests\fixtures\dxbc'),
      [string]$Fxc = 'C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe')
$targets = @{ vs40='vs_4_0'; ps40='ps_4_0'; gs40='gs_4_0'; ps41='ps_4_1'; cs40='cs_4_0'; cs41='cs_4_1'; cs50='cs_5_0'
              hs50='hs_5_0'; ds50='ds_5_0'; ps50='ps_5_0'; ps50b='ps_5_0'; gs50='gs_5_0'; ps51='ps_5_1' }
foreach ($k in $targets.Keys) {
  & $Fxc /nologo /T $targets[$k] /E main /O3 /Fo "$Directory\$k.dxbc" /Fc "$Directory\$k.asm" "$Directory\$k.hlsl"
  "$k rc=$LASTEXITCODE"
}
