# KMD interrupt-driven job completion (design note, 2026-11-08)

Class: C (contract: Asahi and macOS complete jobs from the firmware's RX doorbell interrupt; Microsoft's
WDDM model reports DMA completion from the ISR/DPC path) + O (expected 0.3-0.5 ms/frame HD, ~49 % of one
CPU core). Offline analysis: GPU_CURRENT_STATE "STRUCTURAL GAP DECOMPOSED" (2026-11-08), EXPERIMENTS entry
"EXP2268 OFFLINE STRUCTURE". No hardware run yet.

## Sources inspected

- KMD: `drivers/apple-agx/render-admission/src/backend_platform_windows.c` (job worker loop ~7195-7340,
  `AdmissionJobPollSleepWindows` ~1724, `AdmissionNotifyCompletionAtInterrupt` ~5005, job thread ~7675),
  `interrupt.c` (ISR/DPC, broker IRQ status register), `lifecycle.c:140-200` (resource validation),
  `scheduler_windows.c:340` (`AdmissionSchedulerDpc`), `shared/src/apple_agx_platform_provider.c:1008`
  (`AppleAgxPlatformProviderPoll` -> `DrainEvents`), `shared/src/apple_agx_rtkit_session.c:252`
  (`AppleAgxRtkitSessionDrainRuntime`), `shared/include/apple_agx_asc_transport.h` (MAILBOX_FULL bit 16,
  MAILBOX_EMPTY bit 17, `AppleAgxAscReadInboxControl`).
- Asahi: `drivers/gpu/drm/asahi/gpu.rs:95-115, 361-378` (EP_FIRMWARE `MSG_RX_DOORBELL = 0x42<<48` ->
  `rx_channels: fw_log/ktrace/stats/event .poll()`), `channel.rs:317-450` (EventChannel::poll),
  `arch/arm64/boot/dts/apple/t8103.dtsi:526-535` (agx_mbox AIC 575 send-empty, 576 send-not-empty,
  577 recv-empty, 578 recv-not-empty, all level-high).
- m1n1: `hv.c:197` (the 9 AGX G2 routes are always registered), `hv_agx_g2.generated.h` /
  `config/j313-agx-g2.json` (physical->guest: 563-566 -> 880-883, 579 -> 884, 576 -> 885, 575 -> 886,
  578 -> 887, 577 -> 888), `hv_exc.c:2079-2125` (routed level IRQ: AIC line masked on forward, LR inject
  or per-CPU queue, no `bhl`), `hv_vgic.c:666/715` (unmask on guest deactivate),
  `hv_agx_power_mmio.c:755-780` (virtual scanout edge on vINTID 889, polled by the service step).
- Mu/ACPI: `mu/.../J313AppleAgx.asl.inc` (G2 device APPL0002, 9 level Interrupt resources 880-888),
  `J313AppleAgxAbiAdmission.asl.inc` (edge 889); live device per `Wom1TranslatedResources` (EXP2267):
  5 memory regions (SGX MMIO 0x204000000/64 MiB, retained page 0x9fffb8000, 0x9fff70000, broker
  0x300000000/4 KiB, local reserve 0x8e0000000/384 MiB) + exactly ONE interrupt, edge (the 889 scanout line).
- WDK 26100 `dispmprt.h`: `DXGKDDI_INTERRUPT_ROUTINE(MiniportDeviceContext, MessageNumber)` at DIRQL;
  `DXGKCB_SYNCHRONIZE_EXECUTION(..., MessageNumber, ...)`; `DXGKCB_QUEUE_DPC`; `DXGKCB_NOTIFY_INTERRUPT`
  with `DXGK_INTERRUPT_DMA_COMPLETED` (d3dkmddi.h:689).

## Observed contract

### Current KMD (poll)
The job worker thread kicks a job, then loops: drain the RTKit inbox (`AppleAgxRtkitSessionDrainRuntime`),
drain the firmware event channel (`ProviderPoll -> DrainEvents`), depth-2 prebind/kick, then sleep for the
predicted remaining time on an `EX_TIMER_HIGH_RESOLUTION` (slack 492 us) and spin. Completion is reported
to dxgkrnl from the worker through `DxgkCbSynchronizeExecution(AdmissionNotifyCompletionAtInterrupt)`
(DMA_COMPLETED + `DxgkCbQueueDpc`). The ISR in `interrupt.c` serves only the virtual scanout edge (889) via
the broker's IRQ status/enable registers; no firmware interrupt reaches the KMD.
Cost (Wom1JobPoll, EXP2267 suite+GM window): 35454 jobs, 40141 sleeps, requested 24.8 s / slept 37.7 s
(timer wakes 320 us late on average, max 3.5 ms), 1.6 M spins, CPU busy 49 % of job time; per-frame
detect sum 1.02 ms, ~0.3-0.5 ms on the critical path (EXP2268 decomposition).

### Asahi / macOS
The firmware signals host-visible progress with one RTKit message, `0x42<<48` on endpoint 0x20, delivered
through the ASC mailbox `recv-not-empty` level interrupt (AIC 578). The driver's RTKit `recv_message`
polls the four RX rings (fw_log, ktrace, stats, event); the event ring carries the completion/stamp
events that complete jobs. No timer, no spin. (macOS: same mailbox interrupt; AGXArmFirmware interrupt
handler processes the event rings; kext RE allowed, not yet done for this note.)

### Windows
dxgkrnl connects the adapter's interrupt resource(s) and calls `DxgkDdiInterruptRoutine` at DIRQL with a
MessageNumber; the ISR must return FALSE for interrupts that are not its own and quiesce a level source
before returning; `DxgkCbQueueDpc` queues `DxgkDdiDpcRoutine`; `DxgkCbNotifyInterrupt` is legal only from
the ISR or inside `DxgkCbSynchronizeExecution` (already how the KMD reports today).

### m1n1 / Mu
AIC 578 is already routed to vINTID 887 (level) by `hv_irq_routes_register_agx_g2`, with mask-on-forward
and unmask-on-deactivate in the vGIC. The live ACPI device does not list 887, so the guest never enables
it (the line stays masked after the first forward, if it ever fires). The SGX MMIO region (64 MiB) that
contains the ASC mailbox is already mapped to the KMD (`AscIo`).

## Translation (design)

Owner: the KMD (completion detection is driver-owned on every platform). HV involvement: none beyond the
existing static IRQ route (no new broker bit, no HV polling), consistent with ANS-DIRECT and the HV
inventory.

1. ACPI (Mu, behind a build flag so the recovery Mu stays unchanged): add
   `Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 887 }` to the AGX device after the
   existing 889 edge.
2. KMD resource validation (`lifecycle.c`, `interrupt.c:AdmissionFindBrokerResource`): accept one edge
   (scanout) and at most one level (mailbox) interrupt; record both vectors in `Wom1TranslatedResources`.
3. ISR (`AdmissionDdiInterruptRoutine`): dispatch on MessageNumber (see UNKNOWN 1): scanout path unchanged;
   mailbox path: read inbox control; if MAILBOX_EMPTY, return FALSE (not ours / spurious); otherwise pop
   the inbox FIFO into a small DIRQL-safe ring (message + endpoint words), count `Wom1IrqRxCount`, and
   `DxgkCbQueueDpc`. Draining the FIFO is the quiesce for the level line (Asahi/Linux do the same).
4. DPC (`AdmissionDdiDpcRoutine`): hand the ring to the RTKit session (new `DrainFromRing` entry that
   feeds the same message handlers as `DrainRuntime`, so logging/crash handling stays identical), then
   `KeSetEvent(JobThreadWake)` with `JobThreadRequest` = completion-poll request. No event-ring parsing at
   DISPATCH_LEVEL in step 1; the worker keeps owning `ProviderPoll`.
5. Worker: with `Wom1IrqCompletion=1` the poll loop replaces the predicted sleep by
   `KeWaitForSingleObject(JobThreadWake, timeout = predicted + slack)`; the timer remains as the bounded
   fallback (lost interrupt, timeout/TDR classification unchanged). Depth-2 prebind/kick runs on each wake
   as today. `Wom1JobPoll` keeps counting (sleeps -> waits) so the same receipt measures the change.
6. Fallback if the firmware's RX doorbell does not accompany every completion (UNKNOWN 2): keep the
   predictive timer as the primary wake and use the interrupt as an early wake only; still removes the
   late-wake term.

Invariants kept: completion reporting still goes through `DxgkCbSynchronizeExecution` + DMA_COMPLETED;
the fence/transaction guard (`AppleAgxCompletionTransactionCanReport`) is untouched; event-ring parsing
stays single-threaded in the worker; RX drain of stats/ktrace/fwlog (class C, EXP2219) unchanged.

## What is still unknown (EXP2272 answered 1 and 2; see the ledger)

EXP2272 (2026-11-08): dxgkrnl connects both line interrupts and passes MessageNumber 0 for both; the
firmware raises an RX doorbell per job completion (38905 messages / 33796 jobs); ring high water 3, 0 drops;
GM -1 % (no wake yet). Remaining: item 3 (latency of the routed level line), measured in step 2.


1. dxgkrnl and two line-based interrupt resources: which MessageNumber (0/1) each line gets, or whether
   only the first resource is connected. Microsoft documents MessageNumber for MSI; the line-based case
   with several resources is undocumented. Smallest experiment: ACPI 887 added, KMD accepts it,
   receipt-only ISR counters per MessageNumber and per source (broker status vs inbox control), no
   behaviour change (`Wom1IrqCompletion` absent). Pass: counts of 887 ISR entries ~ RTKit RX messages.
2. Does the firmware raise RX_DOORBELL for every job's completion events on our initdata (Asahi relies on
   it; the stats/ktrace rings also raise it)? The same receipt-only run answers it (ISR count vs jobs).
3. Latency of a routed level IRQ through m1n1 (mask, LR inject, guest ack, unmask = several traps) vs the
   320 us late wake it replaces. Measured by the detect column of `--jobs` in the step-2 run.

## Step 2 as implemented (Wom1IrqCompletion, default off)

- ISR (`AdmissionPlatformMailboxInterrupt`): after moving messages into the ring, `DxgkCbQueueDpc` when the
  switch is on (`DpcQueued`).
- DPC (`AdmissionDdiDpcRoutine` -> `AdmissionPlatformMailboxDpc`): `IrqWakePending = 1`,
  `KeSetEvent(JobPollTimerEvent)` (`DpcWakes`). The same event the predictive timer signals.
- Worker poll loop: for a planner SPIN or SLEEP action with the switch on and the line armed, a pending wake
  re-polls at once (`WaitSkipped`); otherwise `AdmissionJobPollWaitWindows(waitUs)` with waitUs = 500 us for
  a spin step or the planned sleep, on the high-resolution timer; `ExCancelTimer` always after the wait
  (`WaitEarly` = the wake ended it, `WaitTimeout` = the fallback). TICK (> 50 ms) path unchanged. The
  wait is accounted through `AdmissionJobPollNoteSleep`, so `Wom1JobPoll` late/busy stay comparable.
- Without the switch or without the ACPI line: byte-for-byte the EXP2272 behaviour.

## Step 2 result (EXP2273)

Worker busy 47.6 % -> 2.5 %, 57 % of waits ended by the wake, 0.8 % worker self-pops; detect per job unchanged
(0.98 vs 1.02 ms/frame summed) and GM neutral (43.82 vs 43.99; both 887 runs ~1.5 % under the no-887 controls, a
plausible HV interrupt tax of ~40k traps/s). Class C: kept opt-in; not an FPS lever. Open: ABBA on/off in one
boot; DPC wake only when the mailbox ISR requested it; HV trap count per interrupt (ICH_HCR TALL1).

## Checkpoint and recovery

- Step 1 (receipt-only, one Mu build with the flag + KMD package): ISR/DPC counters, `Wom1TranslatedResources`
  with two vectors, suite 119/0, GM HD PERF within the 2 % gate. Failure: boot/PnP code on the device with
  two interrupts -> recovery Mu (flag off) + visible recovery as today; the KMD change is opt-in via the
  resource list (no 887 -> today's behaviour).
- Step 2 (`Wom1IrqCompletion=1`): expected `Wom1JobPoll` late_mean -> ~0 (waits instead of sleeps), detect
  per job 0.04-0.17 -> <0.05 ms, GM HD PERF +1-2 %, CPU busy of the worker well below 49 %. Failure
  criteria: GPU timeout/TDR without a real hang (missed wake), suite failure, perf gate -2 %.
- Rejected alternative: forwarding the RX doorbell as a status bit on the virtual 889 edge from m1n1's
  service step (keeps the HV in the data path and inherits the 5 kHz tick latency).

## Default-on plan (architect spec, 2026-11-09T19:28:41Z)

Correction to "Step 2 result" above: the "~40k traps/s HV interrupt tax" reading is withdrawn (ledger, EXP2273
CORRECTION): the line fires ~390 times/s and the GM difference between the 887 runs and the no-887 controls is inside
run-to-run spread. What stands: worker busy 47.6 % -> 2.5 %, FPS neutral.

Class: **C**. The predictive poll burns half a core for a device that has a completion interrupt; a WDDM miniport is
expected to be interrupt-driven (ISR -> DxgkCbQueueDpc -> DPC). FPS neutrality is therefore not a reason to keep it off
(GPU_DRIVER_ARCHITECTURE.md section 1). Two things separate today's opt-in from a default: the production Mu does not
list the line, and the ISR does not yet honour the level-line contract in every state.

### Contracts

- WINDOWS CONTRACT (FULL GRAPHICS, DxgkDdiInterruptRoutine): return FALSE only when the adapter did not interrupt;
  otherwise dismiss the interrupt on the adapter before returning TRUE; only DxgkCbQueueDpc / DxgkCbNotifyInterrupt
  from the ISR. When dxgkrnl connects and disconnects the ISR relative to Start/Stop/SetPowerState is not documented.
- AGX/ASAHI CONTRACT: ASC mailbox recv-not-empty is a level line (AIC 578 -> vINTID 887) with no enable/ack register in
  the ASC variant (Asahi apple-mailbox: only the M3 variant has irq controls; the ASC line is gated at the AIC). The
  only way to dismiss it is to read the outbox FIFO until EMPTY.
- m1n1: masks the AIC line when it forwards the interrupt and unmasks it on the guest's deactivate. A handler that
  returns without draining gets the same interrupt again immediately.
- TRANSLATION: "dismiss" == "pop until EMPTY". Whenever the line is listed and the outbox can hold a message, some ISR
  path must pop it.

### Defect to fix first (H1, labelled hypothesis: derived from source, never observed on hardware)

Three paths return with the FIFO possibly non-empty:

1. `AdmissionDdiInterruptRoutine` (interrupt.c) returns FALSE before the mailbox handler when
   `InterruptIngressEnabled == 0` (after `AdmissionInterruptStop`, 16 call sites in lifecycle.c) or
   `InterruptReady == 0`.
2. `AdmissionPlatformMailboxInterrupt` returns FALSE when `MailboxIrqArmed == 0` (after the disarm in
   `AdmissionPlatformDestroy`, and in any window where the runtime exists but is not armed) or when the runtime is gone.
3. `AdmissionMailboxDrainFifoAtDirql` stops at a full ring (64) and leaves the message in the FIFO (`RingDrops++`).

In each case a message that is in the FIFO (a late firmware reply, a crashlog/syslog burst, a stalled worker) re-raises
the line at DIRQL without end. With the line absent (today's production Mu) none of this can happen, which is why the
default flip needs it closed first. EXP2272/2273 did not hit it: 0 RingDrops, clean stops.

### KMD change (one commit, class C, no behaviour change while the line is armed and the ring has room)

Introduce one pure decision function in a header that the offline test can compile:

```
AdmissionMailboxIsrAction(LineListed, BaseMapped, Armed, FifoEmpty, RingDepth, RingSlots)
  -> NOT_OURS        LineListed == 0, or BaseMapped == 0, or FifoEmpty
  -> MOVE            Armed, ring has room                 (today's path)
  -> DISCARD_FAULT   Armed, ring full: pop, count RingDrops, set sticky IrqRx.Overflow
  -> DISCARD         not Armed: pop, count IrqRx.Discarded
```

- The top-level ISR calls the mailbox handler **before** the `InterruptReady` / `InterruptIngressEnabled` checks
  whenever `MailboxInterruptVector != 0`; those checks keep gating only the scanout/broker sources.
- The handler loops on the action until NOT_OURS (bounded by a per-call cap, e.g. 256 pops, then return TRUE: the FIFO
  cannot refill faster than it is popped, the cap only bounds one ISR).
- `BaseMapped == 0` is the one state where nothing can be popped. Make it unreachable while the firmware can send:
  keep the transport mapping until the ASC is stopped, and order `AdmissionPlatformDestroy` as: stop the sender
  (existing shutdown), disarm (ISR now discards), then unmap. Record in the note which existing step stops the sender;
  if none does on the surprise-removal path, DISCARD covers it as long as the mapping is still there.
- Sticky `Overflow` is a transport fault: the worker treats it like a lost reply (existing engine-reset path), never
  silently continues. A lost message becomes a recoverable TDR instead of a DIRQL livelock.
- Receipt: `Wom1IrqRx` gains Discarded and Overflow; export it and `Wom1JobPoll` on the throttled
  diagnostics-off path (next to OV2/OV5/PresentTs at backend_platform_windows.c 1500-1584) so a default-on run can be
  judged without `Wom1JobDiagnostics` (which changes timing).
- Default: `runtime->IrqCompletion = !AdmissionPlatformSwitchDisabled(Context, L"Wom1IrqCompletion")`. It stays inert
  without the ACPI line (`AdmissionIrqWaitActive` needs `MailboxIrqArmed`), so the same package runs on the old Mu
  and on recovery unchanged.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH: "the ISR never returns with a listed, mapped, non-empty FIFO" is a
deterministic state machine. Test (new `tests/test_render_irq_quiesce.py`, C model compiled from the header):
enumerate LineListed x BaseMapped x Armed x FifoEmpty x RingDepth in {0, 63, 64}; assert that after the handler loop
the model FIFO is empty in every state with LineListed && BaseMapped, that MOVE is chosen only when armed with room,
and that Overflow is set exactly on DISCARD_FAULT. Update the source-text assertions in
tests/test_render_irq_completion_step1.py (148/150/164) and tests/test_render_job_poll_predicted_sleep.py (320) for
the default flip; grep tests/ for `Wom1IrqCompletion` and `IrqCompletion` before the commit
(test_depth2_defaults.py:89, test_depth2_general_admission.py:125 carry the "default off" comment).

### Mu change

New production candidate = the MUANSCPPC002 source with `J313_AGX_MAILBOX_IRQ = TRUE` (MacBookAirMid2020.dsc:43),
nothing else. Gate before any boot: decompiled DSDT diff against MUANSCPPC002 shows only the added
`Interrupt (ResourceConsumer, Level, ActiveHigh, Exclusive) { 887 }` in the two APPL0002 templates. The recovery Mu
stays MUANSCPPC002 (no line), so recovery always has the poll path.

### Hardware sequence (fork 6; one variable each)

1. IRQD1: new KMD package on the **current** production Mu (no line). Expected: byte-identical behaviour, perf gate
   PASS, suite 119/0, `Wom1IrqRx` absent (the KMD writes `Wom1IrqRx` and `Wom1JobPoll` only when the line is
   listed; corrected 2026-11-09 after the merge 212f09e0e). Proves the default flip is inert without the line.
2. IRQD2: same package on the new Mu candidate, same boot A/B/B/A on `Wom1IrqCompletion` (0 = off arm; warm-up + 2
   measured runs per arm, GM HD PERF and CS 1.6 warm r2/r3). Expected: FPS inside the 2 % gate both ways, worker busy
   ~2.5 % on vs ~48 % off, RingDrops 0, Discarded 0 in steady state, Overflow 0, suite 119/0, 3 driver
   disable/enable cycles and one ordered restart with `BUGCHECK ok code=0x0`, no watchdog (0x133/0x101) stop.
   Failure: any stop, a hang at stop/restart (the H1 signature), perf gate FAIL -> production Mu stays MUANSCPPC002
   and the package default returns to off.
3. On PASS: the candidate becomes the production Mu (working-profile memory + state file updated by the architect),
   architecture 2.1 item moves to DONE.

Not in this change: waking the worker only from the mailbox DPC (dropping the fallback timer) and the launch-path work.

## Implementation notes (offline implementer, 2026-11-09T20:03Z, commit a6916965c on claude/irq-default-on-24672b)

Software only: host tests, no MSVC build, no hardware. H1 stays a labelled hypothesis.

### What was verified about H1

- AGX/ASAHI CONTRACT confirmed from upstream Linux `drivers/soc/apple/mailbox.c` (the local reference tree carries only
  `arch/` and `drivers/gpu/`, so the file was read from torvalds/linux master): `apple_mbox_asc_hw` has
  `.has_irq_controls = false` and no `irq_enable` / `irq_ack` / `irq_bit_*`; only `apple_mbox_m3_hw` has them.
  `apple_mbox_poll_locked` reads I2A_RECV0/RECV1 while `!(i2a_control & control_empty)` and writes `irq_ack` only
  `if (has_irq_controls)`. Registers match ours: I2A_CONTROL 0x114, RECV0 0x830, RECV1 0x838, EMPTY bit 17
  (`J313_AGX_G2_ASC_OUTBOX_*` = 0x8114 / 0x8830 / 0x8838). Linux gates the ASC line only at the irqchip
  (`enable_irq` in `apple_mbox_start`, `disable_irq` in `apple_mbox_stop`, requested `IRQF_NO_AUTOEN`). A WDDM miniport has
  no equivalent: dxgkrnl owns connect/disconnect, so "pop until EMPTY" is the only dismissal available to the KMD.
- Path 1 (gates before the handler) cannot be excluded. Microsoft's DxgkDdiInterruptRoutine / DxgkDdiStartDevice /
  DxgkDdiStopDevice pages say nothing about when the interrupt is connected or disconnected. In our own order
  (`lifecycle.c`) `InterruptReady` is 0 before `AdmissionInterruptStart` and after `AdmissionInterruptStop`, and in both
  windows `PlatformRuntime` is NULL (`AdmissionPlatformRuntimeStart` follows InterruptStart, `AdmissionPlatformRuntimeStop`
  precedes every InterruptStop). So with the gates closed there is no mapping to pop from either: moving the handler
  ahead of the gates is correct and kept, but the state it then meets is "BaseMapped == 0", not DISCARD.
- Path 2 confirmed in source, and found to be coupled to the stop sequence: the old Destroy disarmed first, and the
  sender stop (below) then read its replies from the raw FIFO at PASSIVE while the ISR returned FALSE. An ISR that
  discards when not armed would have stolen those replies, so the order had to change together with the dismissal.
- Path 3 confirmed in source (`RingDrops++; break;` with the message left in the FIFO).
- Which step stops the sender: `AdmissionPlatformDestroy` -> `AppleAgxBackendRuntimeStop` -> firmware cleanup
  (`apple_agx_firmware.c`: StopEndpoint x2, StopAsc, DestroyFirmwareUat, PowerOff) -> `AdmissionFirmwareStopAsc` ->
  `AppleAgxRtkitSessionStop` (AP power 0x10, IOP power 0x10 + ack, then `AppleAgxAscSetRun(FALSE)` clears CPU RUN).
  `AppleAgxFirmwareProviderDestroy` only checks that this already happened. There is no separate surprise-removal
  path: `AdmissionDdiRemoveDevice` calls the same `AdmissionPlatformRuntimeStop`.

### Deviations from the spec, and why

1. Teardown order is "stop the sender (line still armed) -> close -> power off / unmap", with the close in two places:
   `AdmissionFirmwarePowerOff` (before `AppleAgxPowerRelease`) and `AdmissionPlatformDestroy` (before its own release
   and the unmap). Reason: PowerOff follows StopAsc inside the same cleanup, and the ISR is entered for every scanout
   interrupt too (both lines arrive as message 0), where it reads OUTBOX_CTRL. Leaving it armed until Destroy's unmap
   would read ASC registers after the power release. Whether such a read faults is not known; the order avoids it.
2. `BaseMapped` is a new gate `MailboxIsrOpen` (set with the mapping when the line is listed, cleared under both
   interrupt locks by `AdmissionMailboxIsrClose`), not `AscTransport.Base != NULL`: the pointer is never cleared and
   PASSIVE code still needs it after the close.
3. DISCARD is therefore only a transient state (between the disarm and the synchronized close). It is kept as specified.
4. `IrqRx.Overflow` is the sticky receipt flag; the worker acts on a separate one-shot `IrqOverflowLatch`
   (`InterlockedExchange(..., 0)` at the top of the poll loop -> `SchedulerFaulted` + break, as a failed RTKit drain).
   A flag that stays set would fault every job after the reset.
5. The diagnostics-off export of `Wom1IrqRx` + `Wom1JobPoll` happens only when the mailbox line is listed (own 2 s
   throttle, no flush). Without the line the worker does not call the export at all, as before, so IRQD1 has no
   `Wom1IrqRx` value (absent, not zero) and `Wom1JobPoll` still needs `Wom1JobDiagnostics` there.
6. `Wom1IrqRx` is version 3 (two ULONGs appended: Discarded, Overflow; 108 bytes). `tools/agx_irq_rx_decode.py`
   reads v1-v3 and prints a LOST MESSAGES line when RingDrops, Discarded or Overflow is non-zero.
7. The DPC is queued only when the pass moved a message into the ring (not for discards). `IsrEmptyCalls` now also
   counts passes after the close.
8. The worker's synchronized pop (`AdmissionMailboxSyncPop`) uses the same loop, so it also drops at a full ring.

### Residual states the KMD cannot dismiss (BaseMapped == 0 with the line listed)

- No runtime: before `AdmissionPlatformRuntimeStart` maps the SGX region, and after the close. Safe only while the
  ASC cannot send. EXP2272/2273 booted and stopped cleanly in this state, which is the only evidence.
- Stop failures: if `AppleAgxRtkitSessionStop` fails or times out, the cleanup still calls PowerOff, which closes the
  ISR; if `AppleAgxPowerRelease` then fails, the ASC may be running with the ISR closed. Destroy returns
  STATUS_DEVICE_BUSY in both cases. Not changed here.
- Pre-existing, not changed: `AdmissionPlatformRuntimeStop` clears `Context->PlatformRuntime` and frees the runtime
  without the interrupt lock, so an ISR already inside the handler can still read the freed runtime.
- An idle period with more than 64 undrained messages now ends in Overflow and one engine reset at the next job
  instead of a livelock. EXP2272 ring high water was 3.

### Tests

`tests/test_render_irq_quiesce.py` compiles the production `AdmissionDdiInterruptRoutine`,
`AdmissionPlatformMailboxInterrupt`, `AdmissionMailboxServiceFifoAtDirql` and the header against a model FIFO
(clang, -Wall -Wextra -Wconversion -Werror, ASan/UBSan) over 1920 states: line listed x runtime x open x armed x ready x
ingress x switch x ring depth {0, 63, 64} x fill {0, 1, 3, 200, 300}. `test_model_detects_the_h1_defects` puts each
H1 path back and requires the model to fail. Results in this worktree: quiesce 9/9; KMD suites (`test_render_*`,
`test_depth2_*`, `test_fw_rx_*`, `test_notifier_*`, `test_apple_agx_scheduler`) 162 of 163, the one failure being
`test_mu_flag_defaults_off_and_only_adds_the_aslpp_define` (mu submodule not initialised here). The full
`run_against_baseline.py` gives BASELINE_NEW_FAILURES on the parent commit too (56 NEW: pinned Mesa/AIL under
`.local` and mu are missing in this worktree); the NEW set after the change is identical. BASELINE_OK has to be
taken in the GPU worktree after the merge.

## Implementation notes, revision 2: cross-check against the bench session's parked patch (2026-11-09)

The bench session implemented the same spec independently (parked, uncommitted:
`.local/handoff/fork6-irq-default-on.patch`, sha256 3b7b2f1d...48f10). The architect passed on five findings; each was
checked against the source. Where this revision changes the code, it supersedes the notes above (deviation 2 and the
second and third residual states).

1. **"There is no sender-stop step": not confirmed; the spec's order stands.** The comment that patch cites
   ("the retained firmware cannot be stopped and restarted", EXP1241/EXP1278, `backend_platform_windows.c`) is the
   header of `AdmissionPlatformRuntimeQuiesceActive`, the TDR engine reset: there the firmware keeps running, and so
   does the runtime, armed. The teardown is a different path and does stop the sender:
   `AdmissionPlatformDestroy` -> `AppleAgxBackendRuntimeStop` -> `AppleAgxFirmwareRollback` -> StopAsc ->
   `AppleAgxRtkitSessionStop` -> `AppleAgxAscSetRun(FALSE)`, then PowerOff -> broker `HV_AGX_POWER_CMD_OFF` ->
   m1n1 `power_off()` -> `pmgr_adt_power_disable("/arm-io/sgx")` and `("/arm-io/gfx-asc")`
   (`hv_agx_power_broker.c`, `hv_agx_power_platform.c`). The KMD is also what starts the sender
   (`AppleAgxRtkitSessionStartCpuAndInitializeHandoff` sets CPU RUN), so before the first start nothing sends.
   Whether the stop conversation succeeds on hardware with the line listed is not something the source can show;
   EXP2273 stopped cleanly with the old order. That is an IRQD2 observation.
   - Why the adapter-owned mapping (InterruptStart..InterruptStop) was not adopted: it keeps the ISR reading
     OUTBOX_CTRL on every interrupt (scanout included) while gfx-asc is powered off: before PowerOn, and after the
     release until InterruptStop. Whether a read of the disabled domain faults is unknown; the sender is stopped in
     exactly those states, so there is nothing to pop there. The runtime-owned access avoids the read after the
     release. (It does not avoid it before PowerOn: the line is armed when the region is mapped, as in the
     hardware-validated EXP2272/2273 code. The scanout source is masked then; not changed.)
   - What that patch gets right and this revision takes over: the runtime-gone states must not depend on a stop that
     failed. A failed power release now reopens the ISR's access (`AdmissionMailboxIsrReopen`, armed again). By the
     m1n1 source every failed or refused OFF leaves gfx-asc powered (`asc_off` is the last step; a refusal while the
     retained root is Prepared/Active returns BUSY before anything is switched). Destroy then returns
     STATUS_DEVICE_BUSY and the callers keep the runtime, so a still-running ASC always has an armed consumer.
   - Remaining unknown for IRQD2 (disable/enable cycles, ordered restart): the windows with the line connected and no
     runtime (before `AdmissionPlatformRuntimeStart`, after a successful Destroy until dxgkrnl disconnects). The KMD
     cannot pop there. They are safe only because the ASC is not running; when dxgkrnl connects and disconnects
     relative to StartDevice/StopDevice is undocumented. A hang or DPC watchdog at stop/start is the signature.

2. **DPC and ISR lifetime: confirmed, fixed.** `AdmissionPlatformMailboxDpc` and the ISR took the runtime from
   `Context->PlatformRuntime`, which `AdmissionPlatformRuntimeStop` clears and frees with no interrupt lock and no DPC
   flush. Both now use `Context->MailboxIsrRuntime` (replaces the `MailboxIsrOpen` flag): set with the mapping when
   the line is listed, cleared inside `DxgkCbSynchronizeExecution` by `AdmissionMailboxIsrClosed`, and
   `AdmissionPlatformDestroy` calls `KeFlushQueuedDpcs` after the close and the power release, before any unmap,
   only when the line is listed. The create-failure path was checked: when Destroy fails, `RuntimeStart` returns
   STATUS_DEVICE_BUSY without freeing, so the same holds there. The other DPC halves (paging, scheduler, scanout)
   still use `PlatformRuntime`; that is older than this change and not touched.
3. **Export guard literal: already satisfied.** The new term sits before the Ov5 terms;
   `test_ov5_frag_wait_drill` and `test_ov5_ta_waiver` pass.
4. **Diagnostics-off receipt rule: same as the parked patch.** Written only when the line is listed. IRQD1 on
   today's Mu: `Wom1IrqRx` absent, not "present and all zero".
5. **Dependency named, no locking added.** The worker's synchronized pop (`AdmissionMailboxSyncPop`, MessageNumber 0)
   and the ISR exclude each other only if `DxgkCbSynchronizeExecution(…, 0, …)` takes the lock the mailbox line's
   ISR runs under. That rests on the EXP2272/2273 observation that both AGX lines arrive as MessageNumber 0; how
   dxgkrnl maps two line interrupts to interrupt objects is undocumented, and the source shows no way to decide it.
   If it were false, the two 64-bit reads of one message could interleave (`WorkerSyncPopHits` with garbled RTKit
   messages would be the sign). The close synchronizes on both 0 and 1.

Differences that remain, decided from the source:

- **Overflow.** The parked patch fails every outbox control read while `IrqRx.Overflow` is set, until the runtime is
  recreated. The engine reset (`AdmissionPlatformRuntimeQuiesceActive`) does not recreate the runtime, so after one
  overflow every later job would fault again. Here the flag stays sticky in the receipt and the worker faults once
  per overflow episode (`IrqOverflowLatch`).
- **DISCARD.** With the reopen, "not armed but reachable" exists only between the disarm and the synchronized close.
  `Discarded` should stay 0 on hardware; a non-zero value means a message arrived inside that window.
