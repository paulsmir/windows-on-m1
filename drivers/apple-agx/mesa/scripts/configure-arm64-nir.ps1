$ErrorActionPreference='Stop'
$root='C:\Users\pauls\AD04-fullcompiler-001'
$tool='C:\VS2022Community\VC\Tools\MSVC\14.44.35207'
$kit='C:\Program Files (x86)\Windows Kits\10'
$clang='C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin'
$env:PATH="$root\winflex;$root\venv\Scripts;$clang;$tool\bin\HostX64\x64;"+$env:PATH
$env:INCLUDE="$tool\include;$kit\Include\10.0.26100.0\ucrt;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um"
$env:LIB="$tool\lib\arm64;$kit\Lib\10.0.26100.0\ucrt\arm64;$kit\Lib\10.0.26100.0\um\arm64"
if(Test-Path "$root\nir-arm64"){throw 'Preserve existing ARM64 configure'}
$out=Join-Path $root ('arm64-config-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $out | Out-Null
$args=@('setup',"$root\nir-arm64",'C:\Users\pauls\AD04-d3d10-frontend-build\mesa',
 '--cross-file',"$root\compiler-arm64.ini",'--buildtype=debugoptimized','--wrap-mode=nofallback',
 '-Dplatforms=[]','-Dgallium-drivers=softpipe','-Dvulkan-drivers=[]','-Dopengl=true',
 '-Dgles1=disabled','-Dgles2=disabled','-Dglx=disabled','-Degl=disabled',
 '-Dgbm=disabled','-Dllvm=disabled','-Dshared-glapi=disabled','-Dxmlconfig=disabled',
 '-Dexpat=disabled','-Dzlib=disabled','-Dzstd=disabled','-Dbuild-tests=false',
 "-Dc_args=/FI$root\uatomic_clang_compat.h","-Dcpp_args=/FI$root\uatomic_clang_compat.h")
$p=Start-Process "$root\venv\Scripts\meson.exe" -ArgumentList $args -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$out\stdout.log" -RedirectStandardError "$out\stderr.log"
@{exit=$p.ExitCode;arguments=$args;scope='ARM64 compile/link only; executable wrapper absent'}|ConvertTo-Json|Set-Content "$out\result.json"
Write-Output "CONFIGURE_EXIT=$($p.ExitCode) EVIDENCE=$out"
Get-Content "$out\stdout.log" -Tail 10
Get-Content "$out\stderr.log" -Tail 4
exit $p.ExitCode
