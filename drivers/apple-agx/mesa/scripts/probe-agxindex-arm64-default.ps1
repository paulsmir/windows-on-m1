param([string]$Root = 'C:\Users\pauls\AD04-agxindex')
$clang = 'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin\clang-cl.exe'
$source = Join-Path $Root 'agxindex_layout_probe.c'
foreach ($expected in @(8, 12, 16, 20, 24)) {
   $obj = Join-Path $Root "results\arm64-default-$expected.obj"
   & $clang '--target=aarch64-pc-windows-msvc' /nologo /W3 /std:c11 "/DPROBE_GCC_STRUCT=0" "/DPROBE_EXPECT_SIZE=$expected" $source /c "/Fo:$obj" *> (Join-Path $Root "results\arm64-default-$expected.log")
   Write-Output "EXPECTED=$expected COMPILE=$LASTEXITCODE"
}
