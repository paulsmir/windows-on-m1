# Decode guest ETL captures to xperf "dumper" CSV with symbols, on the builder
# (it has the Windows Performance Toolkit and the driver PDBs).
#   xperf-dump.ps1 -Etl C:\path\cpu-typing.etl[,...] -SymbolDirs <dir with the KMD/UMD PDBs>[,...]
# Output: <etl>.csv next to each ETL. Feed it to analysis/dwm_frames.py etc.
param([Parameter(Mandatory = $true)][string[]]$Etl, [string[]]$SymbolDirs = @())
$env:_NT_SYMBOL_PATH = (@($SymbolDirs) + 'srv*C:\sym*https://msdl.microsoft.com/download/symbols') -join ';'
$x = 'C:\Program Files (x86)\Windows Kits\10\Windows Performance Toolkit\xperf.exe'
foreach ($e in $Etl) {
  $csv = [IO.Path]::ChangeExtension($e, '.csv')
  & $x -i $e -symbols -o $csv -a dumper 2> ($csv + '.err')
  '{0} {1}' -f $csv, (Get-Item $csv).Length
}
