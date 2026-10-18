function Test-OriginalArmedBoot($Visible,$Problem,$Arm,$Version) {
 return ($Visible -eq 1 -and $Problem -eq 0 -and !$Arm -and $Version -eq '30.0.865.0')
}
function Assert-OriginalEtlReceipt($Receipt,$BootUtc,$EvidenceRoot) {
 if(!$Receipt.OriginalArmedBoot -or $Receipt.BootUtc -ne $BootUtc -or @($Receipt.Etls).Count -eq 0){throw 'original boot ETL receipt required before restart'}
 foreach($e in $Receipt.Etls){
  if($e.Name -notlike 'original-*.etl*'){throw 'original ETL name required'}
  $p=Join-Path $EvidenceRoot $e.Name
  if(!(Test-Path $p) -or (Get-FileHash $p).Hash -ne $e.SHA256){throw 'original ETL hash mismatch'}
 }
}
