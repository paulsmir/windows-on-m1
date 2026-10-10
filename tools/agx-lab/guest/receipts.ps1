# Copy the AGX KMD diagnostic receipts (REG_BINARY values under the APPL0002
# device parameters) to %USERPROFILE%\rcpt\<name>.bin and print their names.
# Decoders live in tools/agx-lab/analysis.
$p=Get-ItemProperty "HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0002\0\Device Parameters"
New-Item -ItemType Directory -Force $env:USERPROFILE\rcpt | Out-Null; Remove-Item $env:USERPROFILE\rcpt\* -EA 0
foreach($n in 'Wom1G3PteWait','Wom1G3PagingWait','Wom1G3LeafHistory','Wom1G3CopyQueryFailure','Wom1JobTiming971','Wom1G3Poison','Wom1G3PagingFailure','Wom1G3PrivateFailure','Wom1G4SubmitFailure','Wom1G4SubmitFailureCount','Wom1G3UnpublishedGroups','Wom1G3BrokerTiming','Wom1PagingCensus','Wom1G3PagingProfile','Wom1FwTiming1112','Wom1G3PrivateProcs','Wom1PagingCensus1121','Wom1AllocLife1123','Wom1DisplayRing1131','Wom1SubmitPath1135'){ if($p.$n){ [IO.File]::WriteAllBytes("$env:USERPROFILE\rcpt\$n.bin",$p.$n); $n } }
