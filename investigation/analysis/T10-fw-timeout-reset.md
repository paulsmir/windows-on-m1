# T10: firmware timeout and failed reset (offline)

Scope: EXP1919 evidence only; no hardware, guest, package or other-worktree
operations. The source proves an observability defect, not the original hang's
cause. Add reset-step provenance; retain recovery behavior and fail-closed gates.

Supplied receipt text, decoded offline: first type-4 event fence 0x194a,
PID 9720, context 0xffffa704b1d6f060, halt count 1; last fence 0x240d,
PID 280, context 0xffffa704bad3f820, halt count 2. Both have 2 ms submit-to-event
and resumed|job_known. Neither read FAULT_INFO. FirmwareRecovery reports 2
recoveries, 0 deadline expiries and longest job 50 ms (fence 0x1c16).
The pre-crash text contains no Wom1GpuReset. This is not a post-reset snapshot.

## WINDOWS CONTRACT

Pinned WDK 26100 `d3dkmddi.h:8242-8260` defines ResetEngine at PASSIVE_LEVEL
and LastAbortedFenceId as the last packet aborted during execution;
`:8333-8341` declares ResetFromTimeout. `:1062-1076` permits DMA_PAGE_FAULTED
with a known faulted fence and zero VA when the error has another cause.
The existing implementation deliberately uses that notification for firmware
drops, not DMA_COMPLETED (`backend_platform_windows.c:3098-3121`).

The repository's prior contract audit `G4-recovery.md:19-35` and
`R151-prequeue-contract.md:35-52` records the Microsoft engine-TDR contract:
engine failure can promote to adapter reset; reset must actually quiesce work,
empty hardware queues, and report the correct aborted fence. Never manufacture
completion of memory still owned by the GPU. Current engine reset preserves
never-started render scenes for resubmission; adapter reset advances the last
completed fence to the last submitted fence only after success.

Official references (not fetched: this task is explicitly offline):
- https://learn.microsoft.com/en-us/windows-hardware/drivers/display/tdr-changes-in-windows-8
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_resetengine
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_resetfromtimeout
- https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check-0x116---video-tdr-failure

The EXP1919 dump lists P2 as AppleAgxRenderAdmission+0x398c0, P3 c0000483,
and a dxgkrnl TDR worker stack, without a KMD stack frame. P2 identifies the
responsible driver location; it does not identify the failing return statement.
No matching KMD PDB/map was found in this worktree or the EXP1919 package/evidence
under the shared `.local` artifacts. The PDB named in the supplied log is the
x64 **UMD**, not the ARM64 KMD. Do not map this RVA using an older package's PDB.
The preserved KMD is SHA256
`1f47579e7a76c097098517fd85c25c1a20add783b031485d8ab6b48484f5f088`,
built from db67ef1af53aea21266e9f169311bd3b061a88d8 (kmd-build-receipt.json).

## AGX/ASAHI CONTRACT

Primary reference `.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/`
at commit `77cb8f24c2381a8abb7272d7bbdec548d6426a8a`:
- `fw/channels.rs:245-274`: event type 0 Fault, type 1 Flag, type 4 Timeout;
  Timeout carries counter, unknown word, signed event slot. These are distinct
  message kinds, not a success Flag notification.
- `channel.rs:365-381` dispatches Fault/Timeout to their GPU handlers.
- `gpu.rs:1422-1441`: Timeout reads fault information; valid MMU fault information
  turns it into WorkError::Fault, otherwise WorkError::Timeout. Thus a type-4
  event is not evidence of a pure execution timeout, or of a benign stall.
- `gpu.rs:1006-1040`: pending-stamp info bit 0 marks a pending entry; V13_5
  uses slot=(info>>4)&0x7f, flags=info&0xf (older versions shift 3). Affected
  work is marked error through its wait value, other culprit slots Killed,
  and pending-stamp records are cleared before resume.
- `workqueue.rs:945-975`: mark_error first retires already successful commands,
  then marks remaining commands through the given stamp; it does not itself
  forge hardware stamp writes. `:850-940` retires commands when stamps advance;
  command completion and job fence error signaling are separate from resume.
  `workqueue.rs:206-217` invokes the callback with the saved error;
  `queue/compute.rs:372-378` and `queue/render.rs:1381-1388` set the fence error
  and complete a command; `queue/mod.rs:160-170` signals the job fence only
  when its pending command count reaches zero. Timeout maps to ETIMEDOUT,
  fault to EIO, killed work to ECANCELED (`workqueue.rs:83-92`).
- `gpu.rs:1059-1097`: wait up to HALT_ENTER_TIMEOUT (100 ms) for halted, then
  write halted=0 followed by resume=1. This is recovery from a firmware halt,
  not a fresh firmware boot and not an acknowledgment that recovery finished.

The pinned m1n1 (21c3648d) and Mu (361a2134) git objects are absent from the
local submodule object stores. Inspected available object-store HEADs, explicitly
not substitutes for exact EXP1919 source: m1n1 c6d10e04afdad5314e8ac1e67bc3919b094ab000,
Mu f1ef718e08db0e4c30fdb5d8555973513ad9a004. No other worktree was read.
m1n1 `proxyclient/m1n1/fw/agx/channels.py:360-365` calls type 4 TimeoutMsg,
interpreting the two words at +4/+8 as one u64 counter and +12 as stamp_index.
`agx/__init__.py:242-268,292-306,343-354` checks fault info, clears pending
stamps, and resumes by the same writes; it also rejects stale timeout counters.
`fw/agx/initdata.py:10-26` places halt_count/halted/resume at 0x10/0x20/0x30.
Mu's `Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgx.asl.inc:2-49`
exposes APPL0002, coherent resources and G13/V13_5 properties, not a Windows
runtime firmware-recovery service. Exact current firmware equivalence remains
unverified offline. EXP1919's manifest instead pins the actual m1n1 artifact
b97cf8834c3f8e8728b796bf211ef8d21f020db2a427415616b5ad54d05baf49 (0d3a75b3).

## TRANSLATION

`apple_agx_g13_codec.c:102-137` decodes type 4 as TerminalFault and preserves
counter/stamp index. `apple_agx_g13_queue_runtime.c:575-588` passes only kind
and current fence to Recover. `backend_platform_windows.c:2167-2246` captures
the active owner and optional FAULT_INFO, waits for halted, and performs the
same resume writes. FAULT_INFO is **already available for timeout events**;
EXP1919 did not enable Wom1GpuFaultInfoRead. Keep this opt-in: prior mappings
aborted this read (EXP592/671). No new MMIO access is needed for this change.

The KMD substitutes one outstanding job's synthetic recovered completion for
Asahi's per-slot pending-event processing:
`apple_agx_g13_queue_provider.c:767-779` emits ObservationRecovered and makes
the provider reusable; `apple_agx_backend_runtime.c:392-401` sets all queue
complete flags and invokes terminal Dropped; `backend_platform_windows.c:3180-3427`
commits scheduler and local packet retirement, reports DMA_PAGE_FAULTED, then
retires the context/private transaction. It does not wait for success stamps on
this path. A software successor may dispatch normally or be refused after the
owning process is poisoned; no source evidence proves all successors completed
on EXP1919. Resume writes alone do not prove firmware queue consumers settled.

Mismatch to investigate, not fix speculatively: KMD ignores timeout counter/
culprit slot during recovery and does not perform Asahi's pending-stamp clearing.
The extent to which the serialized KMD queues require that operation must be
measured against the exact retained firmware. Clearing guessed offsets, advancing
stamps, or declaring all successors successful would violate memory ownership.

`scheduler_windows.c` ResetEngine has three c0000483 gates: active-job
quiescence failed, G3 PrivateReset failed, or a packet became active after the
quiescence check. `gpuva_g3_windows.c:713-730` rejects any Queued && Started
private scene, even if it belongs to an earlier job. `:733-768` clears Queued
only after GpuDone and reporting. Thus an unretired earlier transaction is a
plausible private-reset failure, but EXP1919 does not select it over a later
active-job failure. `backend_platform_windows.c:5043-5134` already records
quiesce outcome (state, worker, poll, backend, timeout, ended). The v2 reset
receipt cannot identify which subsequent gate returned the error.

Ownership: retained m1n1/firmware contract owns initial power/ASC setup and
hardware queue operation; Mu exposes resources; KMD owns runtime transport,
DMA mappings, queue bookkeeping, Windows interrupts/fences and bounded recovery;
Windows owns scheduling/TDR/device removal. No initialization, IRQ route, memory
map, power, firmware or capabilities change is proposed.

## Implementation plan

1. Extend the real reset replay with failure provenance assertions: timeout,
   failed private-scene preflight after quiescence, and newly active packet.
   Existing success/resubmission/fail-closed assertions must remain passing.
2. Append v3 reset stage/history fields after the entire v2 prefix; preserve
   historical receipt decoding. Set stage immediately before each checked
   reset operation, finish with Complete only on success. Record each attempt's
   stage at the same ring index as its existing entry. No new hardware reads.
3. Test emitted native structs against the offline decoder, old v1/v2 records,
   ring wraparound, malformed version/size. Run targeted tests and the required
   default baseline; leave known_failures.txt and experiment/change ledgers alone.
4. Commit this independently reviewable diagnostic correction and write REPORT.md.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A failed reset must identify
its own failing gate, not the preceding attempt's stage or a generic hardware
error shared by three independent causes. Receipt decoding must correlate each
stage with the correct reset after ring wraparound and keep v1/v2 readable.

## Exact receipt format and interpretation

Wom1GpuReset remains REG_BINARY under the APPL0002 device key. Version 3 is
536 bytes, little endian. All v2 offsets are retained, including its 96-byte
v1 prefix, timing/count fields at 96, and eight 48-byte history entries at 112.
The Version/Bytes header changes to 3/536. New fields:

| Offset | Type | Meaning |
| --- | --- | --- |
| 496 | u32 | Stage of the last reset attempt |
| 500 | u32[8] | HistoryStage, matching History indices |
| 532 | u32 | ReservedV3, zero |

Stage values: 0 None, 1 Validate, 2 Quiesce, 3 Private, 4 Paging,
5 PacketActive, 6 PacketReset, 7 BackendRelease, 8 Scheduler, 9 Complete.
They identify the last operation checked, not all steps successfully executed.
On failure Stage is the refusing gate; Quiesce retains its existing finer result.
Ring entry index is (Resets-1)%8; decoder reorders both arrays together.
Decoder accepts v1/96, v2/496 and v3/536 and rejects mismatched sizes/versions.
Wom1GpuFault v1/168 and Wom1FirmwareRecovery/32 are unchanged.

The receipt is written/flushed by existing `receipts.c:2997-3015`; no new I/O
or MMIO is added. It covers attempts entering AdmissionResetEngineRecorded.
An earlier failure in AdmissionScanoutSetTimelinePaused (scanout_windows.c:1428)
can still bypass it. Always correlate EntryMs/Resets with the actual failing
reset, not just the last available registry value. This change does not claim
coverage of that earlier adapter entry or of native firmware completion.

## WHAT IS STILL UNKNOWN

- Exact cause of either EXP1919 timeout (FAULT_INFO not read), firmware counters,
  culprit slot, pending stamps and hardware queue consumption at the event.
- Whether both Dropped completion callbacks and Windows fault notifications
  finished, which successor first stopped, and which reset precondition failed.
- Exact symbol at +0x398c0 without the matching ARM64 KMD PDB; no source line is
  assigned to that RVA. No native WDK build or hardware verification in this task.
- Whether pending-stamp clearing or stale-event handling is necessary for this
  retained firmware contract. The current resume handshake matches Asahi but
  its post-resume completion assumptions remain a hardware question.

## Smallest hardware discriminator (for the hardware owner only)

WHY THIS HYPOTHESIS:
1. Both type-4 events reached the KMD resume writes in 2 ms; the initial missing
   resume hypothesis is excluded by the receipt, but stamp progress is unmeasured.
2. The same c0000483 can come from three distinct reset gates; the supplied
   pre-crash receipt has no Wom1GpuReset, so selecting a fix now is unsupported.
3. A private scene can outlive local packet retirement until final reporting;
   stage=Private with Quiesce=None/Ended would isolate that lifecycle boundary.

Keep firmware, caps, signer, workload and recovery profile fixed. First retrieve
Wom1GpuReset from the existing EXP1919 dump/device registry and the matching KMD
PDB if already preserved; this may avoid a new run. Otherwise use one exact
hash-verified package carrying only this diagnostic change, same GravityMark
workload, collect Wom1GpuFault, Wom1GpuReset, Wom1FirmwareRecovery, existing queue/
completion receipts and dump. Join the fault fence to the later reset fence.
Quiesce+Timeout identifies unfinished active work; Private identifies retained
Queued&&Started state after quiescence; PacketActive identifies reactivation.
Complete excludes these gates. Preserve the original dump and exact package,
then the established ordinary GPU-visible cleanup; immutable GPU-hidden fallback
only if required by the existing recovery contract. No hardware is run here.

A separate measurement can enable the existing Wom1GpuFaultInfoRead opt-in after
the hardware owner verifies the mapping contract. Do not silently bundle a new
MMIO read with the stage-only experiment. Pending-stamp/counter instrumentation
is a subsequent discriminator if the reset stage does not locate the boundary.

## Offline verification

The test extension first failed to compile against the old receipt, then,
with only the new header/decoder present, the real reset replay aborted on the
missing Stage/HistoryStage success assertion. After stage recording it passed.
Logs in `.local/t10/red.log`, `red-behavior.log`, `green.log`.
The focused real-C suites (ASan/UBSan) and decoder: 12 tests passed
(`affected-green.log`). Default baseline before and after the implementation:

```
ran 414 tests in 175 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

Broader optional checks also exposed three unchanged failures, excluded from
that runner's default module set: obsolete `AppleAgx.vcxproj` registration
assertion in test_apple_agx_backend_runtime; missing PagingNotifyQpc and
KeQueryPerformanceCounter shim in test_apple_agx_present_fault_notification;
and absent m1n1 checkout in test_firmware_recover_layout. Their input files
match HEAD. No unrelated tests or known_failures entries were modified.

Read-only independent review found no blocking issues; its early scanout-entry
coverage limit is recorded above. Native WDK build and hardware remain pending.
