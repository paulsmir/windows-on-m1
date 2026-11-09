# ANS-DUMP-MODE: crash dumps on the native SSD (AppleAns)

Status: design, 2026-11-09. No code yet. Class C (production defect: a bugcheck on the native SSD leaves no dump; the
only evidence is m1n1's `BUGCHECK ok code=` line).

## Sources inspected

- `drivers/storage/appleans/windows/{adapter.h,driver.c,direct.c,native.c}`, `common/ans_direct.{h,c}`,
  `windows/tests/dump_mode_test.py`.
- Pinned SDK `winsdk26100/km/storport.h` (DUMP_MODE_*, MINIPORT_DUMP_POINTERS, STOR_FEATURE_DUMP_*).
- Microsoft Learn: "Restrictions on miniport drivers that manage the boot drive", PORT_CONFIGURATION_INFORMATION,
  MINIPORT_DUMP_POINTERS, STORAGE_REQUEST_BLOCK (SRB_FUNCTION_DUMP_POINTERS), HW_INITIALIZATION_DATA FeatureSupport,
  HwStorFindAdapter, StorPortMarkDumpMemory.
- Microsoft sample `storahci` (Windows-driver-samples; copies in the session scratchpad `storahci-src`):
  entrypts.c, pnppower.c, hbastat.c, util.c/h. Behaviour reference only; no code copied.

## WINDOWS CONTRACT (documented)

1. The dump stack loads a second image of the miniport (`dump_AppleAns.sys`), calls DriverEntry with NULL arguments,
   then HwFindAdapter (HIGH_LEVEL) with `ConfigInfo->DumpMode` = DUMP_MODE_CRASH (1) / HIBER (2) / MARK_MEMORY (3) /
   RESUME (4), then HwInitialize and HwStartIo, all above PASSIVE.
2. "The boot device must be accessible before the miniport returns from" HwInitialize.
3. No DPC initialisation, no registry, no time queries, no pool; "should not attempt to allocate more than 32
   kilobytes"; one request per LUN at a time; HwResetBus requests are to be disregarded; target/LUN unchanged.
4. A physical miniport that sets STOR_FEATURE_DUMP_POINTERS receives SRB_FUNCTION_DUMP_POINTERS on the *live*
   instance after HwInitialize. It must set Version (DUMP_MINIPORT_VERSION_1) and Size; `MiniportPrivateDumpData` is
   handed to the dump instance as `ConfigInfo->MiniportDumpData`; `CommonBufferSize` <= 64 KiB;
   `MaximumTransferLength` is required when it differs from the live value. SRB_FUNCTION_FREE_DUMP_POINTERS releases
   what the live instance allocated.
5. `StorPortMarkDumpMemory` is called from HwFindAdapter at least for DUMP_MODE_MARK_MEMORY / HIBER.

What storahci does (evidence of an accepted pattern, same `STOR_MAP_NON_READ_WRITE_BUFFERS` + full-duplex model):
keeps `StorPortGetDeviceBase` and `StorPortGetUncachedExtension` in dump mode (uncached extension kept <= 30 K),
passes a pool-allocated context through `MiniportPrivateDumpData`, sets only Version/Size/MiniportPrivateDumpData,
queue depth 1, timers replaced by `StorPortStallExecution` waits, completion work called inline instead of a DPC,
SRB_FUNCTION_SHUTDOWN is the last SRB after SYNCHRONIZE CACHE.

## ANS CONTRACT (source: common/ans_direct.c, Asahi apple-nvme as cited in SOURCES.md)

- Hand-off: RTKit/ANS is booted by m1n1 (`BOOT_STATUS == 0xde71ce55`), NVMe `CC.EN == 0`. The live instance enables
  the controller (`ansd_enable`), which refuses any other state.
- Queues: linear SQ, tag == slot; admin depth 2, IO depth 64; each of SQ/CQ/TCB is one 16 KiB aligned array per
  queue (6 x 16 KiB) plus `ANS_QUEUE_DEPTH + 2` PRP/metadata pages. Total `APPLE_ANS_DIRECT_BYTES` is well above the
  32/64 KiB dump allocation limits.
- The mailbox must be serviced while waiting (`ansd_mbox_service`), today from a 50 ms timer call and the ISR.
- `ansd_disable` clears `CC.EN`, waits `CSTS.RDY == 0`, and resets queue bookkeeping; it does not touch RTKit.
- No DART and no EL2 broker in the data path (ANS-DIRECT): DMA addresses are `ansd_translate(StorPort physical)`.

## Ownership

| Concern | Owner |
| --- | --- |
| RTKit boot, power, hand-off state | m1n1 (unchanged) |
| Controller enable, queues, I/O, shutdown | AppleAns live instance |
| Controller re-enable and polled I/O at crash | AppleAns dump instance (new) |
| Queue/PRP memory used at crash | allocated by the live instance, lent through the dump context |
| Dump file layout, which SRBs are sent | Windows crashdmp/dump port |

## TRANSLATION

1. **Live instance** sets `FeatureSupport |= STOR_FEATURE_DUMP_POINTERS`. On SRB_FUNCTION_DUMP_POINTERS it fills
   Version, Size, `MaximumTransferLength` (dump value, see 5) and `MiniportPrivateDumpData` = pointer to a
   `APPLE_ANS_DUMP_CONTEXT` that lives inside the live device extension (no pool, nothing to free):
   `{Magic, Version, Bytes, CommonVa, CommonPa, CommonBytes, Namespace (blocks, block shift), Checksum}`.
   `Common*` describe the live instance's own uncached extension. FREE_DUMP_POINTERS completes with success.
   Reason for lending live memory instead of a dump-mode allocation: the queue arrays cannot be made smaller than
   6 x 16 KiB without changing the hardware layout, and the documented dump allocation limit is 32-64 KiB.
   After a bugcheck the live instance never runs again, its memory stays mapped, and `ansd_init` rewrites every
   byte the controller reads, so stale contents cannot leak into the dump path.
2. **Dump HwFindAdapter** (`DumpMode != 0`): no broker, no registry, no pool. Validate the context
   (magic/version/bytes/checksum, `CommonBytes == APPLE_ANS_DIRECT_BYTES`); invalid or NULL -> SP_RETURN_NOT_FOUND
   (today's behaviour, so a damaged context degrades to "no dump", never to I/O). Map the two access ranges with
   `StorPortGetDeviceBase` exactly as the live path. Set the same PORT_CONFIGURATION_INFORMATION fields as the live
   path except `MaxIOsPerLun`/queue depth 1. For DUMP_MODE_MARK_MEMORY / HIBER call `StorPortMarkDumpMemory` on the
   context and the lent common buffer. DUMP_MODE_HIBER / RESUME beyond marking are out of scope: the miniport does
   not set STOR_FEATURE_DUMP_RESUME_CAPABLE and returns NOT_FOUND for RESUME (hibernation stays unsupported, as today).
3. **Dump HwInitialize**: build the six queue arrays and PRP pages over the lent buffer with the live layout
   (`AppleAnsDirectResources` split so the carving code is shared and the only difference is where the buffer
   comes from). Take the controller over: new `ansd_takeover()` in common code = check BOOT_STATUS, then if
   `CC.EN` or `CSTS.RDY` is set, clear `CC.EN | SHN` and wait `CSTS.RDY == 0` (bounded by CAP.TO, mailbox serviced
   while waiting), leaving stage HANDOFF; `CSTS.CFS` or a timeout -> fault -> HwInitialize returns FALSE (no dump).
   Then the existing `ansd_enable`, Create IOCQ/IOSQ. Identify is not repeated (namespace geometry comes from the
   validated context); the write-cache feature is left as the live instance set it. No
   `StorPortEnablePassiveInitialization`, no DPC, no `RequestTimerCall`.
4. **Dump I/O**: HwBuildIo unchanged (SG list -> PRPs). HwStartIo submits and then completes the request
   synchronously: poll `ansd_poll` + `ansd_mbox_service` with `StorPortStallExecution(10)` until the tag completes or
   a bounded budget expires (time counted in stall iterations, never by clock), then `StorPortNotification
   (RequestComplete)` before returning. This does not depend on whether the dump port calls HwInterrupt, which is
   not documented. HwInterrupt stays registered and harmless (finds an empty CQ). SCSI surface = the existing
   translation (INQUIRY, READ CAPACITY, READ/WRITE, SYNCHRONIZE CACHE -> FLUSH); SRB_FUNCTION_SHUTDOWN = FLUSH then
   `ansd_shutdown`, inline. HwResetBus returns TRUE without touching the controller.
5. **Transfer size**: the dump value of MaximumTransferLength / NumberOfPhysicalBreaks stays equal to the live one
   as long as one request's PRP list fits the lent metadata pages (it does: same slot layout, one slot used).

## Regression tests (deterministic contracts)

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH:

- `ansd_takeover` state machine (host C test next to the existing ans_direct tests): from {EN=0,RDY=0},
  {EN=1,RDY=1}, {EN=0,RDY=1 draining}, {CFS}, {RDY never clears}, {BOOT_STATUS bad} the result is HANDOFF or a
  fault, never an enable over a live controller, and the wait is bounded. Catches a dump instance writing queue
  registers while the controller still runs on the old queues.
- Dump context validation: wrong magic/version/size/checksum and NULL all refuse before any MMIO access. Catches a
  HIGH_LEVEL dereference of a corrupted pointer.
- `dump_mode_test.py` is rewritten, not deleted: the invariant becomes "in dump mode nothing reaches the broker, the
  registry, passive initialisation, DPC or timer APIs, and FindAdapter refuses without a valid context"; the
  boot-start INF assertions stay.

## WHAT IS STILL UNKNOWN (needs hardware; none can be derived offline)

1. Whether crashdmp loads `dump_AppleAns.sys` at all for this ACPI-enumerated Storport device and delivers
   SRB_FUNCTION_DUMP_POINTERS (documented for physical miniports in general; never observed here).
2. Whether the dump port supplies both access ranges and a working `StorPortGetDeviceBase` when the live instance
   leaves the optional MINIPORT_DUMP_POINTERS resource fields empty (storahci does so on PCI).
3. Whether ANS accepts `CC.EN` 1 -> 0 -> 1 with requests in flight at the moment of the crash (the live reset path
   `AppleAnsDirectReset` does disable + re-init, but only from a quiesced adapter).
4. Whether the GPU or another device holding a DIRQL storm at crash time affects the polled path (not expected:
   HIGH_LEVEL, no interrupts needed).

## Hardware checkpoints (one variable each; recovery in every case = the staged ANSBUILD011-I / 0.4.2 package, and
the bridge-boot emergency path if the native SSD does not boot)

- **DUMP0 (no code)**: read on the current guest: `CrashControl` (CrashDumpEnabled, DumpFile, MinidumpDir), pagefile
  size, System log volmgr event 46 ("Crash dump initialization failed") since the native-SSD boots, and loaded
  `dump_*` modules. Expected: event 46 present on every native boot, no `dump_AppleAns`. This fixes the baseline the
  later checkpoints are judged against. Read-only.
- **DUMP1 (live half only)**: package with the DUMP_POINTERS handling and a dump instance that validates the context,
  records a receipt in the *context* and still returns NOT_FOUND. Expected: boots as today, System-log record 0xA6
  (SRB_FUNCTION_DUMP_POINTERS answered, sealed = 1) once per boot, I/O gate unchanged. Answers unknown 1 without any
  crash. Implemented 2026-11-09 (`windows/dump.{h,c}`, `driver.c` dump_receipt, `native.c` dump_pointers) with two
  deliberate reductions against the TRANSLATION text: (a) the dump instance does not map the ranges yet - it only
  records whether exactly the controller + mailbox ranges were supplied, so that unknown 1 and unknown 2 are not
  tested by one package; mapping moves to DUMP2's first boot, where the receipt flags CTRL_MAPPED / MBOX_MAPPED are
  already reserved; (b) only Version, Size and MiniportPrivateDumpData are filled (the storahci minimum), because the
  dump transfer limits equal the live ones. Receipts: 0xA6 value = sealed << 8 | SRB count; 0xA7 value =
  calls << 16 | DumpMode << 8 | flags, written by the live instance at the next telemetry query after the dump image
  ran HwFindAdapter (the dump image itself cannot log). No 0xA7 after boot means crashdmp does not call the dump
  image's HwFindAdapter before a crash, which is a valid outcome and not a failure of DUMP1.
- **DUMP2 (full path, small dump)**: `CrashDumpEnabled = 3` (small memory dump; fits the 4 GiB disk gate), then one
  preregistered manual bugcheck (CRITICAL_PROCESS_DIED by ending a critical process; no third-party tool). Expected:
  m1n1 prints the code, the next boot has `C:\Windows\Minidump\*.dmp` readable by cdb with the same code, volume
  clean (chkdsk read-only), no event 46. Failure: no dump file, a hang at "collecting data" (bounded by the guest
  watchdog), or a dirty/corrupt volume -> package rolled back, verdict rejected, cause read from the context stage
  counters that m1n1 can print next to KiBugCheckData.
- **DUMP3**: kernel dump (`CrashDumpEnabled = 2`) once DUMP2 passes and disk space is confirmed.

## DUMP2 as implemented (supersedes the TRANSLATION text where they differ)

EXP2302 (DUMP1) verdict carried in: DUMP_POINTERS answered twice with a sealed record (0xA6), no 0xA7 and no
`dump_` modules before a crash, so nothing about the dump image's environment is measured yet.

- **Order in HwFindAdapter**: structure length -> `DumpMode` -> dump branch. The PnP-restart ownership check is
  skipped for the dump image because its device extension is new and unspecified; the dump branch zeroes it.
- **Validation first**: without a record that passes `ans_dump_context_valid` the dump image returns
  SP_RETURN_NOT_FOUND and has dereferenced, mapped and marked nothing.
- **Modes**: DUMP_MODE_CRASH maps the two register ranges (they must be exactly the live ones) and adopts the lent
  buffer; DUMP_MODE_MARK_MEMORY only marks the record and the buffer and never touches hardware (hypothesis:
  HwInitialize may still be called, it returns TRUE without work); DUMP_MODE_HIBER / RESUME are refused, which
  replaces the earlier text about marking for hibernation. Hibernation stays unsupported.
- **No write before stop** replaces "ansd_init rewrites every byte": `direct_carve` only computes addresses from the
  record (`common_pa` + offset through `ansd_translate`), `ansd_init_takeover` proves CC.EN=0 / CSTS.RDY=0 and only
  then clears the queue arrays; then `ansd_enable` and Create IOCQ/IOSQ. No Identify, no Set Features, no timer:
  the namespace comes from the record.
- **I/O**: `MaxIOsPerLun = 1`; HwStartIo takes no lock, starts the request through the live translation and polls
  `AppleAnsDirectInterrupt` until no slot is busy. Bound: 3 000 000 polls x 10 us; on expiry the adapter is
  faulted and the SRB fails, buffers are kept. Only EXECUTE_SCSI, FLUSH and SHUTDOWN are served.
- **SHUTDOWN = ordered FLUSH only**, as in the live path, instead of "FLUSH then ansd_shutdown inline": I/O that
  crashdmp sends after SHUTDOWN cannot fail, and a bugcheck without a dump already leaves the controller running
  into the reset. `ScsiStopAdapter`, if crashdmp sends it, runs the existing `ansd_shutdown` inline. A normal
  shutdown notification at the end of the dump is a later single-variable step.
- **Not implemented**: stage counters readable by m1n1; the receipt in the live record (flags CTRL_MAPPED,
  MBOX_MAPPED) is the only trace and is lost if the dump never completes. Live register VAs are not passed in the
  record; if `StorPortGetDeviceBase` fails in dump mode, the result is "no dump", and the fallback is a new step.
- HwInterrupt returns FALSE, HwResetBus returns TRUE without touching the controller, ScsiRestartAdapter fails,
  the error log is never called (`AppleAnsDiag` returns at once).

Still unknown (hardware only): whether crashdmp loads `dump_AppleAns.sys` at all once CrashDumpEnabled != 0;
whether the dump-mode configuration carries both access ranges; whether ANS accepts CC.EN 1 -> 0 -> 1 with requests
in flight; whether HwInitialize runs in MARK_MEMORY mode.

Smallest falsifiable step now: DUMP2 (package 0.4.4).
