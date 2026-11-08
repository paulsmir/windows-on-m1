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
