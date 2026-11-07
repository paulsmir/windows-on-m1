$builderRoot = if ($env:APPLE_AGX_BUILDER_ROOT) { $env:APPLE_AGX_BUILDER_ROOT } else { 'C:\Users\pauls\APB12E~1\active' }  # builder root: APPLE AGX\active (8.3 alias, no space)
$ErrorActionPreference = 'Stop'
$compiler = "$builderRoot\AD04-d3d10-frontend-build\mesa\src\asahi\compiler"
$pattern = '\.(kill|cache|discard|abs|neg|memory|has_reg)\s*=\s*[^;]+;'
Get-ChildItem $compiler -Recurse -Include *.c,*.h |
   Select-String -Pattern $pattern |
   ForEach-Object { '{0}:{1}:{2}' -f $_.Path, $_.LineNumber, $_.Line.Trim() }
