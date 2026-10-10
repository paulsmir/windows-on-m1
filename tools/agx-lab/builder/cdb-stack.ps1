# Raw stack of a user-mode crash/abort/hang dump (e.g. agx_gl_smoke_abort.dmp),
# filtered to frames of the given modules. On the builder:
#   cdb-stack.ps1 -Dump C:\path\x.dmp -SymbolDirs <dirs with PDBs> [-Arch x86|arm64|x64] [-Modules a!,b!]
# A modal assert MessageBox looks like a hang on the guest; the dump shows it.
param([Parameter(Mandatory = $true)][string]$Dump, [string[]]$SymbolDirs = @(), [string]$Arch = 'x86',
      [string[]]$Modules = @('libgallium_wgl!', 'opengl32!', 'agx_gl_smoke!'), [string]$Count = '0x1000')
$cdb = "C:\Program Files (x86)\Windows Kits\10\Debuggers\$Arch\cdb.exe"
$sym = (@($SymbolDirs) + 'srv*C:\sym*https://msdl.microsoft.com/download/symbols') -join ';'
$sp = if ($Arch -eq 'x86') { '@esp' } else { '@sp' }
$pattern = ($Modules | ForEach-Object { [regex]::Escape($_) }) -join '|'
& $cdb -z $Dump -y $sym -c "~0s; dps $sp L$Count; q" 2>&1 | Select-String $pattern | Select-Object -First 50 |
  ForEach-Object { $_.Line.Substring(0, [Math]::Min(200, $_.Line.Length)) }
