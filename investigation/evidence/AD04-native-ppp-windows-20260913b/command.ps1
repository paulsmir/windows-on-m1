$ErrorActionPreference='Stop'
if(Test-Path 'C:/Users/pauls/AD04-native-ppp-windows-20260913b'){throw 'Fresh source path required'}
if((Get-FileHash 'C:/Users/pauls/AD04-native-ppp-windows-20260913b.tar.gz' -Algorithm SHA256).Hash.ToLowerInvariant() -ne '8c1cd094391a0204b72933b22e41312ca39e9c9c9bc5e5d4eb7c01dc9b797d7a'){throw 'Source archive hash mismatch'}
[void](New-Item -ItemType Directory 'C:/Users/pauls/AD04-native-ppp-windows-20260913b')
tar -xf 'C:/Users/pauls/AD04-native-ppp-windows-20260913b.tar.gz' -C 'C:/Users/pauls/AD04-native-ppp-windows-20260913b'
if($LASTEXITCODE){exit $LASTEXITCODE}
& 'C:/Users/pauls/AD04-native-ppp-windows-20260913b/drivers/apple-agx/mesa/scripts/run-native-owner-contract.ps1' -Project 'C:/Users/pauls/AD04-native-ppp-windows-20260913b' -NativeRoot 'C:/Users/pauls/AD04-native-ppp-windows-20260913b-native-x64' -ResultRoot 'C:/Users/pauls/AD04-native-ppp-windows-20260913b-x64' -Architecture x64
$x=$LASTEXITCODE
& 'C:/Users/pauls/AD04-native-ppp-windows-20260913b/drivers/apple-agx/mesa/scripts/run-native-owner-contract.ps1' -Project 'C:/Users/pauls/AD04-native-ppp-windows-20260913b' -NativeRoot 'C:/Users/pauls/AD04-native-ppp-windows-20260913b-native-arm64' -ResultRoot 'C:/Users/pauls/AD04-native-ppp-windows-20260913b-arm64' -Architecture arm64
if($x){exit $x}
exit $LASTEXITCODE
