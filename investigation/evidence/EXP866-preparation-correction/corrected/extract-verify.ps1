$ErrorActionPreference='Stop'
$b='C:\Users\pavel\EXP866'
if((Get-FileHash "$b\guest-payload.tar").Hash -ne 'adf57fbf9a7fb5c58bbcfa250c475bf454dbc11e6e49eec4160280df5f8b1f53'){throw 'payload mismatch'}
& tar.exe -xf "$b\guest-payload.tar" -C $b
if($LASTEXITCODE){throw 'extract failed'}
& "$b\verify-transfer.ps1"
if(!$?){throw 'transfer gate failed'}
