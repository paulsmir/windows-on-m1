$ErrorActionPreference = 'Stop'

function Test-Phase3Policy($Entries) {
   $required = @('uatomic-x64', 'lut-derived-x64', 'agxindex-actual-x64', 'agx-compile-x64', 'agx-compile-arm64')
   $actual = @($Entries | ForEach-Object { $_.name })
   if (Compare-Object $required $actual) { return $false }
   $bad = $Entries | Where-Object { $_.exit_code -ne 0 -or ($_.Contains('run_exit_code') -and $_.run_exit_code -ne 0) }
   return $bad.Count -eq 0
}

function Entries {
   param([hashtable]$Override = @{}, [string[]]$Omit = @())
   $names = @('uatomic-x64', 'lut-derived-x64', 'agxindex-actual-x64', 'agx-compile-x64', 'agx-compile-arm64')
   return @($names | Where-Object { $_ -notin $Omit } | ForEach-Object {
      $entry = [ordered]@{name=$_; exit_code=0}
      if ($_ -in @('uatomic-x64', 'lut-derived-x64', 'agxindex-actual-x64')) { $entry.run_exit_code = 0 }
      if ($Override.ContainsKey($_)) { foreach ($key in $Override[$_].Keys) { $entry[$key] = $Override[$_][$key] } }
      Write-Output -NoEnumerate $entry
   })
}

if (!(Test-Phase3Policy (Entries))) { throw 'all-zero manifest rejected' }
if (Test-Phase3Policy (Entries @{ 'agx-compile-x64' = @{exit_code=1} })) { throw 'failed compiler accepted' }
if (Test-Phase3Policy (Entries @{ 'uatomic-x64' = @{run_exit_code=1} })) { throw 'failed executable accepted' }
if (Test-Phase3Policy (Entries @{} @('agx-compile-arm64'))) { throw 'missing required entry accepted' }
Write-Output 'POLICY=PASS'
