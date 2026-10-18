# Guarded unattended Windows reinstall design

**Date:** 2026-09-02  
**Status:** approved by the operator  
**Scope:** redeploy Windows 11 Pro ARM64 into the existing J313 Windows
volumes, preserve Apple/Asahi partitions, automate OOBE and baseline
configuration, and install the hardware-qualified AppleInput package.

## Objective

Extend the existing guarded WinPE redeployment workflow so that, after one
explicit destructive confirmation, it can reinstall the existing Windows
installation without interactive Windows Setup or account creation. The
installed system must use an English UI, the French AZERTY keyboard layout,
and a local administrator account named `pavel` with password `a`. The host's
existing SSH public key must be provisioned for key-based administrative SSH.

The deployment is a Windows recovery operation, not a GPU experiment.
AppleAgx must remain absent until a clean post-install baseline passes.

## Selected approach

Use a guarded DISM apply workflow against the two existing volumes named
`Windows` and `WINESP`. Do not use Windows Setup disk selection and do not
repartition or clean the physical NVMe disk.

This is preferred over a `DiskConfiguration` answer file because the current
GPT contains Apple, Asahi, recovery, and Windows partitions on one physical
device. Selecting or cleaning a disk would unnecessarily put the non-Windows
partitions at risk. Volume-scoped formatting preserves the established layout
while still replacing every Windows OS and Windows EFI file.

## Sources inspected

- `scripts/reinstall-windows.cmd` and `tests/test_reinstall_windows.py`;
- `documentation/INSTALL.md` and
  `documentation/reference/windows-install-commands.txt`;
- `documentation/STABLE_8CORE_INPUT.md` and `documentation/APPLE_INPUT.md`;
- `investigation/EXPERIMENTS.md`, especially EXP145, EXP286, EXP316,
  EXP322, EXP323, and EXP333;
- current mounted ARM64 media at `/Volumes/WINDOWS ARM`;
- Microsoft documentation for answer files, `LocalAccount`, input locales,
  offline driver servicing, and BCDBoot.

The media is an exFAT filesystem at `/dev/disk8s1`. It contains an ARM64
`BOOTAA64.EFI`, a verified two-image `boot.wim`, and a verified three-image
`install.wim`. Windows 11 Pro is image index 3, architecture ARM64, build
26200.8037, default language `en-GB`. A size-only source-media comparison found
no missing or different-sized Windows media files after excluding macOS
metadata.

## Ownership and platform contract

- m1n1 owns preservation/virtualization of the J313 hardware and the synthetic
  NVMe bridge presented to the guest.
- Mu owns the UEFI boot environment, the physical xHCI bring-up, and the ACPI
  descriptions for Windows, including `ACPI\\APPL0001` and inert
  `ACPI\\APPL0002`.
- WinPE owns target discovery, preservation receipts, formatting of only the
  two Windows volumes, DISM image application, offline configuration, and EFI
  boot-file construction.
- Windows inbox `USBXHCI`/HID drivers own external USB keyboard and mouse.
  There is no separate project xHCI driver to inject.
- The project `AppleInput` KMDF/VHF package owns the built-in keyboard and
  Precision Touchpad after Windows PnP binds `ACPI\\APPL0001`.
- Windows Setup/unattend owns specialize/OOBE, local-account creation, locale,
  and the first-logon transition.
- Windows OpenSSH owns remote access. The installer provisions only the public
  half of `/Users/pavel/.ssh/air`; the private key never enters the media or
  guest.

## Media payload

The prepared USB will contain:

- the guarded redeployment command script;
- an unattended answer file copied to `Windows\\Panther\\unattend.xml`;
- post-setup and baseline-validation scripts;
- the host SSH public key and its recorded fingerprint;
- the exact EXP145 AppleInput package from Actions run `33113494073`:
  - `AppleInput.sys` SHA-256
    `15fbb42e0b7a686282c7874495fb1ac0214096a81a20e879b1a2690c9c19c1ef`;
  - `AppleInput.inf` SHA-256
    `6d57e67faa703e0698027c587d7761bb0b3287fbc96575daeb6573159fec0a23`;
  - `appleinput.cat` SHA-256
    `325875ac2aacabfbb2ab008dcb31149bb7e9c7361143e7d16e31bac61cd8bd07`;
  - `AppleInputTest.cer` SHA-256
    `fea6b5b61a715ec47317abcfb77988d56023a218e7a373b8347237449d1269ed`;
- a manifest containing hashes and deployment settings.

The media builder may add the launcher payload to both `boot.wim` indices so
that the same guarded workflow starts regardless of which WinPE index Mu
enters. The original WIM must be backed up or the rebuilt media must be emitted
as a separately named artifact before it replaces the active USB copy.

## Guarded target discovery

Discovery is read-only until all checks pass. The installer must:

1. Find exactly one source volume containing the verified ARM64
   `sources\\install.wim` and `EFI\\BOOT\\BOOTAA64.EFI`.
2. Find exactly one NTFS volume with the exact label `Windows`.
3. Find exactly one FAT32 volume with the exact label `WINESP`, including an
   unlettered hidden ESP.
4. Prove that source, OS, and ESP are three distinct volumes.
5. Prove that `Windows` and `WINESP` belong to the same internal synthetic NVMe
   disk and that the source does not belong to that disk.
6. Verify the expected volume filesystem, size range, and existing boot/OS
   identity. Any ambiguity is a hard stop.
7. Display the resolved source, physical disk, partitions/volumes, image index,
   edition, and every path that will be preserved or erased.

The only destructive gate is the exact literal `ERASE WINDOWS`. No partition
table command may be constructed before that gate.

## Evidence preservation

Before formatting, copy a bounded recovery/evidence set to a timestamped
directory on the USB and hash it:

- current SYSTEM, SOFTWARE, DEFAULT, SAM, SECURITY hives and transaction logs;
- current Windows BCD;
- current minidumps and memory-dump metadata when present;
- EXP305 and the durable EXP316 pre-VSS rollback material when present;
- a DiskPart inventory, file manifest, and SHA-256 receipt.

Preservation failure is a hard stop before formatting. A complete disk image is
out of scope; this set preserves the boot/GPU investigation evidence needed
after redeployment.

## Deployment sequence

After the exact confirmation:

1. Quick-format only the resolved `Windows` volume as NTFS and the resolved
   `WINESP` volume as FAT32.
2. Apply `install.wim` image index 3 with DISM.
3. Verify `Windows\\System32\\winload.efi`.
4. Apply the French keyboard locale `040C:0000040C` to the offline image while
   retaining the image's English `en-GB` UI.
5. Copy the validated unattended and post-setup payload into the offline image.
6. Run BCDBoot against the resolved ESP using UEFI mode and `en-GB` locale.
7. Copy `bootmgfw.efi` to `EFI\\BOOT\\BOOTAA64.EFI` and verify the BCD and both
   loaders.
8. Enable test signing only in the newly created Windows loader BCD because
   AppleInput is test-signed.
9. Persist deployment logs and final hashes on the USB.

No AppleAgx INF, SYS, service, certificate, or staged package is copied or
installed.

## Unattended Windows configuration

The answer file uses the ARM64 component architecture and the normal
`specialize`/`oobeSystem` passes:

- UI language and fallback: `en-GB`;
- system/user locale: `en-GB`;
- input locale: `040C:0000040C` (French AZERTY);
- time zone: `Romance Standard Time`;
- computer name: `J313-WIN`;
- local administrator: `pavel`;
- password: literal `a`, as explicitly selected by the operator;
- automated first logon and OOBE pages suppressed where the Windows edition
  supports the documented setting.

The password is intentionally weak and will be visible in the answer file.
Therefore password authentication for SSH remains disabled. The password is a
local console/recovery credential only.

## AppleInput and first-boot configuration

A system-context post-setup script must:

1. Verify every AppleInput payload hash before use.
2. Trust only the exact AppleInput test certificate in the required local
   machine stores.
3. Stage/install the exact AppleInput INF.
4. Set `TransportOnly=0`, `PublishKeyboard=1`, and `PublishTrackpad=1`.
5. Rescan/restart only `ACPI\\APPL0001\\0` when present.
6. Record the published `oem*.inf`, service state, parent status, VHF children,
   and package hashes. It must not assume a published INF number.
7. Leave inbox USBXHCI/HID unmodified.

OpenSSH provisioning must install/enable the server capability if available,
set `sshd` to automatic start, create the administrators authorized-key file
with the host public key, apply the required ACL, enable the private-network
firewall rule, and keep password authentication disabled. If the OpenSSH
capability needs a network payload that is not locally available, the script
must log that exact blocker and retry after network readiness without blocking
the desktop.

The first-logon script also disables hibernation/Fast Startup for the
development baseline and removes one-time autologon state after provisioning.

## Verification and exit criteria

Offline verification must cover parsing, target uniqueness, destructive-order
gates, forbidden disk commands, WIM/image identity, unattend structure,
AppleInput hashes, public-key fingerprint, and post-setup idempotence.

The first real boot is a baseline-only boot. PASS requires:

- Windows reaches lock/login and desktop without OOBE interaction;
- account `pavel` exists and French AZERTY is active;
- all 8 CPUs are online;
- NVMe and external xHCI/input are healthy;
- AppleInput, APPL0001, keyboard child, and Precision Touchpad child are
  healthy;
- SSH key login succeeds using `/Users/pavel/.ssh/air`;
- Event 129, new bugcheck, and reset counts are zero;
- registry hives load normally;
- exactly one inert `ACPI\\APPL0002` is visible with no AppleAgx package,
  service, module, signer, or staged driver.

Only after that gate passes may a fresh recovery checkpoint be created and GPU
driver work resume.

## Failure and recovery

- Before formatting: abort without installed-Windows mutation.
- After OS format but before successful DISM/BCDBoot: retain logs on USB and
  rerun only after the failure is understood; do not touch other partitions.
- After deployment but before baseline PASS: return to the same WinPE media,
  collect offline hives/BCD/logs, and diagnose or repeat the same deterministic
  deployment. Do not install AppleAgx.
- A target-identity mismatch or unexpected physical-disk relationship forbids
  the destructive phase.

The smallest hardware checkpoint is one clean boot through the pinned
EXP241 m1n1/Mu pair to Windows desktop plus successful SSH-key login. The
recovery path is the installer USB and the untouched Apple/Asahi boot chain.
