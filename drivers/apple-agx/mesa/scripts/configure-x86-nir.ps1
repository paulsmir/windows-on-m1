# CS 1.6 ICD plan, phase 4: the x86 (i686) counterpart of configure-arm64-nir.ps1
# for the 32-bit OpenGL ICD that hl.exe loads. Same pinned Mesa tree, clang-cl
# and uatomic compatibility header; platforms=windows adds the WGL frontend and
# the libgl-gdi opengl32 the ICD is linked from.
# EXP1182: -Name selects the build directory and -NDebug configures a
# release-assert build (b_ndebug=true, still /O2 with debug info): the
# default debugoptimized build keeps every Mesa/Asahi assert and runs NIR
# validation after each compiler pass.
param([string]$Name='nir-x86',[switch]$NDebug)
$ErrorActionPreference='Stop'
$root='C:\Users\pauls\AD04-fullcompiler-001'
$tool='C:\VS2022Community\VC\Tools\MSVC\14.44.35207'
$kit='C:\Program Files (x86)\Windows Kits\10'
$clang='C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin'
$here=Split-Path -Parent $MyInvocation.MyCommand.Path
$env:PATH="$root\winflex;$root\venv\Scripts;$clang;$tool\bin\HostX64\x86;$tool\bin\HostX64\x64;"+$env:PATH
$env:INCLUDE="$tool\include;$kit\Include\10.0.26100.0\ucrt;$kit\Include\10.0.26100.0\shared;$kit\Include\10.0.26100.0\um"
$env:LIB="$tool\lib\x86;$kit\Lib\10.0.26100.0\ucrt\x86;$kit\Lib\10.0.26100.0\um\x86"
if(Test-Path "$root\$Name"){throw 'Preserve existing x86 configure'}
$out=Join-Path $root ('x86-config-'+[guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory $out | Out-Null
$args=@('setup',"$root\$Name",'C:\Users\pauls\AD04-d3d10-frontend-build\mesa',
 '--cross-file',"$here\compiler-x86.ini",'--buildtype=debugoptimized','--wrap-mode=nofallback',
 '-Dplatforms=windows','-Dgallium-drivers=softpipe','-Dvulkan-drivers=[]','-Dopengl=true',
 '-Dgles1=disabled','-Dgles2=disabled','-Dglx=disabled','-Degl=disabled',
 '-Dgbm=disabled','-Dllvm=disabled','-Dshared-glapi=disabled','-Dxmlconfig=disabled',
 '-Dexpat=disabled','-Dzlib=disabled','-Dzstd=disabled','-Dbuild-tests=false',
 "-Dc_args=/FI$root\uatomic_clang_compat.h","-Dcpp_args=/FI$root\uatomic_clang_compat.h")
if($NDebug){ $args += '-Db_ndebug=true' }
$p=Start-Process "$root\venv\Scripts\meson.exe" -ArgumentList $args -Wait -PassThru -NoNewWindow -RedirectStandardOutput "$out\stdout.log" -RedirectStandardError "$out\stderr.log"
@{exit=$p.ExitCode;arguments=$args;scope='x86 configure; executables run under WOW64 on the builder'}|ConvertTo-Json|Set-Content "$out\result.json"
Write-Output "CONFIGURE_EXIT=$($p.ExitCode) EVIDENCE=$out"
Get-Content "$out\stdout.log" -Tail 12
Get-Content "$out\stderr.log" -Tail 6
exit $p.ExitCode
