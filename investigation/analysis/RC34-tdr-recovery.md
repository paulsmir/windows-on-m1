# RC34: EXP1964 TDR recovery boundary

2026-10-26; offline work on `codex/rc34`, base
`cb11bc9846f0a74f166bbb4c8b355d2dae78f559`.

## Verdict and implementation decision

**The production recovery fix is not implemented.** EXP1964 reached firmware
fault recovery, resumed firmware, but did not regain ownership of any of the
three work queues. Both engine reset attempts and the adapter reset failed at
the same quiescence gate. Returning success there would let Windows reclaim
memory still potentially accessed by the GPU. The current sources do not
establish a second, usable reset/reinitialization operation for this state.

This is the README exception: explain precisely why no correct behavior
change is supported, rather than adding another receipt-only experiment or
calling a changed status code a recovery fix. No production code, capability,
timeout, firmware, package, or ledger was changed. The deliverable is a
cross-layer executable safety regression and this analysis. Hardware recovery
remains an explicit blocker, not an offline-validated fix.

## Evidence: actual failure, not just the workload timing

Evidence root: `.local/experiments/EXP1964-gpu-count-predicate` in the shared
repository, read only. The durable handoff contains `dump-facts.json`,
`decoded-receipts.json`, `decoded-mapping.json`, and the extracted receipt bytes.

- `investigation/EXPERIMENTS.md:64128` records the 10k crash; the 700 ms number
  at line 64132 is **CPU encoding time**, not a measurement of GPU execution.
- Dump `evidence/rec1964h/102626-16671-01.dmp`: `PAGEDU64`, ARM64, 8 CPUs,
  dump type 4, bugcheck `0x116`; parameters
  `ffffe701e9a02050`, `fffff803eff51ec0`, `ffffffffc0000483`, `3`.
- The triage module record at file offset `0x13dc8` contains name offset
  `0x173d8`, `AppleAgxRenderAdmission.sys`, base `fffff803eff50000`, and image
  size `0x81000`, matching the packaged PE. **P2 = module + `0x1ec0`**, in
  `.text`. There is no PDB/map in the saved experiment directory. The PE's
  CodeView record names a builder-side PDB; no builder or debugger was used.
  P2 identifies the responsible driver; it is not proof of the failing DDI's
  instruction address. No function name or TDR_CONTEXT layout is guessed.
- `evidence/rec1964h/kmd-receipts-pre-disable.txt:190`: the sole firmware event
  is Timeout, fence `0x3fb8`, PID 720, **70 ms after submission**, halt count 1,
  resumed, valid FAULT_INFO `0x11ac9a001303cc11`: unmapped **read** of
  `0x11ac9a0000`, VM slot 1, compute unit `gCDM_CS_KS0_SP(0)`. PID 720 is the
  10k process (`app-logs/gm1964b-umd.log:1`), not an inferred DWM process.
- Receipt line 172: four reset calls total, one earlier successful queued-job
  reset at 19,911 ms. The relevant calls are engine reset at 322,727 ms
  (job age 3,097 ms), engine reset at 323,762 ms, adapter reset at 324,771 ms.
  They wait 1,001 / 1,000 / 1,001 ms and all return `c0000483`, stage
  `quiesce`, outcome `timeout`. Worker idle, backend submitted, poll guard 0,
  one firmware recovery, firmware halted=0. Active fence `3fb8`, completed
  `3fb7`, preemption `wait-current-boundary`; no aborted fence is claimed.

The final before/after retirement samples are identical:

| Queue | Done | Published endpoint | Stamp | Expected stamp |
|---|---:|---:|---|---|
| TA | 149 | 151 | `7a29f700` | `7a29f800` |
| 3D | 1006 | 1008 | `3d29f700` | `3d29f800` |
| Compute | 904 | 905 | `3fb2` | `3fb8` |

All reads succeeded, ring capacity 1280, `RecoveryPending=1`. These sequential
samples establish lack of observed retirement, not an atomic hardware state.
They rule out a missing reset callback, a reset blocked on its worker, and a
successful firmware drop waiting only for Windows notification. They do not
explain why firmware failed to retire the queues. A dependent barrier stranded
behind the faulted compute work is a hypothesis, not a proven cause.

The serial log's final display activity, NVMe dump write and PSCI reset are at
`evidence/EXP1964/full.log:4993`; it supplies no stronger GPU recovery proof.

## Windows contract and owning code

Pinned WDK: `.local/reference/wdk26100/d3dkmddi.h:8242` defines RESETENGINE
and its output fence; lines 8253, 8334 and 8344 declare the reset/restart DDIs.
`.local/reference/winsdk26100/shared/ntstatus.h:9836` defines `c0000483` as
`STATUS_DEVICE_HARDWARE_ERROR`.

The three critical files (scheduler_windows.c, backend_platform_windows.c and
apple_agx_g13_queue_runtime.c) are byte-identical to the saved EXP1964 source.zip.
The actual installed driver is the render-admission driver, not the older
`windows/src/fail_closed.c` scaffold:

- `drivers/apple-agx/render-admission/src/driver.c:87` registers adapter
  reset/restart; line 98 registers engine reset.
- `src/scheduler_windows.c:543` poisons the active context and freezes dispatch;
  line 549 calls the runtime quiescence poll. Lines 573–574 convert its failure
  to `STATUS_DEVICE_HARDWARE_ERROR`.
- `src/backend_platform_windows.c:5130` waits for exclusive polling ownership;
  lines 5183–5186 produce `STATUS_IO_TIMEOUT` when the job remains active.
  The receipt preserves `quiesce=timeout` despite the outward status conversion.
- `src/scheduler_windows.c:750` uses the same engine-reset core for adapter
  reset, then advances the adapter's completed fence to its last submitted
  fence. Line 781 is restart; it requires an initialized, nonfaulted scheduler.
  EXP1964 failed **before** restart could repair anything.

[ResetEngine](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_resetengine)
may succeed only after reset, an empty hardware queue, and readiness for new
packets. [ResetFromTimeout](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_resetfromtimeout)
must guarantee no GPU memory access on successful return; a failed adapter
reset causes a bugcheck. Changing `c0000483` to another error would not recover.

[Windows 8+ TDR](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/tdr-changes-in-windows-8)
requires all three engine DDIs when SupportPerEngineTDR is advertised; adapter
reset/restart remain necessary. Failed engine reset promotes to adapter reset.
The aborted fence must lie in the scheduler's valid submitted/completed range.
Unstarted paging packets may be resubmitted with their original IDs; unstarted
render packets receive new IDs. Therefore “complete every pending packet with
error” is not the engine-reset contract. The existing scheduler retains those
resubmission rights (`shared/src/apple_agx_scheduler.c:413`); successful adapter
reset advances the completed boundary to the last submitted fence.

`src/lifecycle.c:777` advertises PreemptionAware, DMA-buffer-boundary graphics
and compute preemption, and SupportPerEngineTDR. This is **boundary preemption**,
not absence of all preemption: pending packets can be stopped while the current
packet runs to its boundary. The official
[compute preemption definitions](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmdt/ne-d3dkmdt-_d3dkmdt_compute_preemption_granularity)
make this distinction. Neither instruction-level preemption nor a working
arbitrary hard reset follows from those caps.

[TDR timing](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/tdr-registry-keys):
default TdrDelay is two seconds for the preemption wait; TdrDdiDelay is five
seconds for driver threads to leave. Increasing either or disabling TDR is not
a recovery implementation. During reset, interrupts/DPCs still require
synchronization; after successful reset, Windows cleans allocations before
[RestartFromTimeout](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/thread-synchronization-and-tdr).

## Asahi, m1n1, Mu and the missing operation

Primary Asahi source under `.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi`:

- `gpu.rs:1422`: Timeout reads fault information, marks pending events with
  fault/timeout errors, then calls recover. `gpu.rs:1005` marks affected work
  and clears pending-event records. `gpu.rs:1059` waits for halted and writes
  halted=0/resume=1. This is cooperative firmware recovery, not a power reset.
- `workqueue.rs:945`: mark_error tags work; it does not synthesize successful
  completion. `workqueue.rs:851` retires work based on firmware completion.
  `fw/workqueue.rs:83` distinguishes gpu_doneptr from the fetch pointer.
- `gpu.rs:1460` has a **separate ChannelError path** sending RecoverChannel,
  using the event's pipe, slot and value (`fw/channels.rs:187,281`). EXP1964's
  observed event is Timeout. Issuing RecoverChannel with invented operands
  for every timeout is not justified by that implementation.

The KMD already implements pending-event clearing and halt/resume in
`src/backend_platform_windows.c:2209` (writes at 2277–2280), then retains DMA
ownership in `shared/src/apple_agx_g13_queue_runtime.c:582` until all published
done pointers retire. On proven retirement, `apple_agx_g13_queue_provider.c:769`
emits Recovered, and `backend_platform_windows.c:3322` aborts the active fence;
line 3143 emits DMA_PAGE_FAULTED rather than a successful completion. The
survivable-fault regression tests this existing notification path.

The available m1n1 Git object `c6d10e04afdad5314e8ac1e67bc3919b094ab000`,
`proxyclient/m1n1/agx/__init__.py:343`, has the same halted/resume handshake;
lines 357–391 distinguish startup/initdata from ASC stop. The requested
`075e3980` and worktree gitlink `21c3648d` objects are absent from that local
object database. No other worktree was accessed and no claim is made that this
substitute source proves the exact EXP1964 launcher contract.

Available Mu object `f1ef718e08db0e4c30fdb5d8555973513ad9a004`,
`Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgx.asl.inc:1`, exposes AGX
resources and V13_5 identification, not a runtime reset service. It is also not
claimed to be the exact experiment firmware. The retained profile's published
working-method contract (`investigation/CLAUDE_WORKING_METHOD.md:259`) says the
firmware arm is one-shot and device restart cannot reinitialize it.

Ownership: firmware ends/kills GPU work; KMD owns queues, fences, per-process
UAT bindings and error notification; Windows owns TDR policy and device removal;
Mu/m1n1 supply the retained launch/resources. Neither host deadline expiry,
software queue deletion, faulted-context marking, nor a CPU write to a GPU
completion pointer establishes that DMA stopped. RTKit stop alone is not a
demonstrated restart of this retained profile. No external source code copied.

## Test plan and outcome

1. Reuse the existing compiled reset replay; pass separate scenario and firmware
   fixtures without changing production code.
2. Exercise a combined TA/3D/compute submission for every incomplete retirement
   mask. Repeat engine/engine/adapter reset; require failure, retained active and
   pending contexts, no fabricated completed/aborted fence, and failed restart.
3. Let all queues retire without success-stamp writes; require successful reset,
   culpable-context poison, queue drain, correct adapter fence boundary and restart.
4. Mutation check: compile a temporary queue-runtime copy with the done-pointer
   inequality disabled. The new test must fail; unmodified source must pass.
5. Run affected tests and the baseline; record exact results in handoff REPORT.md.

The mutation failed at the assertion rejecting a fabricated completion. The
unmodified implementation passed. This is **not a defect RED→GREEN** and is not
represented as one: the missing hardware retirement is intentionally modeled,
not repaired by making the model advance. No WDK build or hardware validation.

## Next falsifiable checkpoint (operator-owned; not run)

First use the existing `windows/icd/agx_tdr_drill.c:1` baseline and then its
one-shot, PID-scoped `drill` mode (`Wom1TdrDrillAllowed=1`, Jobs=1). It submits a
short draw but strands its worker, so TDR must poll a physically completed job.
Require device error only in the probe, successful engine-reset receipt, usable
desktop and a fresh-device baseline draw afterward. This safely bounds GPU work
and tests the Windows reset path; **it does not prove recovery from hung GPU
execution**. Prepare the ordinary GPU-visible exact-package recovery beforehand.

For the actual unresolved boundary, use one isolated compute dispatch whose
finite data-dependent loop is calibrated beyond TdrDelay but bounded, then the
same combined compute→render dependency shape as EXP1964. An infinite-loop shader
is deterministic as a hang only if compiler optimization cannot erase it; its
recovery is not safe to promise with the present firmware contract. It is not
the first checkpoint. Keep default TDR policy and preserve a verified rollback.

WHY THIS HYPOTHESIS: (1) the recorded firmware fault is on the compute unit;
(2) compute and both dependent graphics queues remain unretired after resume;
(3) all Windows reset attempts reach the same poll timeout without a transport
failure. Compare compute-only versus combined retirement to distinguish a
general failure to kill compute work from a dependent-queue recovery failure.
If both strand ownership, the next implementation belongs in the firmware/queue
reset contract, not in fence reporting. Capture event payloads, pending-event
slots, halt/resume state and queue pointers before/after the existing resume.
Do not prescribe unverified register writes or an arbitrary power sequence.
