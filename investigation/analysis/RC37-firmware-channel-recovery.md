# RC37: G13 channel recovery and a deliberate CDM fault checkpoint

2026-10-26; offline `codex/rc37`, base
`c3c23c448c54513a49b6d21c3d6f45bafd56083c`.

## Result and limits

The missing **ChannelError -> RecoverChannel** branch is implemented. It decodes
the firmware's operands, resolves the active queue owner, publishes the G13/V13_5
device-control command, and waits for its receipt. The existing all-queue
retirement gate still controls DMA ownership and error completion. A missing
handler or ambiguous command failure never falls back to stopping RTKit or
re-publishing an uncertain command.

**This is not a demonstrated fix for EXP1964/1965's 0x116.** Those runs establish
Timeout, halt/resume, and stalled TA/3D/compute retirement. They do not establish
that firmware emitted ChannelError. No supported operation was found that lets
the host invent RecoverChannel operands for a Timeout-only stall. That state
still fails closed. No hardware, guest, builder, network, package or other
worktree was used; no reset-success claim is made from host tests.

## Machine-state evidence

Shared read-only evidence roots:

- `EXP1964-gpu-count-predicate/evidence/rec1964h/kmd-receipts-pre-disable.txt`
  lines 172,189,190 and `app-logs/gm1964b-umd.log:1`; RC34's complete report,
  analysis and decoded receipts identify PID 720/fence 0x3fb8, fault 70 ms after
  submission. Raw FAULT_INFO `0x11ac9a001303cc11`: unmapped CDM read at
  `0x11ac9a0000`, VM 1. Halt count 1, resumed, done/write TA149/151,
  3D1006/1008, compute904/905. Engine/engine/adapter reset quiescence each times
  out; final 0x116 with STATUS_DEVICE_HARDWARE_ERROR.
- `EXP1965-gpu-vdm-stream/evidence/rec1965h/kmd-receipts-pre-disable.txt:187-191`
  repeats the same raw fault for PID7248/fence0x3cbb after71ms. TA697/699,
  3D1060/1062, compute739/740 remain unretired. RC36's 10k section establishes
  the failing submission as combined render/CDM. Saved serial logs provide no
  additional ChannelError proof. Both decoded reset receipts have last_poll_guard0.

These are saved observations, not newly read live state. No third comparison
against an old working artifact was performed. The confirmed source gap below
is actionable independently; the timeout-only cause remains a separate question.

## Primary firmware contract and source comparison

All Asahi coordinates are under
`.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/`:

| Source | Observed contract |
|---|---|
| `gpu.rs:1005` | Mark pending stamps' owners with errors; clear pending records. This does not complete work. |
| `gpu.rs:1059` | Bounded wait for halted; store halted=0, resume=1. No host completion-pointer reset. |
| `gpu.rs:1422`, `:1443` | Timeout/Fault read hardware fault info, mark errors, then recover. |
| `gpu.rs:1460`, `:1478`, `:1507` | ChannelError independently marks owner work erroneous, resolves the slot's QueueInfo, sends RecoverChannel, kicks device control and waits. No halt/resume in this handler. |
| `fw/channels.rs:145`, `:176`, `:187`, `:281` | G13/V13_5 device-control size0x30, RecoverChannel opcode0x0f; pipe@4, packed QueueInfo GPU pointer@8, event value@0x10. Event8 fields error/pipe/slot/value@4/8/c/10. |
| `event.rs:218`, `:233` | Slot maps to its owning queue. An event is not authority to choose an arbitrary queue address. |
| `workqueue.rs:851`, `:945` | mark_error first accounts for already-completed work, then tags affected pending commands; completion is driven by firmware stamps. |
| `fw/workqueue.rs:83` | gpu_doneptr differs from gpu_rptr (fetch); CPU does not manufacture firmware retirement. |

At the base commit, `shared/src/apple_agx_g13_codec.c:130` classified event8 as
a generic terminal fault and lost its fields. `queue_runtime.c:621` only handled
Timeout/Fault; ordinary event8 reached terminal Quiesce, and event8 during
RecoveryPending returned ResetFailed. `backend_platform_windows.c:2367` implements
Quiesce using RTKit stop, without a demonstrated retained-profile restart.

Current implementation coordinates under `drivers/apple-agx/`:

- `shared/src/apple_agx_g13_codec.c:130` decodes the four event8 words.
- `shared/src/apple_agx_device_control.c:83,303` encodes/publishes opcode0x0f,
  zero padding and exact operands through the established channel12 publisher.
- `shared/src/apple_agx_g13_queue_runtime.c:652` resolves pipe and slot against
  the trusted binding and exact outstanding value of this serialized job. It
  accepts one acknowledgement per participating pipe. Only a previously
  acknowledged tuple is recognized as a delayed duplicate against a successor.
- `shared/src/apple_agx_platform_provider.c:438,718` forwards the callback and
  its original context through the provider's reconstructed I/O table.
- `render-admission/src/backend_platform_windows.c:2390` publishes once, waits
  at PASSIVE_LEVEL with the existing bounded 500 ms device-control deadline, and
  preserves the original Timeout/Fault capture when a dependent channel errs.
- `shared/src/apple_agx_g13_queue_runtime.c:584` still requires every published
  GPU done pointer, including all three queues for a combined job. Command
  receipt, elapsed time, a completed subset or success-stamp writes alone cannot
  release the submission. A failed/ambiguous callback latches failure and leaves
  the provider faulted; no automatic re-publication or claim of recovery.

The wire implementation is original project C derived from observed protocol
layout; no external implementation body was copied. Asahi reference files carry
GPL-2.0-only OR MIT notices; they remain untouched.

## Windows contract and existing error completion

Pinned primary ABI: `.local/reference/wdk26100/d3dkmddi.h:1063-1078` defines
DmaPageFaulted: valid faulting fence, node/engine, VA or0 when unknown, diagnostic
fields and process handle. SDK `shared/d3dkmdt.h:2309-2329` defines reset policy
flags; an unknown fence requires FENCE_INVALID. The existing completion path
already sends **DMA_PAGE_FAULTED**, not the system-reserved DMA_FAULTED:
`backend_platform_windows.c:3230`. It supplies a VA/write/level only from a
valid matching fence/context capture. It does not request engine/adapter reset
once firmware retirement was proven. `:3343` aborts the active scheduler fence,
poisons that context, releases its job and reports once; it does not convert the
error into successful DMA completion or discard unrelated pending contexts.

Pinned `d3dkmddi.h:8242` declares ResetEngine and its output fence. RC34 documents
Microsoft's [ResetEngine contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_resetengine),
[ResetFromTimeout contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_resetfromtimeout),
and [engine TDR sequencing](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/tdr-changes-in-windows-8).
Those URLs were not fetched during this offline task. No reset DDI, preemption
capability or TDR delay was changed. The existing reset replay proves success
only after actual retirement, and preserves failure for every partial combined
retirement mask. Windows owns device-error handling and any TDR escalation.

Firmware owns execution/drop/retirement; KMD owns queue publication, interrupts,
DMA lifetime, per-context fences and error notification. m1n1/Mu own retained
launch, memory exposure and power. Available m1n1 object c6d10e04 AGX
`__init__.py:343-391` has halt/resume plus distinct start/stop; available Mu object
f1ef718e `J313AppleAgx.asl.inc:1` exposes resources/V13_5, no reset service.
Exact referenced m1n1 075e3980 and Mu361a2134 objects are absent locally; these
substitutes do not prove EXP1965's exact launch implementation. The preserved
launch contract is expanded RAM/full2688MiB pool, unchanged firmware/caps/signing;
`investigation/CLAUDE_WORKING_METHOD.md:259` describes the one-shot firmware arm.
No power/reset operation is inferred from the substitute source.

## Deliberate fault probe

`windows/icd/agx_gpu_fault_probe.c` is a separate, explicitly invoked C tool. It
uses a small D3D11 compute dispatch through the same native G13 compute builder;
it does not depend on GravityMark or alter the normal D3D12 probe suite.

The one-shot diagnostic is gated by `Wom1GpuFaultDrillAllowed=1`. State belongs
to the caller's existing G3 process object, under the state lock, so PID reuse
cannot inherit an old arm. Ambiguous process identity refuses the escape.
`gpuva_g3_windows.c:3026` provides arm/query; `:3078` consumes the arm only for
that active process/fence, after normal BeginJob and the graph lease. The real
graph walker proves the chosen 64-byte range at 0x10000 unmapped. Root table,
root IPA and lease must exist, graph must be certain and in flight; absent
root/middle/leaf edges are accepted, mapped/unpublished/uncertain state is not.
The CDM input is substituted before encoding at `backend_platform_windows.c:2629`
(standalone app compute, excluding presentation) or`:2857` (native combined).
A failed proof consumes the arm and leaves the original job untouched.

This deliberately faults **CDM instruction fetch**, not a shader data read.
The original app submission validation, other process mappings and normal GPU
admission remain unchanged. Query success requires a hardware FAULT_INFO captured
for the same process/context/fence, VA and VM (`gpuva_g3_windows.c:45`); device
removal alone cannot make the tool pass. Without Wom1GpuFaultInfoRead it cannot
claim the hardware fault was observed. Build/transport failure or an unsuitable
address causes FAIL rather than a fabricated pass.

The parent runs a baseline render child, fault child, then a fresh render child.
Both render children verify a real triangle's center pixel 0xffff00ff; the fault
child warms up before arming. GPU waits are bounded 15 s and each child 25 s. Logs
are written through and named separately by sequence; the parent retains the
original DWM process handle, requires it alive with the same PID, and the render
child checks composition, DwmFlush and timing availability. Run in the same
interactive desktop session as DWM; a session0 SSH process correctly refuses.

Build from `drivers/apple-agx/windows/icd` in the owner’s Windows developer prompt:

```
cl /nologo /W4 /O2 agx_gpu_fault_probe.c /link d3d11.lib dxgi.lib gdi32.lib dwmapi.lib
agx_gpu_fault_probe.exe C:\\agx-evidence\\rc37.txt
```

This task performed only local Clang syntax checks against the pinned SDK for
ARM64, x64 and x86, not Windows linking or execution.

## Falsifiable checkpoint and recovery (hardware owner only; not run)

WHY THIS HYPOTHESIS:

1. Asahi requires an explicit RecoverChannel reply for event8; the base KMD
   discarded its operands and attempted terminal RTKit stop.
2. EXP1964/65 demonstrate that firmware resume alone is insufficient for their
   observed combined job. A controlled CDM fetch fault can expose the subsequent
   event/retirement sequence without GravityMark's allocation/encoding pressure.
3. An event8 reply now has deterministic byte/state tests, while all seven
   incomplete retirement masks continue to refuse reset success.

Prerecord/hash the exact owner-built package and probe. Preserve the established
ordinary GPU-visible recovery artifact and launch profile. Enable the two drill
switches only for this checkpoint, retain normal TDR settings, run the tool in
the interactive session, and collect its four distinct logs, fault/reset/firmware
receipts, event payload/command receipt evidence, ETL and any dump before cleanup.
A PASS requires matching actual FAULT_INFO, error only for the faulted process,
retirement of every published endpoint, one error fence notification, fresh
known-pixel rendering and original DWM survival. The tool's green output does not
by itself prove which firmware command produced recovery: correlate the actual
firmware event sequence and queue receipts. A one-shot combined dispatch+render
variant remains a subsequent checkpoint; this minimal tool exercises standalone
compute.

No ChannelError plus unretired queues means this patch did not fix that stall.
Retain ownership and allow fail-closed TDR escalation; do not write GPU pointers,
issue guessed recovery operands or increase TDR delay. Collect evidence first,
then exact package/devnode cleanup using established ordinary GPU-visible
recovery; emergency GPU-hidden recovery only when that guest cannot recover.
The hardware owner must perform control-plane checks before physical requests.

## Remaining unknowns

- Whether event8 occurs for the EXP1964/65 failure, and whether its correct reply
  returns all three queues. Host tests cannot establish proprietary firmware
  execution or production recovery from that Timeout-only state.
- The original unmapped CDM-read cause. RC36 narrowed the job but did not identify
  its exact program/allocation.
- Compute stamp values are raw Windows fences in all three builder paths,
  whereas Asahi event.rs:58/63 increments 0x100 and`:84` compares signed deltas
  shifted 8. This is a source discrepancy, not proof of firmware kill semantics.
  It was not bundled as an unproven recovery fix; a future change needs a
  queue-owned sequence, stable retry identity and wrap tests, not blind Fence<<8.
- WDK build/link, actual deliberate-fault behavior and DWM/device isolation on
  hardware. The probe is ready for that checkpoint, not evidence it has passed.
