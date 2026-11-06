# ANS008 — native write support plan (offline draft, 2026-11-06)

Status: offline design. Nothing here has run on hardware.

## Observed contract

- Broker (m1n1 hv_ans_broker.c `ans_broker_validate_io`): already validates NVMe WRITE (opcode 1)
  as TO_DEVICE with NLB and FUA only. Today it is denied by `read_only`
  (`ANS_FEATURE_READ_ONLY` in the header features).
- DMA policy (hv_ans_memory.c `ans_memory_translate`): direction-aware. TO_DEVICE needs the
  stage-2 read permission (0x40), and the same launch-region exclusions apply.
- Driver (native.c): rejects any NVMe opcode other than 0/2, requires DATA_IN, and builds
  requests through `ans_readonly_request`. ans_scsi maps WRITE(10/16) (0x2a/0x8a) and
  SYNCHRONIZE CACHE (0x35). The MODE SENSE caching page reports write cache state.
- Data at risk: namespace 1 holds the user's macOS container and the Windows install.
  A wrong LBA or a wrong DMA source corrupts real data.

## Safety design (layer that owns the contract: the EL2 broker)

1. The broker gains a write window `[lba_first, lba_last]` and a write-enable fact, both
   fixed at launch by the launch contract (not settable by the guest). A WRITE outside the
   window returns DENIED before any doorbell. With no window, the broker stays read-only
   (today's behaviour).
2. First window: the extents of a preallocated scratch file. The normal recovery guest
   creates `C:\ANS-SCRATCH.BIN` (64 MiB, known pattern, non-resident, no compression or
   sparse flags). `fsutil file queryextents` gives its LBA ranges, which are recorded in
   the packet. They must be one contiguous run, or the window becomes a list.
3. First hardware checkpoint (ANS008-R1): WinPE opens the volume read-only, so no
   filesystem writes happen. A raw-sector tool in WinPE writes a new pattern into the
   window via `\\.\PhysicalDriveN`, with the disk read-only attribute cleared only for
   this step. It reads the pattern back. Any write outside the window would show as a
   broker DENIED receipt.
   - Open item: WinPE has no `dd`. A tiny test executable (raw write/readback with
     unbuffered I/O) must be built on the builder.
4. After the run, the recovery guest verifies the scratch file contents (hash) through
   the bridge path. It also verifies that the NTFS volume is clean (`chkdsk /scan`,
   read-only) and that macOS was not touched.
5. Only after that: NTFS writes inside a full write window, ordering and FLUSH/FUA, then
   native boot.

## Smallest falsifiable checkpoint

ANS008-R1 passes when:

- the pattern written natively reads back natively;
- the bridge-path hash of the scratch file matches the pattern;
- there are 0 DENIED results outside the window;
- chkdsk is clean.

Recovery path: the unchanged normal recovery launch. The scratch file is disposable.
