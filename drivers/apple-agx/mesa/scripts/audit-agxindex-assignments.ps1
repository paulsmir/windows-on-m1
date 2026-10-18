$ErrorActionPreference = 'Stop'
$compiler = 'C:\Users\pauls\AD04-d3d10-frontend-build\mesa\src\asahi\compiler'
$pattern = '\.(kill|cache|discard|abs|neg|memory|has_reg)\s*=\s*[^;]+;'
Get-ChildItem $compiler -Recurse -Include *.c,*.h |
   Select-String -Pattern $pattern |
   ForEach-Object { '{0}:{1}:{2}' -f $_.Path, $_.LineNumber, $_.Line.Trim() }
