param([ValidateSet('x64','ARM64')][string]$Architecture='x64')
$ErrorActionPreference='Stop'
$root='C:\Users\pauls\AD04-fullcompiler-001'
$mesa='C:\Users\pauls\AD04-d3d10-frontend-build\mesa'
$tool='C:\VS2022Community\VC\Tools\MSVC\14.44.35207'
$kit='C:\Program Files (x86)\Windows Kits\10'
$clang='C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin'
$build=Join-Path $root "nir-$Architecture"
if(Test-Path $build){throw 'Preserve previous configure evidence'}
$env:PATH="$root\venv\Scripts;$clang;$tool\bin\HostX64\x64;"+$env:PATH
$env:CC='clang-cl'
$env:CXX='clang-cl'
$env:INCLUDE="$tool\include;$kit\Include\10.0.26100.0\ucrt;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um"
$env:LIB="$tool\lib\x64;$kit\Lib\10.0.26100.0\ucrt\x64;$kit\Lib\10.0.26100.0\um\x64"
if($Architecture -ne 'x64'){throw 'ARM64 configuration requires explicit Meson cross-file; x64 control first'}
$args=@('setup',$build,$mesa,'--buildtype=debugoptimized','--wrap-mode=nofallback',
 '-Dplatforms=[]','-Dgallium-drivers=[]','-Dvulkan-drivers=[]','-Dopengl=false',
 '-Dgles1=disabled','-Dgles2=disabled','-Dglx=disabled','-Degl=disabled',
 '-Dgbm=disabled','-Dllvm=disabled','-Dshared-glapi=disabled','-Dxmlconfig=disabled',
 '-Dexpat=disabled','-Dzlib=disabled','-Dzstd=disabled','-Dbuild-tests=false')
$p=Start-Process -FilePath "$root\venv\Scripts\meson.exe" -ArgumentList $args -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$root\configure-$Architecture.stdout.log" -RedirectStandardError "$root\configure-$Architecture.stderr.log"
Write-Output "CONFIGURE_EXIT=$($p.ExitCode)"
Get-Content "$root\configure-$Architecture.stdout.log" -Tail 12
Get-Content "$root\configure-$Architecture.stderr.log" -Tail 4
exit $p.ExitCode
