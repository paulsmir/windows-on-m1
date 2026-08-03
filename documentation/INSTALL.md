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
external backup. Connect the MacBook to power and make sure macOS has a working internet
connection. Record the starting layout before downloading the installer:

```sh
diskutil list
diskutil apfs list
```

Save this output with the backup notes; it is the reference used later to distinguish Apple,
Asahi, and free-space extents. The Asahi installer is an online bootstrap rather than a DMG
or an application download. Open Terminal in macOS and run the command published by the
[official Asahi m1n1 guide](https://asahilinux.org/docs/sw/tethered-boot/):

```sh
curl https://alx.sh | sh
```

This command downloads the current installer and starts it immediately. Read its warnings
and press Return only after confirming that the displayed machine and system disk are the
expected ones. Do not enable Expert Mode; the normal UEFI-only profile provides everything
this project needs.

### Stage A: resize APFS and create the UEFI-only installation from macOS

The online catalog and its numeric indexes can change. Match the labels below instead of
copying an option number from this guide.

1. At `Choose what to do`, select `Resize an existing partition to make space for a new OS`.
2. Select the existing macOS APFS container that contains `Macintosh HD`. Do not select the
   system Recovery container, an EFI partition, or an external disk.
3. The installer shows the allowed size range. Enter the new size to retain for macOS. The
   amount released must cover both the UEFI-only environment and the desired Windows
   capacity. In other words:

   ```text
   released space = UEFI-only minimum shown by Asahi + desired Windows size
   ```

4. Review the old size, new macOS size, and resulting free space, then confirm the resize.
   Wait for it to finish. Do not interrupt the Mac and do not run a second resize command.
5. When the installer returns to `Choose what to do`, select `Install an OS into free space`.
6. At `Choose an OS to install`, select
   `UEFI environment only (m1n1 + U-Boot + ESP)`. Do not select Fedora, a desktop image, a
   minimal Linux image, or tethered/proxy-only developer mode.
7. Give the new entry a recognizable name such as `Windows`. This is the name to look for in
   Apple Startup Options later.
8. When the UEFI-only profile offers to reserve space for the future OS, leave the intended
   Windows capacity unpartitioned. Allocate only the boot environment requested by the
   installer. The official platform documentation explicitly describes this mode as creating
   a UEFI environment while leaving unpartitioned space for another OS installer:
   [Open OS Platform Interoperability](https://asahilinux.org/docs/platform/open-os-interop/).
9. Review the final partition summary. It must preserve macOS and Recovery, create the small
   Asahi stub/boot container and ESP, and leave the Windows extent free. Confirm only when
   that summary matches the intended layout.

The Asahi installer performs the APFS shrink; this is the step that reduces the macOS
allocation and makes room for the UEFI boot environment and Windows. Do not shrink the APFS
container manually before or after this step.

Do not create an exFAT Windows target in macOS because WinPE will create the Windows GPT
partitions later.

### Stage B: finish the installation in paired 1TR recovery

Partitioning is only the first stage. m1n1 stage 1 and the Apple boot policy are installed
from One True Recovery (1TR); merely running the Terminal command and returning to macOS
does not create a complete bootable entry.

1. Follow the installer's final instructions and fully shut down the Mac. Do not choose a
   normal restart. Wait about 15 seconds after the display and keyboard backlight turn off.
2. Press and keep holding the physical power/Touch ID button. Release it only after
   `Loading startup options` appears.
3. In Startup Options, select the newly created volume named `Windows` (or the name entered
   in Stage A). Do not select `Macintosh HD`, and do not select the generic `Options` item for
   this step.
4. The selected volume enters its paired recovery environment and launches
   `Finish Installation`. Authenticate with the requested macOS machine-owner credentials.
   This must be an administrator who is registered as an owner of the Mac.
5. Follow every `Finish Installation` prompt. Allow it to create the reduced/permissive boot
   policy and install m1n1 stage 1. Do not close the window, power off, or return to Startup
   Options while it is writing the policy.
6. Let the finishing stage reboot or shut down the machine when it says it is complete.
   If it returns to macOS instead of entering UEFI, shut down, hold the power button to open
   Startup Options again, and select the new `Windows` volume. Do not repeat Stage A.

Entering 1TR by holding the physical power button and authenticating is an Apple security
requirement for changing a boot policy; an ordinary Recovery boot is not equivalent. See
[Apple Silicon Platform Security](https://asahilinux.org/docs/platform/security/).

### First stock UEFI boot and return to macOS

The first successful boot must use the stock Asahi UEFI environment before this project's
stage 2 is installed. The UEFI-only profile contains m1n1, U-Boot, and an ESP. Without a
bootable USB device it may stop at a U-Boot/UEFI menu or shell; seeing that output confirms
that the Apple-authorized stage 1 and boot policy work.

To return to macOS after this check:

1. Shut down the Mac completely.
2. Hold the power button until `Loading startup options` appears.
3. Select `Macintosh HD` and let macOS boot normally.
4. Run `diskutil list` and verify that the small Asahi boot container/ESP exists and that the
   intended Windows extent is still unallocated.

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

The `inspect` command must show the existing stock Asahi `m1n1/boot.bin`. If that path is
absent, the selected partition is not the expected ESP; stop and inspect the layout again.

This replacement does not modify the Apple-authorized stage 1 or repeat the 1TR boot-policy
operation. Stage 1 remains in the Asahi stub environment. The script replaces only stage 2
at `<ESP>/m1n1/boot.bin`, which is why the change is reversible from macOS.

`install` validates the embedded standalone manifest, creates the original backup once at
`/var/backups/m1n1-windows/diskXsY.boot.bin.original`, writes through a temporary sibling,
renames atomically, synchronizes, and verifies SHA-256.

The original developer script supplied during bring-up is not used here: it hard-coded one
machine's partition and home-directory paths. The public script requires an explicit target.

Rollback from macOS is:

```sh
sudo scripts/install-esp.sh restore --disk diskXsY
```

### First boot with this project's stage 2

1. Shut down the Air. For a standalone test, disconnect the development host USB cable. For
   Windows installation, connect only the prepared Windows ARM64 USB device and the required
   keyboard/mouse hub.
2. Hold the power button until `Loading startup options` appears and select the `Windows`
   UEFI entry created by Asahi. If that entry is already the default, subsequent ordinary
   power-on boots can enter it automatically.
3. The project `boot.bin` opens a short debug-host maintenance window. With no host attached,
   it automatically starts the embedded Mu firmware. Mu then boots the Windows USB installer
   or, after Windows has been installed, the internal `\\EFI\\BOOT\\BOOTAA64.EFI`.
4. If the project image does not start, return to `Macintosh HD` through Startup Options and
   run the rollback command above. Do not repartition the disk to recover stage 2.

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
