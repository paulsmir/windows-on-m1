param([string]$Root = '')
$builderRoot = if ($env:APPLE_AGX_BUILDER_ROOT) { $env:APPLE_AGX_BUILDER_ROOT } else { 'C:\Users\pauls\APB12E~1\active' }  # builder root: APPLE AGX\active (8.3 alias, no space)
if (!$Root) { $Root = "$builderRoot\AD04-agxindex" }
$clang = "$builderRoot\AD04-asahi-windows-compiler\llvm20\bin\clang-cl.exe"
$source = Join-Path $Root 'agxindex_layout_probe.c'
foreach ($expected in @(8, 12, 16, 20, 24)) {
   $obj = Join-Path $Root "results\arm64-default-$expected.obj"
   & $clang '--target=aarch64-pc-windows-msvc' /nologo /W3 /std:c11 "/DPROBE_GCC_STRUCT=0" "/DPROBE_EXPECT_SIZE=$expected" $source /c "/Fo:$obj" *> (Join-Path $Root "results\arm64-default-$expected.log")
   Write-Output "EXPECTED=$expected COMPILE=$LASTEXITCODE"
}
