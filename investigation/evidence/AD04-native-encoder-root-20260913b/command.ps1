$ErrorActionPreference='Stop'
if(Test-Path 'C:/Users/pauls/AD04-native-encoder-root-20260913b'){throw 'Fresh source path required'}
if((Get-FileHash 'C:/Users/pauls/AD04-native-encoder-root-20260913b.tar.gz' -Algorithm SHA256).Hash.ToLowerInvariant() -ne '34043fad8acb277b71082b27fbccb51e8532b0a7f55e6d1a5199c338543695cc'){throw 'Source archive hash mismatch'}
[void](New-Item -ItemType Directory 'C:/Users/pauls/AD04-native-encoder-root-20260913b')
tar -xf 'C:/Users/pauls/AD04-native-encoder-root-20260913b.tar.gz' -C 'C:/Users/pauls/AD04-native-encoder-root-20260913b'
if($LASTEXITCODE){exit $LASTEXITCODE}
& 'C:/Users/pauls/AD04-native-encoder-root-20260913b/drivers/apple-agx/mesa/scripts/run-native-owner-contract.ps1' -Project 'C:/Users/pauls/AD04-native-encoder-root-20260913b' -NativeRoot 'C:/Users/pauls/AD04-native-encoder-root-20260913b-native-x64' -ResultRoot 'C:/Users/pauls/AD04-native-encoder-root-20260913b-x64' -Architecture x64
$x=$LASTEXITCODE
& 'C:/Users/pauls/AD04-native-encoder-root-20260913b/drivers/apple-agx/mesa/scripts/run-native-owner-contract.ps1' -Project 'C:/Users/pauls/AD04-native-encoder-root-20260913b' -NativeRoot 'C:/Users/pauls/AD04-native-encoder-root-20260913b-native-arm64' -ResultRoot 'C:/Users/pauls/AD04-native-encoder-root-20260913b-arm64' -Architecture arm64
if($x){exit $x}
exit $LASTEXITCODE
