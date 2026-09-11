param([string]$Root = 'C:\Users\pauls\AD04-agxindex-corpus')
$ErrorActionPreference = 'Continue'
$clang = 'C:\Users\pauls\AD04-asahi-windows-compiler\llvm20\bin\clang-cl.exe'
$source = Join-Path $Root 'agxindex_unsigned_corpus.c'
$exe = Join-Path $Root 'candidate.exe'
$raw = Join-Path $Root 'candidate.raw.txt'
$err = Join-Path $Root 'candidate.stderr.txt'
& $clang /nologo /W3 /std:c11 /DPROBE_CANDIDATE=1 $source "/Fe:$exe" *> $err
$compile = $LASTEXITCODE
$run = -1
if ($compile -eq 0) { & $exe > $raw; $run = $LASTEXITCODE }
Write-Output "COMPILE=$compile RUN=$run"
if ($compile -ne 0 -or $run -ne 0) { exit 1 }
