# ANS010 — native boot of the installed Windows (C:) through AppleAns (2026-11-07)

Status: implemented offline. Mu a33217e38, root 1e7a690c4 (driver) + 264dcfef0 (contract), driver
ANSBUILD005-W, firmware ANSBUILD021-I1, C: staged (ANS010-PREP). ANS010-R1 is preregistered in
EXPERIMENTS.md.

## Sources inspected

- Live machine state:
  - recovery `Get-Partition` of disk 0 (4096-byte sectors) and the BCD;
  - hibernation, Fast Startup and crash-dump settings;
  - driver store and DriverDatabase;
  - recovery firmware log EXP2225: `PciRoot(0x0)/Pci(0x0,0x0)/NVMe(0x1,..)/HD(5,GPT,F05B9952-..,0x1D5AF00,0x20000)/\EFI\BOOT\BOOTAA64.EFI`;
  - native firmware log ANS009-R4: PCI BAR programmed, but no NVMe BlockIo before ExitBootServices;
  - WinPE ANS009 inventory: no PCI devices after the handoff; the disk is a child of ACPI\APPL0003, which was configured without setupapi (in the kernel).
- m1n1:
  - `hv_nvme.c` handoff adapters: freeze → drained → bounded flush → seal; designed for a live vNVMe frontend;
  - `hv_ans_service.c`: the write window.
- Mu:
  - WindowsAutoBootDxe;
  - AppleAnsBootSource.h;
  - AppleAnsExitBootServicesGateLib (StartImage + EBS gates);
  - DSC/FDF/dsc.inc;
  - BUILD_PLAN.md.
- Windows/WDK:
  - storport.h `PORT_CONFIGURATION_INFORMATION.DumpMode` (`DUMP_MODE_*`, pinned WDK 26100);
  - INF `StartType=0`/`LoadOrderGroup` semantics;
  - PnP driver store / DriverDatabase staging (`pnputil /add-driver`).

## Observed contract

| Phase | Owner | Storage path |
| --- | --- | --- |
| Mu DXE → bootmgr → winload, before ExitBootServices | Mu NvmExpressDxe on the m1n1 vNVMe bridge (synchronous) | the bridge also carries bootmgr's ESP writes |
| ExitBootServices | Mu gate → EL2 handoff (freeze/drain/flush/seal the vNVMe, release ANS to the broker) | — |
| Kernel | AppleAns (boot-start, SCSI Miniport) → broker | READ, FLUSH and WRITE inside the EL2 window |

- winload loads services with Start=0. The kernel configures APPL0003 from DriverDatabase (`oem6.inf`).
- partmgr finds C: by its GPT partition GUID. The disk is the same one the bridge exposed, so the drive letters stay.

## Layer ownership and changes

- Mu (new `J313_ANS_INSTALLED_BOOT` profile):
  - At ReadyToBoot, recursively connects the PCI controllers. In native profiles BDS never reaches its ConnectAll fallback.
  - The StartImage gate admits only the pinned WINESP HD node (GUID, start, size) with BOOTAA64 or BOOTMGFW.
  - The EBS handoff is unchanged. ACPI is byte-identical to N1.
- EL2 (no change): window = WINESP+MSR+C:+ANSTEST, LBA 30781184 + 29187328 blocks (`config/j313-ans-installed-boot.json`).
  - Excluded: iBoot, the macOS/APFS containers, the second ESP (p4), Apple recovery.
  - Lesson from ANS009 R1–R3: every Windows-mounted volume must be inside the window.
- Driver: DumpMode≠0 refuses in HwFindAdapter, so a crash dump never re-claims the broker.
- C: (recovery guest):
  - package staged as oem6.inf;
  - test certificate added to Root and TrustedPublisher;
  - service AppleAns with Start=3. It is switched to 0 only for a native boot, and back to 3 from the native guest before its reboot.

## Differences

- Asahi/m1n1 own ANS directly. Here EL2 brokers every queue entry and enforces the LBA window.
- The recovery Mu boots the first matching ESP after the BDS ConnectAll fallback. The installed native Mu boots exactly one pinned ESP from ReadyToBoot.
- Windows expects one boot controller. It moves from stornvme (PCI vNVMe) to AppleAns (ACPI) between boot modes. The same C: install serves both, and the AppleAns service is present only as Start=3 in recovery.

## Smallest falsifiable checkpoint (ANS010-R1)

- **Pass:**
  - native Windows answers SSH;
  - APPL0003 is OK with service AppleAns;
  - the boot disk's parent is APPL0003, and no PCI device is present;
  - the 64 MiB probe is written and read back natively, and has the same hash over the bridge afterwards;
  - 0 EL2 rejects and 0 stuck tags;
  - a clean reboot;
  - recovery: chkdsk /scan clean, C: not dirty, APPL0002 gate unchanged.
- **Fail-safe outcomes:**
  - INACCESSIBLE_BOOT_DEVICE: no C: writes have happened yet;
  - hang: the runner stops; NTFS journal recovery runs on the recovery boot.
- **Recovery:** the unchanged `launch-normal2.sh` recovery profile.
  - AppleAns Start=3 makes recovery boots independent of the driver.
  - Rollback: `pnputil /delete-driver oem6.inf /force`, then delete the service key.
