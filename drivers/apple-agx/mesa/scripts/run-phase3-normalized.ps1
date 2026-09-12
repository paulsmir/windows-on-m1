param(
   [Parameter(Mandatory = $true)][string]$InputRoot,
   [Parameter(Mandatory = $true)][string]$ResultRoot
)
$ErrorActionPreference = 'Stop'
if (Test-Path $ResultRoot) { throw "refusing to reuse existing result root: $ResultRoot" }

$clang = 'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin\clang-cl.exe'
$mesa = 'C:\Users\pauls\AD04-d3d10-frontend-build\mesa'
$generated = 'C:\Users\pauls\AD04-asahi-windows-compiler\b5\generated'
$fourcc = 'C:\Users\pauls\AD04-uatomic\include'
$work = Join-Path $env:TEMP ('ad04-phase3-normalized-' + [guid]::NewGuid().ToString('N'))
$compilerWork = Join-Path $work 'compiler'
$include = @('/I', $fourcc, '/I', $InputRoot, '/I', $compilerWork, '/I', "$mesa\include", '/I', "$mesa\src", '/I', "$mesa\src\compiler", '/I', "$mesa\src\compiler\nir", '/I', "$mesa\src\asahi\compiler", '/I', "$mesa\src\asahi\libagx", '/I', "$generated\src", '/I', "$generated\src\compiler", '/I', "$generated\src\compiler\nir", '/I', "$generated\src\asahi\compiler", '/I', "$generated\src\asahi\libagx")
New-Item -ItemType Directory -Force $ResultRoot, $work | Out-Null
Copy-Item "$mesa\src\asahi\compiler" $compilerWork -Recurse -Force
Copy-Item (Join-Path $InputRoot 'compiler\agx_compiler.h') (Join-Path $compilerWork 'agx_compiler.h') -Force
Copy-Item (Join-Path $InputRoot 'compiler\agx_pack.c') (Join-Path $compilerWork 'agx_pack.c') -Force

function Invoke-Captured([string]$Name, [string]$File, [string[]]$Arguments, [bool]$Run) {
   $stdout = Join-Path $ResultRoot "$Name.stdout.log"
   $stderr = Join-Path $ResultRoot "$Name.stderr.log"
   $process = Start-Process -FilePath $File -ArgumentList $Arguments -Wait -PassThru -NoNewWindow -RedirectStandardOutput $stdout -RedirectStandardError $stderr
   $entry = [ordered]@{ name=$Name; command=$File; arguments=$Arguments; exit_code=$process.ExitCode; stdout=$stdout; stderr=$stderr }
   if ($Run -and $process.ExitCode -eq 0) {
      $exe = $Arguments | Where-Object { $_ -like '/Fe:*' } | Select-Object -First 1
      $path = $exe.Substring(4)
      $runOut = Join-Path $ResultRoot "$Name.run.stdout.log"; $runErr = Join-Path $ResultRoot "$Name.run.stderr.log"
      $runProcess = Start-Process -FilePath $path -Wait -PassThru -NoNewWindow -RedirectStandardOutput $runOut -RedirectStandardError $runErr
      $entry.run_exit_code = $runProcess.ExitCode; $entry.run_stdout=$runOut; $entry.run_stderr=$runErr
   }
   return $entry
}

$entries = @()
$overlay = Join-Path $InputRoot 'uatomic_clang_compat.h'
$entries += Invoke-Captured 'uatomic-x64' $clang @('/nologo','/W3','/std:c11','/FI',$overlay,(Join-Path $InputRoot 'uatomic_semantics_test.c'),"/Fe:$(Join-Path $work 'uatomic.exe')") $true
$entries += Invoke-Captured 'lut-derived-x64' $clang @('/nologo','/W3','/std:c11','/I',$InputRoot,(Join-Path $InputRoot 'lut_semantics_test.c'),"/Fe:$(Join-Path $work 'lut.exe')") $true
$entries += Invoke-Captured 'agxindex-actual-x64' $clang @('/nologo','/W3','/std:c11','/I',(Join-Path $InputRoot 'actual'),(Join-Path $InputRoot 'agxindex_fragment_corpus.c'),"/Fe:$(Join-Path $work 'agx-actual.exe')") $true
$compileArgs = @('/nologo','/c','/std:c11','/W3','/DNDEBUG','/DHAVE_STRUCT_TIMESPEC','/DMESA_DEBUG=0','/DBUILDING_MESA','/FI',$overlay) + $include + @((Join-Path $compilerWork 'agx_compile.c'))
$entries += Invoke-Captured 'agx-compile-x64' $clang $compileArgs $false
$entries += Invoke-Captured 'agx-compile-arm64' $clang ($compileArgs + '/clang:--target=aarch64-pc-windows-msvc') $false
$manifest = [ordered]@{ compiler=$clang; mesa=$mesa; generated=$generated; input_root=$InputRoot; result_root=$ResultRoot; work_root=$work; entries=$entries }
$manifest | ConvertTo-Json -Depth 6 | Set-Content (Join-Path $ResultRoot 'manifest.json')
$required = @('uatomic-x64', 'lut-derived-x64', 'agxindex-actual-x64', 'agx-compile-x64', 'agx-compile-arm64')
$actual = @($entries | ForEach-Object { $_.name })
if (Compare-Object $required $actual) { exit 1 }
$bad = $entries | Where-Object { $_.exit_code -ne 0 -or ($_.Contains('run_exit_code') -and $_.run_exit_code -ne 0) }
if ($bad.Count -ne 0) { exit 1 }
