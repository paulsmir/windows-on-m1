# AppleAns: Windows classifies the internal SSD as a hard disk

Status 2026-11-09T23:15Z: cause confirmed offline and on the live guest; fix S1
implemented and host-tested; no hardware run yet.

## Observation (live J313 guest, read-only query, 2026-11-09T23:1xZ)

`Get-PhysicalDisk` for `NVMe APPLE SSD AP0256`: `MediaType = Unspecified`,
`BusType = Fibre Channel`, `SpindleSpeed = 0`. `fsutil behavior query
DisableDeleteNotify` = 0 (the file system would send TRIM). The user sees the
disk as HDD in the Windows UI.

## Sources inspected

- `drivers/storage/appleans/common/ans_scsi.c` (INQUIRY planner),
  `common/tests/test_common.c`, `windows/native.c` (metadata completion),
  `windows/driver.c`, `windows/AppleAns.inf`.
- Microsoft classpnp sample (`Windows-driver-samples`,
  `storage/class/classpnp/src/utils.c`, `classp.h`):
  `ClasspDeviceSeekPenaltyProperty`,
  `ClasspDeviceGetBlockDeviceCharacteristicsVPDPage`, `ClasspSupportsUnmap`,
  `ClasspDeviceGetLBProvisioningVPDPage`.
- Microsoft Learn, "Registry entries for Storport miniport drivers"
  (`Parameters\BusType`).
- Pinned WDK 26100 `storport.h`: `VPD_BLOCK_DEVICE_CHARACTERISTICS_PAGE`
  (PageLength 0x3C), `VPD_LOGICAL_BLOCK_PROVISIONING_PAGE`, `STORAGE_BUS_TYPE`
  (`BusTypeFibre` = 6, `BusTypeNvme` = 0x11).

## WINDOWS CONTRACT

- Seek penalty: when the lower layer does not answer
  `StorageDeviceSeekPenaltyProperty` itself, classpnp reads INQUIRY VPD page
  B1h, but only if page 00h lists it. MEDIUM ROTATION RATE 0001h means no seek
  penalty; 0401h..FFFEh means seek penalty; anything else, a page length other
  than 003Ch, or a missing page fails the query. A failed query is what the UI
  shows as a hard disk.
- TRIM: `ClasspSupportsUnmap` is the LBPU bit of VPD page B2h, read only when
  page 00h lists B2h. No page, no TRIM to the device.
- Bus type: the Storport `Parameters\BusType` value defaults to 6
  (`BusTypeFibre`) when the INF does not set it.

## ANS CONTRACT

Apple ANS is a NAND controller; the namespace has no rotating medium. The
AppleAns planner advertised VPD page 00h only and rejects UNMAP (0x42) with
ILLEGAL REQUEST/20h; no NVMe Dataset Management translation exists.

## TRANSLATION

| Step | Change | Owner | State |
| --- | --- | --- | --- |
| S1 | VPD 00h lists {00h, B1h}; VPD B1h = 64 bytes, PAGE LENGTH 003Ch, MEDIUM ROTATION RATE 0001h, all other fields "not reported" | `ans_scsi.c` | implemented, host RED/GREEN |
| S2 | INF service `Parameters\BusType` = 0x11 (`BusTypeNvme`) | `AppleAns.inf` | not implemented; separate variable |
| S3 | UNMAP -> NVMe Dataset Management (deallocate), then VPD B0h + B2h (LBPU) | `ans_scsi.c` + wrappers | designed and implemented offline 2026-11-09, gated on Identify ONCS: `ANS-TRIM-UNMAP.md` |

S1 advertises a fact about the medium, not a command capability. B0h/B2h stay
absent until S3 exists: a capability is advertised only when implemented.

## WHAT IS STILL UNKNOWN

- Whether `MediaType` in `Get-PhysicalDisk` becomes `SSD` from the seek-penalty
  property alone or also depends on the bus type (the derivation is not
  documented by Microsoft). S1 answers it.
- Which queries Windows adds for a `BusTypeNvme` adapter that is not stornvme
  (protocol-specific properties, health, firmware). Must be read from
  Microsoft sources before S2 touches the boot disk.
- Whether ANS accepts Dataset Management deallocate, its range limits and its
  latency (ONCS from the live Identify, the Linux Apple NVMe driver's quirks).
  Required before S3.

## Hardware checkpoint S1 (one variable: the AppleAns package with S1)

Expected after one reboot on the new package: `Get-PhysicalDisk` reports
`MediaType = SSD` for the Apple disk; Optimize Drives lists the volume as
"Solid state drive"; boot, SSH, storage I/O and the m1n1 `BUGCHECK ok code=0`
line are unchanged. Failure criteria: `MediaType` stays `Unspecified` (then the
bus type participates: S2 becomes the next single variable), any storage error
in the System log, or a boot failure.

Recovery: AppleAns is the boot-disk driver. Keep the current signed package and
the bridge-boot emergency profile available before installing; a failed boot is
rolled back from the bridge profile by restoring the previous package.

Note for whoever runs it: without page B2h classpnp reports no UNMAP support,
so no UNMAP reaches the driver before or after S1. What Optimize Drives offers
for an SSD-classified volume without TRIM is not verified; record what it shows.
