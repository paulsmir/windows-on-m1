$ErrorActionPreference='Stop'
$b='C:\Users\pavel\EXP866'
if((Get-FileHash "$b\hardware-manifest.json").Hash -ne '0a2268bdafa69116ab1c7b057f41ca2292455711a698392bbb71659c313534a7'){throw 'hardware manifest mismatch'}
if((Get-FileHash "$b\guest-transfer-manifest.json").Hash -ne 'd732416e8845a251a81753a91caeddfded837899444996677b6eb71efcbae6d9'){throw 'transfer manifest mismatch'}
$m=Get-Content "$b\guest-transfer-manifest.json" -Raw|ConvertFrom-Json
foreach($p in $m.files.PSObject.Properties){$file=Join-Path $b $p.Name;if($p.Name -match '\.(inf|sys|dll|cat)$'){$file=Join-Path "$b\package" $p.Name};if((Get-FileHash $file).Hash -ne $p.Value){throw "mismatch $($p.Name)"}}
& "$b\verify-cleanup.ps1"
if(!$?){throw 'baseline refused'}
'TRANSFER_PASS_39_OF_39'
