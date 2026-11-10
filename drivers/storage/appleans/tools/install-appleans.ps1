<#
  Standard AppleAns install on the installed Windows (like the AppleInput driver: permanent own
  test-signing certificate, staged package, boot-start service). Idempotent; run elevated in either
  boot mode (bridge/stornvme or native AppleAns).

    install-appleans.ps1 -PackageDir <dir with AppleAns.inf/.sys/.cat> -SysSha256 <hex> -CertFile <.cer>

  Steps:
    1. Trust the AppleAns signing certificate permanently (LocalMachine\Root + TrustedPublisher).
       It is a dedicated certificate (not the AGX experiment signer), so GPU rollbacks keep it.
    2. Stage the package (pnputil /add-driver) and point Services\AppleAns\ImagePath at the store
       copy whose AppleAns.sys hash matches -SysSha256. Service: kernel, boot-start (Start=0),
       group "SCSI Miniport".
    3. Device-configuration records (DriverDatabase Configurations/Descriptors/Strings) and the
       persisted Enum\ACPI\APPL0003\0 devnode: created only when missing (ANS010-R2/R5 contract;
       online staging without a present device omits them). Written by a one-shot SYSTEM task.
    4. AnsStartOverrideGuard (ans-dual-boot.ps1 install) keeps native and bridge boots bootable.
  Revert: C:\Users\pavel\ans-revert-bootstart.ps1 (Start=3, guard removed).
#>
param(
    [Parameter(Mandatory)][string]$PackageDir,
    [Parameter(Mandatory)][ValidatePattern('^[0-9a-f]{64}$')][string]$SysSha256,
    [Parameter(Mandatory)][string]$CertFile
)
$ErrorActionPreference = 'Stop'
$svcKey = 'HKLM:\SYSTEM\CurrentControlSet\Services\AppleAns'
$repo = "$env:SystemRoot\System32\DriverStore\FileRepository"
$work = 'C:\ProgramData\AppleAns'
New-Item -ItemType Directory -Force $work | Out-Null

# 1. certificate
$cert = New-Object System.Security.Cryptography.X509Certificates.X509Certificate2 (Resolve-Path $CertFile).Path
$signer = (Get-AuthenticodeSignature (Join-Path $PackageDir 'AppleAns.sys')).SignerCertificate
if ($signer.Thumbprint -ne $cert.Thumbprint) { throw "AppleAns.sys is signed by $($signer.Thumbprint), not $($cert.Thumbprint)" }
if ((Get-FileHash (Join-Path $PackageDir 'AppleAns.sys') -Algorithm SHA256).Hash.ToLower() -ne $SysSha256) { throw 'AppleAns.sys hash mismatch' }
foreach ($n in 'Root', 'TrustedPublisher') {
    $st = New-Object System.Security.Cryptography.X509Certificates.X509Store($n, 'LocalMachine'); $st.Open('ReadWrite')
    if (-not @($st.Certificates | ? Thumbprint -eq $cert.Thumbprint).Count) { $st.Add($cert); "CERT_ADDED $n $($cert.Thumbprint)" }
    $st.Close()
}
Copy-Item -Force (Resolve-Path $CertFile).Path "$work\AppleAnsStorageTestSigning.cer"

# 2. package + service
& pnputil.exe /add-driver (Join-Path $PackageDir 'AppleAns.inf') 2>&1 | % { "PNPUTIL $_" }
$store = @(Get-ChildItem $repo -Directory -Filter 'appleans.inf_arm64_*' | ? {
    (Test-Path (Join-Path $_.FullName 'AppleAns.sys')) -and
    (Get-FileHash (Join-Path $_.FullName 'AppleAns.sys') -Algorithm SHA256).Hash.ToLower() -eq $SysSha256 })
if ($store.Count -ne 1) { throw "driver store copies of the requested AppleAns.sys: $($store.Count)" }
$published = (Get-Item "HKLM:\SYSTEM\DriverDatabase\DriverPackages\$($store[0].Name)").GetValue('')
if (-not (Test-Path $svcKey)) {
    New-Item $svcKey | Out-Null
    New-ItemProperty $svcKey -Name Type -PropertyType DWord -Value 1 | Out-Null
    New-ItemProperty $svcKey -Name ErrorControl -PropertyType DWord -Value 1 | Out-Null
    New-ItemProperty $svcKey -Name Group -PropertyType String -Value 'SCSI Miniport' | Out-Null
    New-ItemProperty $svcKey -Name DisplayName -PropertyType String -Value 'Apple ANS StorPort Miniport' | Out-Null
    New-ItemProperty $svcKey -Name Start -PropertyType DWord -Value 0 | Out-Null
    New-ItemProperty $svcKey -Name ImagePath -PropertyType ExpandString -Value 'placeholder' | Out-Null
    "SERVICE_CREATED"
}
Set-ItemProperty $svcKey -Name ImagePath -Value "\SystemRoot\System32\DriverStore\FileRepository\$($store[0].Name)\AppleAns.sys"
"IMAGEPATH $((Get-ItemProperty $svcKey).ImagePath) PUBLISHED $published"
# This install path binds the service by ImagePath and never runs the INF's service AddReg, so the
# Storport bus type (INF AppleAnsService.AddReg: BusTypeNvme, default would be Fibre Channel) is set here.
if (-not (Test-Path "$svcKey\Parameters")) { New-Item "$svcKey\Parameters" -Force | Out-Null }
Set-ItemProperty "$svcKey\Parameters" -Name BusType -Value 0x11 -Type DWord
"BUSTYPE $((Get-ItemProperty "$svcKey\Parameters").BusType)"

# 3. records + devnode (SYSTEM-owned keys), only when missing
$cmds = @()
$records = @(Get-ChildItem HKLM:\SYSTEM\DriverDatabase\DriverPackages | ? { $_.PSChildName -like 'appleans.inf_*' -and $_.SubKeyCount -ge 3 })
if (-not $records.Count) {
    $k = "HKEY_LOCAL_MACHINE\SYSTEM\DriverDatabase\DriverPackages\$($store[0].Name)"
    Set-Content "$work\records.reg" -Encoding Unicode @(
        'Windows Registry Editor Version 5.00', '',
        "[$k\Configurations\AppleAnsInstall.NT]", '"Service"="AppleAns"', '"ConfigScope"=dword:00000f7f', '"ConfigFlags"=dword:00000000', '',
        "[$k\Descriptors\ACPI\APPL0003]", '"Configuration"="AppleAnsInstall.NT"', '"Manufacturer"="%provider%"', '"Description"="%devicedesc%"', '',
        "[$k\Strings]", '"devicedesc"="Apple ANS physical storage qualification adapter"', '"provider"="J313 Native ANS Project"')
    $cmds += "reg import $work\records.reg"
}
$enum = 'HKLM:\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0003\0'
if ((Get-ItemProperty $enum -EA SilentlyContinue).Service -ne 'AppleAns') {
    $classRoot = 'HKLM:\SYSTEM\CurrentControlSet\Control\Class\{4d36e97b-e325-11ce-bfc1-08002be10318}'
    $index = 0; while (Test-Path ('{0}\{1:D4}' -f $classRoot, $index)) { $index++ }
    $inst = '{0:D4}' -f $index
    $hw = 'ACPI\VEN_APPL&DEV_0003', 'ACPI\APPL0003', '*APPL0003'
    $hex = (($hw | % { [Text.Encoding]::Unicode.GetBytes($_) + 0, 0 }) + 0, 0 | % { '{0:x2}' -f $_ }) -join ','
    Set-Content "$work\devnode.reg" -Encoding Unicode @(
        'Windows Registry Editor Version 5.00', '',
        '[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0003\0]', "`"HardwareID`"=hex(7):$hex",
        '"Capabilities"=dword:00000030', '"ContainerID"="{00000000-0000-0000-ffff-ffffffffffff}"', '"ConfigFlags"=dword:00000000',
        '"ClassGUID"="{4d36e97b-e325-11ce-bfc1-08002be10318}"', '"Class"="SCSIAdapter"', '"Service"="AppleAns"',
        "`"DeviceDesc`"=`"@$published,%devicedesc%;Apple ANS physical storage qualification adapter`"",
        "`"Mfg`"=`"@$published,%provider%;J313 Native ANS Project`"", "`"Driver`"=`"{4d36e97b-e325-11ce-bfc1-08002be10318}\\$inst`"", '',
        '[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Enum\ACPI\APPL0003\0\Device Parameters]', '',
        "[HKEY_LOCAL_MACHINE\SYSTEM\CurrentControlSet\Control\Class\{4d36e97b-e325-11ce-bfc1-08002be10318}\$inst]",
        '"DriverDesc"="Apple ANS physical storage qualification adapter"', '"ProviderName"="J313 Native ANS Project"',
        "`"InfPath`"=`"$published`"", '"InfSection"="AppleAnsInstall"', '"InfSectionExt"=".NT"', '"MatchingDeviceId"="ACPI\\APPL0003"')
    $cmds += "reg import $work\devnode.reg"
}
if ($cmds.Count) {
    Set-Content "$work\system.cmd" -Encoding ASCII (($cmds | % { "$_ >> $work\system.log 2>&1" }) -join "`r`n")
    schtasks /Create /TN AppleAnsInstallOnce /TR "$work\system.cmd" /SC ONCE /ST 23:59 /RU SYSTEM /F | Out-Null
    schtasks /Run /TN AppleAnsInstallOnce | Out-Null; Start-Sleep 8; schtasks /Delete /TN AppleAnsInstallOnce /F | Out-Null
    Get-Content "$work\system.log" -EA SilentlyContinue | % { "SYSTEM $_" }
}

# 4. dual boot guard (AppleAns Start=0, StartOverride cleared every boot)
& powershell -NoProfile -ExecutionPolicy Bypass -File (Join-Path $PSScriptRoot 'ans-dual-boot.ps1') install
"INSTALL_APPLEANS_OK"
