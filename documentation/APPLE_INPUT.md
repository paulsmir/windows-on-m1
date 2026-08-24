# Built-in Apple keyboard and Precision Touchpad

## Validated J313 checkpoint

The native input stack is hardware validated on the 2020 M1 MacBook Air
(`j313`). Windows receives the built-in keyboard and trackpad through a native
Apple SPI-HID transport driver and two Virtual HID Framework (VHF) children.
The trackpad is exposed as a Windows Precision Touchpad, not as an emulated USB
mouse.

The accepted checkpoint is `EXP-20260824-054` on branch
`feature/j313-native-input`. The exact package came from the official
`Apple input ARM64 WDK` workflow run
[32754271477](https://github.com/paulsmir/windows-on-m1/actions/runs/32754271477),
job `97518075905`, artifact `AppleInput-ARM64-Debug`.

| File | SHA-256 |
| --- | --- |
| `AppleInput.sys` | `7b75873de00a392b6e906edf5776f69c274e86814fb02389414ef557d2b7bdb5` |
| `AppleInput.inf` | `ca844ebf9a0fab6ae4a6aa434033eb487ca246b9248bc4fde968539ca26565cd` |
| `appleinput.cat` | `e11befe19ef7b0dac31360b348394a65259dcb12ea7e7b6bd8ca66097dc0187f` |
| `AppleInputDiag.exe` | `d842e47ee5b8c9299b3f3ceb8027855c016f28494fb4e0f4be7dc0d801f5c3f7` |

The package installed as `oem16.inf` on the validation machine. That published
name is assigned by Windows and will normally be different on another
installation.

The automatic gate proved both VHF frontends running, valid native trackpad
geometry, successful Windows feature negotiation, two healthy
`HID_DEVICE_SYSTEM_VHF` children, and zero transport or VHF errors. The bounded
physical test then advanced 28/28 accepted keyboard reports and 7185/7185
decoded and submitted trackpad reports with no rejection or submission
failure. Built-in typing, pointer motion, left and right click, multitouch, and
simultaneous keyboard and trackpad use were confirmed by the user. Long-duration
stress and complete gesture certification remain separate future gates; they
are not implied by this bounded checkpoint.

Keep an external USB keyboard and mouse connected while installing or replacing
the driver. They are the recovery path if the test package cannot start.

## Architecture

The implementation is native and split across the layers that own each
contract:

1. m1n1 validates the live ADT resources, preserves the Apple input hardware,
   maps the reviewed SPI3 and GPIO regions through stage 2, and translates
   physical IRQ 330 to guest INTID 865.
2. Mu publishes `ACPI\APPL0001\0` with matching MMIO, GPIO, and interrupt
   resources.
3. The ARM64 KMDF driver owns the bounded Apple SPI-HID transport, interrupt,
   discovery, CRC validation, and passive worker.
4. The keyboard VHF child publishes the hardware-derived keyboard contract.
5. The driver obtains native trackpad dimensions with Apple feature report
   `0xd9`, validates the signed logical bounds and physical dimensions, then
   publishes a separate Windows Precision Touchpad VHF child.

No VHF operation runs in the ISR. Reports are validated and submitted from the
passive transport worker. Keyboard and trackpad publication are independent, so
a trackpad failure does not have to remove a working keyboard. Teardown blocks
new submissions and synchronously deletes both VHF objects before releasing
MMIO resources.

The INF deliberately defaults to the fail-closed transport-only state:

```text
TransportOnly=1
PublishKeyboard=0
PublishTrackpad=0
```

Do not change those defaults in the package. Enable publication only after the
transport and geometry diagnostics pass on the target machine.

## Build the ARM64 package

### Recommended: official GitHub Actions WDK build

From a clone of this repository with GitHub CLI authentication:

```sh
gh workflow run apple-input-wdk.yml --ref feature/j313-native-input
gh run list --workflow apple-input-wdk.yml --branch feature/j313-native-input
gh run watch RUN_ID
gh run download RUN_ID --name AppleInput-ARM64-Debug --dir AppleInput-ARM64-Debug
```

Replace `RUN_ID` with the new successful run number. Do not reuse the hashes in
the table above for a newly built package: calculate and record the hashes of
the files that will actually be installed.

### Local Windows WDK build

Use an ARM64-capable Windows WDK/MSBuild environment and run from the repository
root in a Developer PowerShell:

```powershell
nuget restore packages.config -PackagesDirectory packages
msbuild drivers\apple-input\windows\AppleInput.vcxproj /m /t:Clean,Build /p:Configuration=Debug /p:Platform=ARM64 /p:RunCodeAnalysis=true
msbuild drivers\apple-input\windows\tools\AppleInputDiag\AppleInputDiag.vcxproj /m /t:Clean,Build /p:Configuration=Debug /p:Platform=ARM64
```

Place `AppleInput.sys`, `AppleInput.inf`, `appleinput.cat`, and
`AppleInputDiag.exe` in one working directory on the J313 Windows installation.
Confirm that the SYS and EXE are ARM64 binaries and record all four hashes:

```powershell
Get-FileHash .\AppleInput.sys,.\AppleInput.inf,.\appleinput.cat,.\AppleInputDiag.exe -Algorithm SHA256
```

## Install the test-signed driver

These commands require an elevated ARM64 Windows PowerShell. The package is a
development driver, not a production-signed release.

### 1. Enable test signing and reboot once

```powershell
bcdedit /set testsigning on
shutdown /r /t 0
```

Return to the package directory after Windows restarts. Verify that the catalog
contains a signer, export that exact certificate, and trust it only in the two
machine stores needed for this test package:

```powershell
$signature = Get-AuthenticodeSignature .\appleinput.cat
if ($signature.Status -eq 'NotSigned' -or !$signature.SignerCertificate) { throw 'Catalog has no signer certificate' }
$certificate = Join-Path $PWD 'AppleInput-Test.cer'
[IO.File]::WriteAllBytes($certificate, $signature.SignerCertificate.Export([Security.Cryptography.X509Certificates.X509ContentType]::Cert))
Import-Certificate -FilePath $certificate -CertStoreLocation Cert:\LocalMachine\Root
Import-Certificate -FilePath $certificate -CertStoreLocation Cert:\LocalMachine\TrustedPublisher
$signature.SignerCertificate.Thumbprint
```

Record the thumbprint. It lets you remove only this test certificate later.

### 2. Install in transport-only mode

```powershell
pnputil /add-driver .\AppleInput.inf /install
pnputil /restart-device "ACPI\APPL0001\0"
```

Record the `Published Name` printed by `pnputil`, such as `oem16.inf`. Confirm
which INF actually owns the device:

```powershell
$instance = 'ACPI\APPL0001\0'
$publishedInf = (Get-PnpDeviceProperty -InstanceId $instance -KeyName DEVPKEY_Device_DriverInfPath).Data
$publishedInf
Get-PnpDevice -InstanceId $instance
.\AppleInputDiag.exe status --json
```

The initial diagnostic gate requires phase 8, `trackpad_init_phase=3`, valid X
and Y axes, and zero timeouts, CRC failures, fragment failures, offline
transitions, rejected reports, VHF start failures, and submission failures. Both
VHF states must still be zero because publication is disabled.

### 3. Enable the built-in keyboard

```powershell
$parameters = 'HKLM:\SYSTEM\CurrentControlSet\Services\AppleInput\Parameters'
New-Item -Path $parameters -Force | Out-Null
New-ItemProperty -Path $parameters -Name TransportOnly -PropertyType DWord -Value 0 -Force | Out-Null
New-ItemProperty -Path $parameters -Name PublishKeyboard -PropertyType DWord -Value 1 -Force | Out-Null
New-ItemProperty -Path $parameters -Name PublishTrackpad -PropertyType DWord -Value 0 -Force | Out-Null
pnputil /restart-device "ACPI\APPL0001\0"
.\AppleInputDiag.exe status --json
```

Require `keyboard_vhf_state=3`, `trackpad_vhf_state=0`, a healthy new system-VHF
HID child, increasing accepted/submitted keyboard counters, and all error
counters at zero. Type with the built-in keyboard before continuing.

### 4. Enable the Precision Touchpad

```powershell
New-ItemProperty -Path $parameters -Name PublishTrackpad -PropertyType DWord -Value 1 -Force | Out-Null
pnputil /restart-device "ACPI\APPL0001\0"
$status = .\AppleInputDiag.exe status --json | ConvertFrom-Json
$status | Format-List
Get-PnpDevice -PresentOnly | Where-Object { $_.InstanceId -like 'HID\HID_DEVICE_SYSTEM_VHF*' } | Format-Table Status,Class,FriendlyName,InstanceId -AutoSize
```

Do not search only for `HID\VID_05AC&PID_0000*`; Windows gives VHF children
system-generated `HID_DEVICE_SYSTEM_VHF` instance IDs. Accept the trackpad only
when all of the following are true:

- `phase=8` and `trackpad_init_phase=3`;
- `keyboard_vhf_state=3` and `trackpad_vhf_state=3`;
- both axis-valid fields are 1 and both logical and physical ranges are
  nondegenerate;
- GET_FEATURE and SET_FEATURE counts are nonzero;
- `trackpad_feature_last_status=0` and `trackpad_vhf_last_status=0`;
- timeout, CRC, fragment, offline, rejected-report, VHF-start, and submission
  failure counters are all zero;
- keyboard and trackpad accepted/submitted counters increase during physical
  input;
- the APPL0001 parent and new VHF children have `Status=OK` and no problem code.

The helper command for later reinstallations is:

```powershell
drivers\apple-input\windows\scripts\install-driver.ps1 -InfPath .\AppleInput.inf -PublishKeyboard -PublishTrackpad
```

Use it only after test signing and the catalog certificate are already in
place, and only on a package/hardware combination that has passed the staged
gate above. The staged procedure remains the recommended first installation.

## Roll back safely

Keep external USB input connected. In an elevated PowerShell, disable both VHF
frontends before deleting the package:

```powershell
$parameters = 'HKLM:\SYSTEM\CurrentControlSet\Services\AppleInput\Parameters'
New-ItemProperty -Path $parameters -Name TransportOnly -PropertyType DWord -Value 1 -Force | Out-Null
New-ItemProperty -Path $parameters -Name PublishKeyboard -PropertyType DWord -Value 0 -Force | Out-Null
New-ItemProperty -Path $parameters -Name PublishTrackpad -PropertyType DWord -Value 0 -Force | Out-Null
pnputil /restart-device "ACPI\APPL0001\0"
pnputil /delete-driver $publishedInf /uninstall /force
```

If `$publishedInf` is no longer present in the current PowerShell session, read
it from the installation record; do not guess an `oemNN.inf` number. If a known
good earlier AppleInput package was preserved, reinstall that exact package and
verify its recorded SYS hash before restarting APPL0001.

After no test-signed driver is needed, remove only the recorded certificate and
turn test signing off:

```powershell
$thumbprint = 'RECORDED_CERTIFICATE_THUMBPRINT'
Remove-Item "Cert:\LocalMachine\TrustedPublisher\$thumbprint"
Remove-Item "Cert:\LocalMachine\Root\$thumbprint"
bcdedit /deletevalue testsigning
shutdown /r /t 0
```

## Reproduce the validated diagnostic checkpoint

Run this after exercising the built-in keyboard, pointer motion, and click:

```powershell
.\AppleInputDiag.exe status --json | Set-Content -Encoding ascii .\apple-input-status.json
Get-FileHash .\apple-input-status.json -Algorithm SHA256
```

The accepted physical snapshot for `EXP-20260824-054` had SHA-256
`1b87c25e4294b2ccc7083c80648e914bfd3c7c90d6ed2fd81078dce7c7ba0c71`.
It recorded 28 keyboard reports and 7185 trackpad reports, with every report
accepted/decoded and submitted and every transport, parser, VHF, and feature
status counter clean. Raw key values, contacts, coordinates, and report payloads
are intentionally absent from diagnostics.

## Design and implementation records

- `documentation/design/2026-08-24-vhf-keyboard-precision-touchpad.md`
- `documentation/plans/2026-08-24-vhf-keyboard-implementation.md`
- `documentation/design/2026-08-09-native-apple-input.md`
- `documentation/plans/2026-08-09-native-apple-input-implementation.md`
- `documentation/verification/J313_NATIVE_INPUT_V1.md`
- `investigation/EXPERIMENTS.md`, `EXP-20260824-054`
