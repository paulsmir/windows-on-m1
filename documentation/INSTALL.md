# Standalone installation on a J313 MacBook Air

This guide records the installation procedure used on the development machine. It assumes
that the reader is comfortable identifying GPT partitions, recovering an Apple Silicon Mac,
and reviewing every destructive command before pressing Enter.

The normal result is a MacBook Air that enters the Apple-authorized Asahi boot entry and
then starts Windows without another Mac. A second Mac is useful for development and recovery
but is not a standalone runtime dependency.

## Safety model

Back up all data before starting. Keep macOS and Apple Recovery intact. The Windows
partitions must be created only in the unallocated space produced by the official Asahi
installer. Do not resize the APFS container separately with Disk Utility or `diskutil`.

Never run DiskPart `clean` on the internal SSD. Never delete or format an existing Apple,
macOS, Recovery, or Asahi partition because its type is shown as `Unknown` by WinPE. Disk
and partition numbers in the examples are observations, not identifiers.

The project intentionally does not automate internal-SSD repartitioning. The official Asahi
installer owns the initial APFS resize and creation of the Apple-authorized boot environment;
WinPE later partitions only the unallocated Windows extent.

## What the installation media does

Copy the complete contents of a Windows 11 ARM64 ISO to an exFAT USB device. The USB serves
two purposes:

1. its `\EFI\BOOT\BOOTAA64.EFI` starts Windows PE and the graphical Setup environment;
2. its `sources\install.wim` contains offline Windows editions that DISM can apply.

`install.wim` is not booted directly. The graphical Setup UI is used only to reach WinPE
and open Command Prompt with `Shift+F10`. Partitioning, image application, and boot-file
creation are performed manually in that command prompt.

## 1. Back up and provision the Apple-authorized UEFI environment

Before preparing Windows media or running any Windows partitioning command, create a tested
external backup. Run the official Asahi installer from macOS. The Asahi installer performs
the APFS shrink; this is the step that reduces the macOS allocation and makes room for the
UEFI boot environment and Windows. Do not shrink the APFS container manually before or after
this step.

Inspect the starting layout for reference only:

```sh
diskutil list
diskutil apfs list
```

In the Asahi installer, select the option that provisions a UEFI environment for another
operating system (the UEFI-only installation, not an Asahi Linux desktop or minimal Linux
system). Use the installer's sizing flow to free the total capacity intended for the small
Asahi boot environment plus Windows. The installer creates the required boot container and
ESP; the remaining intended Windows capacity must stay unallocated. Do not create an exFAT
Windows target in macOS because WinPE will create the Windows GPT partitions later.

Record the sizes and roles of every existing partition. Photographs or a text capture of
`diskutil list` are useful during WinPE, where Apple partitions may be reported as unknown.

Complete the reboot requested by the Asahi installer. This reboot establishes the boot
policy and second-stage files; stopping before it leaves an incomplete boot entry. Once the
UEFI entry has booted successfully, return to macOS and verify the resulting layout with
`diskutil list` before continuing.

Linux is not required as the Windows runtime. Asahi is used here to create an
Apple-authorized boot chain and ESP layout.

## 2. Obtain the project

Either unpack a checksummed release or clone the coordinator and both pinned forks:

```sh
git clone --recurse-submodules https://github.com/paulsmir/windows-on-m1.git
cd windows-on-m1
git submodule status --recursive
```

Build `dist/j313/boot.bin` by following [BUILD.md](BUILD.md), or place the matching release
artifact there and verify its published SHA-256 checksum.

## 3. Identify the Asahi ESP and install the standalone image

List disks from macOS:

```sh
diskutil list
```

Identify the small Asahi ESP containing `m1n1/boot.bin`. Do not assume it is `disk0s4`; the
identifier depends on the current GPT layout. Substitute the reviewed identifier below:

```sh
sudo scripts/install-esp.sh inspect --disk diskXsY
sudo scripts/install-esp.sh install --disk diskXsY --image dist/j313/boot.bin
```

`install` validates the embedded standalone manifest, creates the original backup once at
`/var/backups/m1n1-windows/diskXsY.boot.bin.original`, writes through a temporary sibling,
renames atomically, synchronizes, and verifies SHA-256.

The original developer script supplied during bring-up is not used here: it hard-coded one
machine's partition and home-directory paths. The public script requires an explicit target.

Rollback from macOS is:

```sh
sudo scripts/install-esp.sh restore --disk diskXsY
```

## 4. Prepare the Windows ARM64 USB device

On macOS, use Disk Utility to erase the USB device as GUID Partition Map plus exFAT. Mount a
Windows 11 ARM64 ISO and copy every file and directory from the mounted ISO to the USB root.
Do not copy the ISO file itself.

The development installation used a complete extracted ISO. Its `install.wim` was larger
than FAT32's single-file limit, which is why exFAT was used. The J313 Mu build used here can
read that exFAT device.

Verify at least these paths:

```text
<USB>/EFI/BOOT/BOOTAA64.EFI
<USB>/sources/boot.wim
<USB>/sources/install.wim
```

## 5. Start the USB WinPE environment

Connect the USB device through the guest-visible USB-C port or hub and boot the Asahi UEFI
entry. If Mu does not select the USB loader automatically, use its shell:

```text
map -r
map
```

Inspect candidate filesystems instead of assuming an alias:

```text
FS0:
dir
FS1:
dir
```

Continue until the filesystem containing `EFI`, `sources`, and the Windows media files is
found, then run:

```text
FSn:\EFI\BOOT\BOOTAA64.EFI
```

`FSn:` is deliberately symbolic. On the development machine aliases changed between boots;
the USB or Windows ESP was sometimes `FS3:`, but that value is not stable.

When graphical Windows Setup appears, press `Shift+F10`. Some compact keyboards require
`Fn+Shift+F10`. The remaining installation is performed in Command Prompt.

## 6. Identify disks and existing partitions

Start DiskPart:

```cmd
diskpart
list disk
list volume
select disk 0
detail disk
list partition
```

Do not continue merely because the internal SSD happens to be Disk 0. Confirm its physical
size and existing partition layout. On the validated 256 GB machine WinPE reported roughly
233 GB total, about 111 GB free, and the USB as a separate roughly 14 GB disk.

Existing Apple/Asahi partitions appeared as `Unknown`; this is not permission to alter them.
Continue only when the unallocated extent matches the Windows space created by the Asahi
installer.

## 7. Create the Windows partitions in the free extent

The following commands assume the reviewed internal SSD is currently selected and its
unallocated extent is large enough. They create a 16 MiB Microsoft Reserved partition, a
512 MiB Windows EFI System Partition, and an NTFS partition using the remainder of that
extent:

```cmd
create partition msr size=16

create partition efi size=512
format quick fs=fat32 label=WINESP
assign letter=S

create partition primary
format quick fs=ntfs label=Windows
assign letter=W

list partition
list volume
exit
```

The MSR is GPT metadata space reserved for Windows disk-management operations. It has no
filesystem and normally does not appear in `list volume`; that absence does not mean its
creation failed. `WINESP` and `Windows` are human-readable labels only. Drive letters `S:`
and `W:` are temporary WinPE assignments used by the following commands.

If the target layout already contains partitions created by an earlier attempt, do not run
the creation block again. Identify and assign the existing Windows ESP and NTFS volume:

```cmd
diskpart
list volume
select volume <windows-volume-number>
assign letter=W
select volume <winesp-volume-number>
assign letter=S
exit
```

## 8. Locate the WIM and choose the Windows edition

Drive letters can change after every boot. Locate the USB source by inspecting likely
volumes with `dir`, then verify the file explicitly. For example:

```cmd
dir C:\sources\install.wim
```

If the USB is not `C:`, try its observed drive letter. Enumerate editions:

```cmd
dism /Get-WimInfo /WimFile:C:\sources\install.wim
```

Choose the index whose `Name` matches the desired Windows edition. Index 3 happened to
select the intended edition in one ISO, but indexes are ISO-specific and must not be copied
from another installation.

## 9. Apply Windows to the NTFS partition

Replace `N` with the index obtained above and replace `C:` if the USB has another letter:

```cmd
dism /Apply-Image /ImageFile:C:\sources\install.wim /Index:N /ApplyDir:W:\
```

This can take a long time with the experimental synchronous NVMe bridge. If the machine
restarts near completion, boot USB WinPE again, reassign `W:` and `S:`, and inspect
`W:\Windows` before repeating a multi-gigabyte apply. On the validated installation the
machine restarted after DISM displayed 97 percent, but the applied Windows tree survived.

Verify the target:

```cmd
dir W:\Windows\System32\winload.efi
```

## 10. Create BCD and the architecture fallback loader

Create Windows boot files on the new Windows ESP:

```cmd
bcdboot W:\Windows /s S: /f UEFI /v
mkdir S:\EFI\BOOT
copy /y S:\EFI\Microsoft\Boot\bootmgfw.efi S:\EFI\BOOT\BOOTAA64.EFI
```

The verbose `bcdboot` command may print retries for optional localized resources. The
required success condition is its final `Boot files successfully created.` message plus the
following explicit checks:

```cmd
dir S:\EFI\BOOT\BOOTAA64.EFI
dir S:\EFI\Microsoft\Boot\BCD
bcdedit /store S:\EFI\Microsoft\Boot\BCD /enum {bootmgr}
```

The fallback path is important because Mu's installed-system policy searches internal block
devices for `\EFI\BOOT\BOOTAA64.EFI` and does not depend on persistent UEFI `BootOrder`.

## 11. Boot the installed Windows system

Remove the Windows USB device and reboot the Air. The intended standalone chain is:

```text
iBoot -> Asahi boot entry -> m1n1 boot.bin -> embedded Mu
      -> internal \EFI\BOOT\BOOTAA64.EFI -> Windows
```

For manual recovery from the Mu shell, use `map -r`, inspect every candidate filesystem,
and launch the one whose `EFI\Microsoft\Boot\BCD` and `EFI\BOOT\BOOTAA64.EFI` match the
Windows ESP:

```text
map -r
FSn:
dir EFI\Microsoft\Boot\BCD
dir EFI\BOOT\BOOTAA64.EFI
EFI\BOOT\BOOTAA64.EFI
```

## 12. Complete OOBE with a local account

The validated installation could not complete the online Microsoft-account path reliably.
At the OOBE screen, press `Shift+F10` or `Fn+Shift+F10` and run:

```cmd
start ms-cxh:localonly
```

This opens the legacy local-account creation UI. Create the local user and continue through
the privacy pages. The screen may remain unchanged for several minutes while storage and
USB are slow; distinguish a slow OOBE transition from a real guest hang using the assisted
telemetry described in [DEBUGGING.md](DEBUGGING.md).

`OOBE\BYPASSNRO` exists in some Windows builds but is not the validated path here and may be
removed or disabled in newer media. Hardware-requirement registry changes used to enter
Setup are a separate mechanism and do not create a local account.

## Recovery

If the packed image does not start, hold the Mac power button to enter Apple startup options
and boot macOS. Restore the original Asahi second stage:

```sh
cd windows-on-m1
sudo scripts/install-esp.sh restore --disk diskXsY
```

If Windows was interrupted by a hard power-off and enters Recovery, allow Recovery to finish
or use the assisted KD reboot procedure rather than repeatedly removing power. Restoring
`boot.bin` does not delete Windows partitions; it only restores the original Asahi payload.

## Validation status

The manual WinPE, DISM, BCDBoot, fallback-loader, and local-account procedure above was
validated on the development J313. The current self-contained standalone image is built and
host-tested, but its final cold-boot hardware validation is pending.
