param([ValidateSet('x64','arm64')][string]$Architecture='x64')
$builderRoot = if ($env:APPLE_AGX_BUILDER_ROOT) { $env:APPLE_AGX_BUILDER_ROOT } else { 'C:\Users\pauls\APB12E~1\active' }  # builder root: APPLE AGX\active (8.3 alias, no space)
# C44: dxil-spirv-c-static for the UMD (same configuration as EXP1388: /MD, no CLI, no native LLVM)
$ErrorActionPreference='Stop'
$src="$builderRoot\c49-dxspv-source"
$cmake="$builderRoot\EXP1388-dxil-runtime-build-only\cmake-tool\cmake\data\bin\cmake.exe"
$tool='C:\VS2022Community\VC\Tools\MSVC\14.44.35207'; $kit='C:\Program Files (x86)\Windows Kits\10'; $v='10.0.26100.0'
$bin=Join-Path $tool "bin\Hostx64\$Architecture"
$env:PATH="$bin;$tool\bin\Hostx64\x64;$kit\bin\$v\x64;"+$env:PATH
$env:INCLUDE="$tool\include;$kit\Include\$v\ucrt;$kit\Include\$v\shared;$kit\Include\$v\um"
$env:LIB="$tool\lib\$Architecture;$kit\Lib\$v\ucrt\$Architecture;$kit\Lib\$v\um\$Architecture"
$env:VSLANG='1033'
$b="$builderRoot\c49-dxspv-$Architecture"
if(Test-Path $b){throw 'exists'}
New-Item -ItemType Directory $b | Out-Null
$proc = if($Architecture -eq 'x64'){'AMD64'}else{'ARM64'}
& $cmake -S $src -B "$b\build" -G 'NMake Makefiles' '-DCMAKE_BUILD_TYPE=Release' '-DCMAKE_SYSTEM_NAME=Windows' "-DCMAKE_SYSTEM_PROCESSOR=$proc" `
 "-DCMAKE_C_COMPILER=$bin\cl.exe" "-DCMAKE_CXX_COMPILER=$bin\cl.exe" '-DCMAKE_POLICY_DEFAULT_CMP0091=NEW' '-DCMAKE_MSVC_RUNTIME_LIBRARY=MultiThreadedDLL' `
 '-DDXIL_SPIRV_CLI=OFF' '-DDXIL_SPIRV_NATIVE_LLVM=OFF' '-DDXIL_SPV_MISC_CLI=OFF' '-DSPIRV_HEADERS_ENABLE_TESTS=OFF' '-DSPIRV_HEADERS_ENABLE_INSTALL=OFF' *> "$b\configure.log"
"CONFIGURE $LASTEXITCODE"
& $cmake --build "$b\build" --target dxil-spirv-c-static *> "$b\build.log"
"BUILD $LASTEXITCODE"
Get-Content "$b\build.log" -Tail 3
Get-ChildItem "$b\build" -Recurse -Filter *.lib | % { "$($_.FullName.Substring($b.Length+1)) $($_.Length) $((Get-FileHash $_.FullName).Hash)" }
