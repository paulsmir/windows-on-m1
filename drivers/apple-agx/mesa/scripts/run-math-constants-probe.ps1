param(
   [Parameter(Mandatory = $true)][string]$Root
)
$ErrorActionPreference = 'Stop'
if (Test-Path $Root) { throw "refusing to reuse existing result root: $Root" }
$clang = 'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin\clang-cl.exe'
$source = Join-Path (Split-Path -Parent $PSCommandPath) 'math_constants_probe.c'
New-Item -ItemType Directory -Force $Root | Out-Null
foreach ($target in @('x64', 'arm64')) {
   $args = @('/nologo', '/W3', '/std:c11', '/D_USE_MATH_DEFINES', $source)
   if ($target -eq 'x64') {
      $exe = Join-Path $Root 'math-constants-x64.exe'
      $args += "/Fe:$exe"
   } else {
      $args += @('/c', '/clang:--target=aarch64-pc-windows-msvc', "/Fo:$(Join-Path $Root 'math-constants-arm64.obj')")
   }
   & $clang @args 1> (Join-Path $Root "$target.stdout.log") 2> (Join-Path $Root "$target.stderr.log")
   $compile = $LASTEXITCODE
   $run = $null
   if ($target -eq 'x64' -and $compile -eq 0) {
      & $exe 1> (Join-Path $Root 'x64.run.stdout.log') 2> (Join-Path $Root 'x64.run.stderr.log')
      $run = $LASTEXITCODE
   }
   [ordered]@{target=$target; compile_exit=$compile; run_exit=$run; arguments=$args} |
      ConvertTo-Json -Compress | Add-Content (Join-Path $Root 'manifest.jsonl')
   if ($compile -ne 0 -or ($null -ne $run -and $run -ne 0)) { exit 1 }
}
