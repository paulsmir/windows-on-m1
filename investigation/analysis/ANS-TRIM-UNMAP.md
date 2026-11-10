# AppleAns: TRIM (SCSI UNMAP -> NVMe Dataset Management deallocate)

Status 2026-11-09T23:28Z: designed and implemented offline with host RED/GREEN
tests. Not built with MSVC, not installed, no hardware run. This is step S3 of
`ANS-SSD-MEDIA-TYPE.md`.

Whether the J313 ANS controller reports Dataset Management in Identify ONCS is
**not known** (no recorded Identify data, and no NVMe command was sent to the
live disk for this note). The implementation therefore advertises nothing
unless the controller's own Identify data says so at start.

## Sources inspected

- `drivers/storage/appleans/common/ans_scsi.c`, `ans_scsi.h`, `ans_order.c`,
  `ans_direct.c` (`ansd_submit`, TCB fill); `windows/identify.c`, `request.c`,
  `native.c` (`AppleAnsNativeStart`, `AppleAnsDispatch`), `direct.c`,
  `driver.c` (`AppleAnsBuildIo`, `MapBuffers`), `telemetry.c`.
- Linux v6.12 `drivers/nvme/host/apple.c` (`apple_nvme_submit_cmd`,
  `apple_nvme_probe`: quirks `NVME_QUIRK_SKIP_CID_GEN | NVME_QUIRK_IDENTIFY_CNS`
  only), `drivers/nvme/host/core.c` (`nvme_config_discard`,
  `nvme_setup_discard`, `nvme_init_identify`), `include/linux/nvme.h`
  (`nvme_dsm_range`, `NVME_DSM_MAX_RANGES`, `NVME_DSMGMT_AD`,
  `NVME_CTRL_ONCS_DSM`), `drivers/nvme/host/nvme.h` (quirk list). Read for
  behaviour only; nothing copied.
- Microsoft classpnp sample (`Windows-driver-samples`,
  `storage/class/classpnp/src/utils.c`, `class.c`, `classp.h`):
  `ClasspDeviceTrimProperty`, `ClasspSupportsUnmap`, `ClasspIsThinProvisioned`,
  `ClasspDeviceGetLBProvisioningVPDPage`, `ClasspDeviceGetBlockLimitsVPDPage`,
  `DeviceProcessDsmTrimRequest`, `ConvertDataSetRangeToUnmapBlockDescr`,
  `InterpretReadCapacity16Data`.
- Pinned WDK 26100 `storport.h`: `VPD_BLOCK_LIMITS_PAGE`,
  `VPD_LOGICAL_BLOCK_PROVISIONING_PAGE`, `UNMAP_LIST_HEADER`,
  `UNMAP_BLOCK_DESCRIPTOR`, `CDB.UNMAP`, `PROVISIONING_TYPE_*`.
- NVM Express SCSI Translation Reference 1.5 (SNT), 5.6 UNMAP, 6.1.6 Block
  Limits VPD, 6.1.8 Logical Block Provisioning VPD, READ CAPACITY(16) notes.
- Host macOS (`system_profiler SPNVMeDataType` on the Mac16,7 development
  host, a different machine and controller generation): `APPLE SSD AP0512Z`,
  `TRIM Support: Yes`. Corroboration that Apple's own stack trims ANS storage,
  not evidence about the J313 controller.

## WINDOWS CONTRACT

- `StorageDeviceTrimProperty` (what `fsutil fsinfo sectorinfo` prints as "Trim
  Supported"): when the port driver does not answer, classpnp returns
  `TrimEnabled = LBPU` from INQUIRY VPD page B2h. The page is read only if page
  00h lists it; it must be at least 8 bytes with PAGE LENGTH >= 4.
- Block limits come from VPD page B0h, read only if page 00h lists it; classpnp
  needs at least 36 bytes and PAGE LENGTH >= 32. Without the page and with
  LBPU set, classpnp assumes no limit on LBA count and descriptor count.
- Thin provisioning (threshold notifications, slab logic) applies only to
  PROVISIONING TYPE 2 with LBPU. Types 0 and 1 are treated alike. READ
  CAPACITY(16) LBPME/LBPRZ are recorded but not used; LBPRZ is taken from B2h.
- TRIM send path (`DeviceProcessDsmTrimRequest`): one UNMAP(10) per buffer,
  CDB 42h, ANCHOR 0, GROUP NUMBER 0, PARAMETER LIST LENGTH = 8 + 16 x n, SRB
  data-out, `SRB_SIMPLE_TAG_REQUEST`, the disk's ordinary timeout. The header
  carries UNMAP DATA LENGTH = list length - 2 and BLOCK DESCRIPTOR DATA LENGTH
  = 16 x n. n is bounded by MAXIMUM UNMAP BLOCK DESCRIPTOR COUNT (and by the
  adapter's maximum transfer length); the sum of the LBA counts in one command
  is bounded by MAXIMUM UNMAP LBA COUNT; ranges are split to that sum. Unmap
  granularity is not used for splitting (the parameter is unreferenced).
- The miniport sets `MapBuffers = STOR_MAP_NON_READ_WRITE_BUFFERS`, so the
  UNMAP parameter list arrives mapped in `Srb->DataBuffer`, like the INQUIRY
  response buffers the driver already writes.

## ANS/ASAHI CONTRACT

- Linux drives ANS through the generic NVMe core. The Apple driver sets no
  discard-related quirk: discard is enabled exactly when Identify Controller
  ONCS bit 2 (Dataset Management) is set. It does not set
  `NVME_QUIRK_DEALLOCATE_ZEROES`, so Linux does not assume that deallocated
  blocks read as zeros.
- Limits Linux applies: at most 256 ranges per command, discard granularity
  one logical block, per-request size up to 2^32-1 512-byte sectors. DMRL/DMRSL
  are not available (`NVME_QUIRK_IDENTIFY_CNS`: ANS does not implement the
  newer Identify CNS values).
- Command: opcode 09h, CDW10 = number of ranges - 1, CDW11 = 4 (AD), PRP1 = the
  range list; each range is 16 bytes little-endian: context attributes (0),
  length in blocks, starting LBA.
- Linux always hands the controller a full 256-entry (4 KiB) zeroed buffer
  "because some devices do not consider the number of ranges when determining
  how much data to DMA". AppleAns does the same: the list lives in one whole
  zeroed 4 KiB page.
- ANS TCB for the command (Linux `apple_nvme_submit_cmd`): DMA flag
  TO_DEVICE because opcode bit 0 is set, PRP1/PRP2 copied, `length` taken from
  the CDW12 position, which is 0 for Dataset Management. `ansd_submit` already
  produces exactly this for an SQE with PRP1 and a TO_DEVICE direction.
- No known ANS-specific latency or firmware problem with deallocate was found
  in the Linux driver. Absence of a quirk is not a measurement.

## TRANSLATION

Ownership:

| Layer | Owns |
| --- | --- |
| `windows/identify.c` | `ns.deallocate` = ONCS bit 2, read from Identify Controller bytes 520..521 |
| `windows/direct.c` | Transport policy: direct mode keeps the Identify value; logs diag 0xA8 (value = ONCS) once per start when the bit is clear |
| `windows/native.c` init (broker) | Forces `ns.deallocate = 0`: the EL2 broker authorizes FLUSH/READ/WRITE only |
| `common/ans_scsi.c` `ans_scsi_prepare` | CDB validation and the VPD pages. Still never touches payload |
| `common/ans_scsi.c` `ans_scsi_unmap_resolve` | The only code that reads a data-out buffer: validates the parameter list and writes the NVMe range list |
| `windows/native.c` `AppleAnsNativeStart` | SRB shape (data-out, mapped buffer, lengths), reserves the tag, calls the resolver with the tag's private page, then the ordinary ordered path |
| `windows/request.c` `ans_io_request` | Builds the SQE; rejects an unresolved plan, a non-writable build, a missing page |
| Dump instance | Namespace is rebuilt without Identify: `deallocate` stays 0 |

Decision on the conversion: a planner helper working on the caller's mapped
buffer, not wrapper code. The conversion is a deterministic contract (lengths,
ranges, endianness) and belongs with the host-tested planner; the wrapper only
supplies pointers it owns. No intermediate copy is made: the helper reads each
field of the list exactly once and writes into a page no command owns, so a
buffer that changes underneath (pass-through from user mode) cannot pass
validation with one value and be submitted with another.

Sequence for one UNMAP SRB:

1. `ans_scsi_prepare`: opcode 42h is unknown (5/20h) unless `ns.deallocate`.
   Otherwise: 10-byte CDB, ANCHOR/GROUP NUMBER/CONTROL zero (5/24h), buffer
   length equal to PARAMETER LIST LENGTH (5/24h), namespace ready (2/04h).
   Length 0: GOOD, nothing done. Length 1..7: 5/1Ah. Length above
   8 + 256 x 16: 5/24h. Read-only namespace: 7/27h. Result: SUBMIT, opcode 09h,
   data-out, `transfer_bytes` = list length.
2. `AppleAnsNativeStart` picks a free tag and calls `ans_scsi_unmap_resolve`
   with `Srb->DataBuffer` and `Pages[tag]` (the tag's PRP-list page, unused by
   a command without SG payload).
3. The resolver zeroes the page, then checks: descriptor bytes within the list
   (5/1Ah), a multiple of 16, UNMAP DATA LENGTH = descriptor bytes + 6, reserved
   bytes zero (5/26h); per descriptor LBA <= capacity and LBA + count <=
   capacity (5/21h); sum of counts <= FFFFFFFFh (5/26h). Zero-count descriptors
   are skipped. Any error fails the whole command before anything is sent and
   leaves the page zero. No blocks named: COMPLETE (GOOD, no command).
4. `ans_io_request` builds one Dataset Management command: CDW10 = ranges - 1,
   CDW11 = 4, PRP1 = the page, 4096 bytes TO_DEVICE.
5. Completion is the ordinary CQE path; NVMe errors map through
   `ans_scsi_nvme_sense` (Invalid Opcode -> 5/20h).

VPD pages, only with `ns.deallocate`:

- 00h lists 00h, B0h, B1h, B2h.
- B0h, 64 bytes, PAGE LENGTH 003Ch: MAXIMUM UNMAP LBA COUNT FFFFFFFFh, MAXIMUM
  UNMAP BLOCK DESCRIPTOR COUNT 0100h (SNT value, the NVMe range limit), OPTIMAL
  UNMAP GRANULARITY 1, UGAVALID 0. Every other field 0 (not reported). These are
  exactly the limits step 3 enforces.
- B2h, 8 bytes, PAGE LENGTH 0004h: LBPU = 1. LBPWS, LBPWS10, ANC_SUP, DP,
  LBPRZ, threshold exponent and PROVISIONING TYPE are 0. READ CAPACITY(16)
  LBPME/LBPRZ stay 0.

Deviation from SNT 1.5, deliberate: SNT asks for PROVISIONING TYPE 1 (resource
provisioned) when deallocate is supported. Nothing about ANS provisioning is
proven, classpnp treats 0 and 1 alike, so the field stays "not reported".

Ordering, flush, shutdown, reset (`ans_order.c`): UNMAP is one ordinary task.
It is admitted with the SRB's own queue attribute (classpnp sends SIMPLE) and
occupies one tag from admission to CQE. An ORDERED task (the shutdown flush)
still waits for it and holds back later ones; SYNCHRONIZE CACHE keeps mapping to
a full-namespace Flush. No extra fence is added: SIMPLE tasks are unordered
with respect to each other by definition, and the file system does not trim
clusters with writes in flight. Parse errors and the empty case go through the
same admission and are completed by `AppleAnsDispatch`, so sense reporting keeps
its order. A deallocate that never completes ends like any other command:
Storport timeout, `AppleAnsDirectReset`.

Read-only mode is checked three times, independently: planner (7/27h),
resolver (7/27h), `ans_io_request` (`writable`), plus the wrapper's opcode
allow-list (direct transport and writable only).

## WHAT IS STILL UNKNOWN

- ONCS on the J313 ANS controller. If bit 2 is clear, behaviour is unchanged and
  the System log gets one AppleAns record with diag code 0xA8.
- Whether ANS accepts the command as built (Linux uses the same shape, but on
  its own TCB/queue code), and how long a large deallocate takes compared with
  the disk timeout. One command may name up to 256 ranges and 2^32-1 blocks.
- Whether deallocated blocks read back as zeros (LBPRZ stays 0; nothing depends
  on it).
- Whether Storport answers `StorageDeviceTrimProperty` itself for this adapter
  before classpnp falls back to the VPD page (the seek-penalty checkpoint S1
  answers the same question for B1h).
- The SBC-3 sense codes for malformed parameter lists (1Ah for a short list,
  26h for bad contents) are standard additional sense codes chosen from memory
  of SBC-3; the T10 text was not available. classpnp never sends such lists.
- MSVC `/W4 /WX`: the host build is clang only.

## Hardware checkpoint S3 (specified, not run)

WHY THIS HYPOTHESIS: (1) Linux enables discard on ANS from ONCS alone, with no
Apple quirk; (2) classpnp sends UNMAP only on LBPU from page B2h, which this
package now reports when ONCS allows; (3) the command reaches the controller
through the already validated write path (same tag, TCB and PRP rules).

Precondition: checkpoint S1 (`ANS-SSD-MEDIA-TYPE.md`) has a verdict and its
package is the installed baseline. Single variable: the AppleAns package built
from this commit, direct (ANS-DIRECT) transport, nothing else changed.

Steps and expected observations, after one reboot on the new package:

1. `fsutil fsinfo sectorinfo C:` prints "Trim Supported" (baseline: "Trim Not
   Supported"). Boot, SSH, storage I/O and the m1n1 `BUGCHECK ok code=0` line
   are unchanged.
2. AppleAns telemetry before and after step 3: `submitted - reads - writes -
   flushes` (non-read/write/flush commands) is recorded.
3. `Optimize-Volume -DriveLetter C -ReTrim -Verbose` completes without error.
   The telemetry difference from step 2 grows by the number of Dataset
   Management commands (at least 1); `completed` follows `submitted`; no
   timeouts, resets or invalid CQEs.
4. `chkdsk C: /scan` reports no problems; a file written before the retrim
   reads back with the same SHA-256.

Failure criteria: "Trim Not Supported" with no 0xA8 record (Windows did not use
the pages: stop, read the property path); an AppleAns System-log record 0xA4
with opcode 09h (controller rejected the command: value gives the NVMe status);
a 0xA5 reset record, a Storport timeout (event 129) or a `disk` event 153
during the retrim (latency or a lost completion); any file-system error or hash
mismatch; a boot failure. A 0xA8 record with "Trim Not Supported" is not a
failure of this change: ANS does not report Dataset Management, and S3 ends
there.

Recovery: AppleAns is the boot-disk driver. Keep the S1 baseline package and
the bridge-boot emergency profile ready before installing. A failed boot or any
failure criterion is rolled back from the bridge profile by restoring the
baseline package; if the guest still boots, `fsutil behavior set
DisableDeleteNotify 1` stops the file system from sending TRIM until the
package is replaced.

## Independent review and qualification gate (2026-11-10, before any hardware run)

Review of `f9f03b9a7` against the code (not this note), the Asahi `apple.c`/`core.c`
(AsahiLinux/linux `asahi` branch, read 2026-11-09) and WDK 26100 `storport.h`:

| Item | Finding |
| --- | --- |
| Parameter list parsing | `unmap_ranges`: big-endian header and descriptors, each field read once; descriptor bytes bounded by the list (5/1Ah), multiple of 16, UNMAP DATA LENGTH = bytes + 6, reserved bytes zero (5/26h); zero-count descriptors skipped; ANCHOR/GROUP/CONTROL rejected in `ans_scsi_prepare` (5/24h). No defect. |
| LBA unit | `ans_identify_namespace` accepts only LBADS 12 and reports block size 4096; READ/WRITE and UNMAP both pass the SCSI LBA through unscaled. No 512/4096 conversion exists anywhere. |
| Bounds | `lba > blocks || count > blocks - lba` (overflow-safe) against the same `ns->blocks` READ CAPACITY reports; any bad descriptor fails the whole command and zeroes the page. |
| Range count, buffer, tags | At most 256 ranges (list length capped at 8 + 256 x 16, B0h says 0100h), so no splitting. The list is the tag's own page, written under InterruptLock while the slot is free; completion, reset and stop use the ordinary slot path. |
| VPD/READ CAPACITY | B0h/B2h fields equal the enforced limits; LBPME/LBPRZ 0. |
| Reset, shutdown, dump | Ordinary task; the dump instance's namespace is zeroed (no capability). |
| Asahi | Discard is enabled from ONCS alone (`nvme_config_discard`); the Apple driver sets `SKIP_CID_GEN | IDENTIFY_CNS | ADMIN_PAGE_ALIGN`, none about discard; TCB `length` is the CDW12 word (0 for DSM), DMA TO_DEVICE: identical to `ansd_submit`. |

No defect found. What the review cannot give is protection of the other operating systems
on the same namespace, so the driver now carries its own gate (`common/ans_trim.c`):

- `Services\AppleAns\Parameters\Device` (REG_DWORD, read with `StorPortRegistryRead` in
  FindAdapter): `UnmapMode` 0 off / 1 observe / 2 deallocate; `UnmapFenceStart`,
  `UnmapFenceEnd` in namespace (4 KiB) LBAs, end exclusive. Missing, unreadable or unknown
  values give observe; deallocate without a non-empty fence gives observe.
- observe: UNMAP is advertised, validated, translated and recorded, then completed GOOD
  without a command. deallocate: ranges not entirely inside the fence are dropped (UNMAP is
  advisory: LBPRZ 0, nothing may depend on it) and only the rest is sent. off: as before S3.
- `ans_io_request` gets an independent mode check from the wrapper; the dump instance's
  policy is zero (off).
- Receipt (`TELEMETRY.md`, control code 0x001b6004): counters per command and range, the
  hull, up to 16 disjoint extents whose union contains every validated range (nearest two are
  joined on overflow, so the union only grows), the last 32 ranges. Diag 0xA9 records
  mode<<16 | ONCS at every start.

Hardware order: observe (ranges compared with the GPT) -> deallocate with the fence on the
Windows partition and a scratch file -> full retrim. `ArgumentString`/`DriverParameter` was
not used: Microsoft documents `StorPortRegistryRead` for exactly this key.
