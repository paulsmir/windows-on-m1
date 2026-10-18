$ErrorActionPreference='Stop'
. "$PSScriptRoot\etl-contract.ps1"
if(!(Test-OriginalArmedBoot 1 0 0 '30.0.865.0')){throw 'original boot rejected'}
foreach($c in @(@(0,45,0,'30.0.865.0'),@(1,0,1,'30.0.865.0'),@(1,0,0,'30.0.1.0'),@(1,28,0,''))){if(Test-OriginalArmedBoot @c){throw 'foreign boot accepted'}}
$d=Join-Path $env:TEMP ([Guid]::NewGuid().ToString());New-Item -ItemType Directory $d|Out-Null
try {
 $p=Join-Path $d 'original-test.etl';[IO.File]::WriteAllBytes($p,[byte[]](1,2,3))
 $r=[pscustomobject]@{OriginalArmedBoot=$true;BootUtc='boot1';Etls=@([pscustomobject]@{Name='original-test.etl';SHA256=(Get-FileHash $p).Hash})}
 Assert-OriginalEtlReceipt $r 'boot1' $d
 $caught=$false;try{Assert-OriginalEtlReceipt $r 'boot2' $d}catch{$caught=$true};if(!$caught){throw 'stale boot accepted'}
 [IO.File]::WriteAllBytes($p,[byte[]](4,5,6));$caught=$false;try{Assert-OriginalEtlReceipt $r 'boot1' $d}catch{$caught=$true};if(!$caught){throw 'changed ETL accepted'}
 $r.Etls=@();$caught=$false;try{Assert-OriginalEtlReceipt $r 'boot1' $d}catch{$caught=$true};if(!$caught){throw 'missing ETL accepted'}
} finally {Remove-Item $d -Recurse -Force}
'EXP865_ETL_CONTRACT_PASS'
