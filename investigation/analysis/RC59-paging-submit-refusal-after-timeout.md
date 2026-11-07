# RC59: the stream 0x119/2 is a refused system paging submission

Offline branch `codex/rc59`, base `0b56c5e1` (integration with rc54 and rc57).
No hardware, guest, cdb or other worktree was used. Derived evidence:
`.local/handoff/codex/rc59/dump-decode.json` (all four dumps, SHA-256 of each).

## What the dumps prove

The four saved GravityMark stream-on dumps are ARM64 PAGEDU64 triage dumps
(DumpType 4). TRIAGE_DUMP64 at file offset 0x2000 gives the captured stack
(TopOfStack, 0xd00 bytes), the driver list and 1086 data blocks. The guest
`dxgmms2.sys` 26200 image (`.local/reference/guest-bin-26200`, SizeOfImage
0x113000, the dump module's size) and its public symbols resolve every
dxgmms2 return address; each one follows a `bl`/`blr`.

| dump | P2 | stack (innermost dxgmms2 first) |
|---|---|---|
| EXP1969 `crash-b/102626-13906-01.dmp` | 80000011 | identical |
| EXP1976 `rec1976h/102626-14562-01.dmp` | c000000d | identical |
| EXP1978 `rec1978h/102626-16953-01.dmp` | c000000d | identical |
| EXP1980 `rec1980h/102626-14046-01.dmp` | c000000d | identical |

`VidSchiWorkerThread+0xd4 -> VidSchiRun_PriorityTable+0x1ac ->
VidSchiSubmitPagingCommand+0x2f0 -> VidSchiSendToExecutionQueueWithWait+0x5c
-> VidSchiSendToExecutionQueue+0x9b8 -> watchdog -> KeBugCheckEx`.

`VidSchiSendToExecutionQueue` (disassembly 0x140024bd8-0x140024c04) calls
`WdLogSingleEntry5(0, 0x119, 2, status, sp+0x90, x19)` after a nonzero DDI
return when its allow-failure flag is clear. **P3 is the
DXGKARG_SUBMITCOMMANDVIRTUAL it passed** (filled by
`VidSchiFillSubmitCommandDataVirtual`, packet flag bit 13), **P4 is the VIDSCH
packet** (its +0x68 is the same fence). rc9's RE already showed that
`VidSchiSubmitPagingCommand` passes allow-failure 0: any paging refusal is a
bugcheck, not a device removal.

Decoded P3 (WDK26100 `d3dkmddi.h:5218-5232` layout), in every dump:
`Flags = 0x1` (Paging only), `DmaBufferSize = 0x10`, one 0x80-byte private
record, `UmdPrivateDataSize = 0`, node/engine 0, hContext a `RAOC` v5 system
context. The private record is a valid `ADMISSION_PAGING_RECORD`
(`render_paging.h`): EXP1976/1978/1980 a 512 KiB `AdmissionPagingVirtualFill`
of pool segment 3; EXP1969 a physical Discard plan. **Its fence is the
firmware-timed-out render fence + 1 every time**: 0x6879/0x687a,
0x2b73/0x2b74, 0xb499/0xb49a (`Wom1FirmwareRecovery`, kind 4 Timeout), and
0xbf2f for EXP1980 (its receipts were not captured). The system context's
`GpuvaG3Process` (+0xc8, captured) is non-null.

## Which KMD path returned the status

`AdmissionDdiSubmitCommandVirtual` (`gpuva_g3_windows.c:5247`) ->
`AdmissionDdiSubmitCommandVirtualInner` -> system context ->
`AdmissionGpuvaG3SubmitVirtualPaging` (`:5175`, `:3643`):

- input checks `:3654-3674`: every captured field passes;
- shape `:3676-3680` and records `:3682-3686`: the dumped record passes the
  real `AdmissionPagingRecordsValid` (`render_paging.c:27`), executed in
  `tests/test_g4_paging_submit_containment.py`;
- remaining: `AdmissionCpuQueueSubmit` at `:3694`, rejected at `:3698`
  (`AdmissionG4RejectPagingQueue`), then the wrapper maps the status to
  INVALID_PARAMETER at `:5281`.

`AdmissionCpuQueueSubmit` (`work_queue_windows.c:34-48` at base) returns
`STATUS_DEVICE_BUSY` exactly when PagingStopping, **SchedulerFaulted**, a full
CPU queue, or a refused `AppleAgxSchedulerQueueFence` (full queue, **an
unclaimed boundary preemption** via `AppleAgxSchedulerAdmissionBlocked`,
`apple_agx_scheduler.c:161`, or an out-of-order fence). EXP1969's P2 is that
DEVICE_BUSY (before RC43 renamed it). Stop is not in progress, and with the
render job active and the paging fence its direct successor, neither queue
can be full nor the fence out of order. **The refusal is the CPU-queue
admission at `work_queue_windows.c:48`, caused by SchedulerFaulted or by an
unclaimed preemption.** Which of the two cannot be read offline: the adapter
context is not in the triage data blocks, and no receipt captured
`SchedulerFaulted` before the stop. The stale `AppleAgxRenderAdmission.sys
+0x41414/+0x41464` frames in EXP1980 are `AdmissionPagingReceiptStage` under
`AdmissionQueueDdiStatus`, i.e. the DDI failure recorder ran on this stack.

Both states arise only around a dropped job:

- SchedulerFaulted is sticky until an engine reset (`scheduler_windows.c:659`);
  the backend sets it on any worker failure after a dropped job
  (`backend_platform_windows.c:4303, 4518, 4674`, ...). EXP996 recorded the
  same "stuck completion faulted the shared scheduler, VidSch bugchecked
  0x119 on the next paging submission" (`gpuva_g3_windows.c:908-911`).
- A preemption VidSch requested while the hung render ran stays in
  WaitCurrentBoundary/ReadyToNotify until the platform worker finishes
  (`AdmissionSchedulerTryNotifyPreemption`, `scheduler_windows.c:91`, defers while the render worker is
  scheduled); after a drop that worker also exports recovery/fault receipts.

## Fix and containment

Commit `981166c1`: a valid paging packet is never refused for scheduler state.

- SchedulerFaulted: the packet is queued and held. Dispatch already stops
  while faulted (`AdmissionDispatchQueuedWork`). The unfinished fence reaches
  dxgkrnl's TDR; per-engine reset (`SupportPerEngineTDR`, lifecycle.c:789)
  runs `AppleAgxSchedulerResetEngine`, which keeps it resubmittable; VidSch
  resubmits it with its original fence.
- Unclaimed preemption: `AppleAgxSchedulerAdmitPagingFenceAsPreempted` puts
  the fence in the preempted list behind the cutoff, stores no packet, and
  keeps its records counted unsubmitted (R161 quiescence). WDDM resubmits
  every packet after the reported LastCompletedFence; the resubmission finds
  it (`AppleAgxSchedulerQueueResubmittedPagingFence`). A resubmission of a
  still-preempted fence during a new pending preemption is accepted unchanged.
- Present/NOP refusals stay fallible device errors; PagingStopping, an old or
  repeated fence and capacity are still refused.

This turns the 0x119 into a per-engine TDR or an ordinary resubmission. It
does not cure the firmware Timeout of the stream job, and it does not make a
TDR survivable when the firmware never returns the job (EXP1919 0x116).

## GravityMark's stream use versus the probes

EXP1976-1980 UMD logs: during loading GM records 48 dispatch-indirect
ExecuteIndirect calls (MaxCount 1, no count) and five counted R32-indexed
draws with **MaxCount 60000**, stride 20, after compute work in the same
list; their arguments and count are GPU-produced. No ExecuteCommandLists is
logged after those recordings before the log ends, so the timed-out job is
most likely the first frame that carries them. RC43 (MaxCount 2) and RC57
(MaxCount 2400, statistics) read records and count from an UPLOAD heap. The
generator always allocates MaxCount records of VDM and USC clones
(`agx_win32_indirect_stream_emit.h:178-181`) and launches MaxCount+1 threads.
Commit `e3b44b69` adds **RC59G**: 2400 live records and the count written by
GPU copies into a DEFAULT buffer, INDIRECT_ARGUMENT barrier, one MaxCount
60000 ExecuteIndirect, then the ordinary continuation draw.

WHAT IS STILL UNKNOWN: SchedulerFaulted versus unclaimed preemption at the
refusal; why the stream job times out in 14-26 ms; whether VidSch resubmits a
paging fence admitted during a pending preemption (assumed per the
DMA_PREEMPTED contract, otherwise TDR recovers it); native WDK build.
