# Level-Sensitive Guest Timer Delivery Design

Date: 2026-08-14
Status: Approved for implementation planning

## Context

EXP-017 raised the non-ECV secondary recovery heartbeat from 100 Hz to 1000 Hz.
That reduced long guest stalls, but added roughly seven thousand EL2 entries per
second across the seven secondary CPUs and caused continuous pointer stutter.
EXP-018 restored 100 Hz.  Windows became more responsive, then stopped making
observable progress for at least one minute before recovering.

Two lock-free EXP-018 snapshots showed that physical counters continued to
advance while guest timer INTID 18 list registers remained in Pending-only or
Active-only states.  No list-register shortage or NVMe queue exhaustion was
present.  A diagnostic physical IPI briefly advanced timer IAR/EOI activity.
The recovery heartbeat is therefore masking an event-delivery defect; choosing
another polling frequency is not a correctness fix.

Source inspection found the concrete state-machine error in `src/hv_exc.c`.
When `timer_v_injected[cpu]` is true and the timer LR is Active, the production
path treats the delivery as merely outstanding and never calls the helper that
adds Pending to the live LR.  A level interrupt that reasserts while Active must
become Active+Pending.  `hv_vgic_diag_repend_live_intid()` already implements
that transition and its host test passes, but the production branch ordering
bypasses it.

## Sources Inspected

- Live J313 evidence: EXP-017 and EXP-018 records and the preserved EXP-018
  freeze log and watchdog snapshots.
- m1n1: `src/hv_exc.c`, `src/hv_vgic.c`, `src/hv_vgic_diag.c`,
  `src/hv_tick_policy.c`, and the corresponding host tests.
- Apple interrupt model: the Asahi AIC bring-up description that Apple timers
  and fast IPIs arrive as FIQs through AIC rather than a hardware GIC.
- Arm GICv3 behavior: LR Pending, Active, and Active+Pending state semantics,
  virtual IAR acknowledgement, EOI, priority masking, and `HCR_EL2.VI`.
- Mu and ACPI: no change is required; this defect is below firmware and ACPI in
  m1n1's virtual interrupt controller.
- Windows: no driver change is appropriate.  Windows consumes the architected
  GIC timer contract and cannot repair a lost EL2 level transition.

## Ownership

- The Apple physical timer line and `CNTx_CTL_EL02.ISTATUS` own whether the
  source is asserted.
- The live virtual LR owns guest-visible Pending, Active, and Active+Pending
  state.
- The per-CPU timer queue owns a delivery only while no LR is available.
- `HCR_EL2.VI` is derived from deliverable Pending LR state and guest priority
  masking; it is never an independent interrupt latch.
- `timer_p_injected[]` and `timer_v_injected[]` stop controlling correctness.
  They may remain temporarily as derived diagnostic fields until the watchdog
  schema is migrated.
- m1n1 owns translation, delivery, EOI, and wake.  Mu and Windows remain
  unchanged.

## Unified Level Transition

Both physical timer INTID 17 and virtual timer INTID 18 use one synchronizer.
For an existing live LR, its next state is determined only by the sampled
physical line and current LR state:

| Current LR state | Line asserted | Line deasserted |
| --- | --- | --- |
| Empty | create Pending delivery | Empty |
| Pending | Pending | Empty |
| Active | Active+Pending | Active |
| Active+Pending | Active+Pending | Active |

If the line is asserted with no live LR, the synchronizer injects one Pending
LR or queues exactly one deferred delivery when all LRs are occupied.  Repeated
synchronization must not create duplicates.  If the line deasserts before a
queued delivery reaches an LR, that stale queued delivery is removed.

The synchronizer recomputes `HCR_EL2.VI` after every LR mutation.  A Pending LR
is deliverable only when its group is enabled, its distributor and
redistributor state permit delivery, and its priority passes PMR.  An
Active+Pending LR remains active; EOI converts it to Pending, after which VI is
recomputed immediately.

## Physical Wake

Virtual pending state and physical CPU wake are separate on Apple hardware.
The local timer FIQ already wakes the physical CPU that owns its timer, so the
normal local synchronization path must not send a redundant IPI.  A targeted
physical wake is required only when another CPU makes a previously
non-deliverable timer Pending for a sleeping target.  The implementation must
use the existing race-safe targeted IPI plus SEV notification path and must
edge-limit this notification to the non-deliverable-to-deliverable transition.
It must never introduce a periodic wake or an IPI storm.

The first implementation keeps remote wake behind an explicit result from the
pure transition helper.  If no production timer path performs remote
synchronization, the result is tested but no speculative call site is added.

## EOI and Rearm

Virtual IAR changes Pending to Active and immediately recomputes VI.  Virtual
EOI changes Active to Empty or Active+Pending to Pending, drains one deferred
delivery when an LR becomes free, and immediately recomputes VI.

When Windows writes a future comparator or disables the timer, the next EL2
boundary observes the line deasserted and withdraws stale Pending state while
preserving Active state until EOI.  Rearming must also restore the corresponding
Apple FIQ enable bit.  This prevents both lost ticks and duplicate/stale ticks.

## Diagnostics and Performance

The 100 Hz secondary heartbeat remains during EXP-019 only as bounded recovery
and observation, not as the delivery mechanism.  Runtime diagnostics are
rate-limited and must not add unconditional formatting to the release hot path.
Once event-driven delivery is hardware-validated, a later single-variable
experiment may reduce or disable the recovery heartbeat.

The watchdog snapshot records source assertion, LR state, queue ownership, VI,
IAR, and EOI consistently.  Diagnostic booleans must be derived from LR/queue
state so they cannot disagree with the actual delivery owner.

## Error Handling and Invariants

- At most one live or queued delivery exists per timer INTID and CPU.
- An asserted Active timer is never represented as Active-only after sync.
- A deasserted timer never retains Pending after sync.
- EOI never loses a reassertion: Active+Pending becomes Pending.
- No LR mutation returns to the guest without recomputing VI and issuing the
  required instruction synchronization barrier.
- Queue overflow is recorded and fails visibly in diagnostics; it is not hidden
  by setting an injected boolean.
- The existing external USB input, NVMe, display, Mu, and launch contracts are
  unchanged.

## Test Strategy

Host tests first exercise a pure transition function with literal expected LR
states for every row of the table.  They also cover idempotent assertion,
deassertion of Pending and Active+Pending, EOI preservation, duplicate queue
suppression, stale queued-delivery withdrawal, and the newly-deliverable wake
result.  Each test must be observed failing before production implementation.

The complete m1n1 host suite and focused SMP launch suite then run.  A clean
Mach-O build is recorded as EXP-019 before hardware launch.

## EXP-019 Hardware Checkpoint

EXP-019 changes only event-driven timer/vGIC state synchronization relative to
EXP-018.  It retains the EXP-018 Mu artifact, eight CPUs, 100 Hz secondary
recovery heartbeat, physical display, monitor diagnostics, and assisted launch.
The ESP remains unchanged and EXP-016 remains the recovery artifact.

Acceptance requires:

1. CPUs 0 through 7 enter Windows exactly once.
2. Windows reaches the login or desktop with external USB input and NVMe alive.
3. During boot, ordinary interaction, and a bounded CPU/storage stress period,
   no global pause, `CLOCK_WATCHDOG_TIMEOUT`, reset, or timer-progress loss is
   observed.
4. Timer IAR/EOI counters continue to advance without diagnostic IPIs.
5. No asserted Active-only timer remains after an EL2 synchronization boundary;
   reassertion appears as Active+Pending and EOI returns it to Pending.
6. Responsiveness is no worse than the initially responsive part of EXP-018 and
   does not reproduce EXP-017's continuous micro-stutter.

A single successful boot is insufficient to call the fix stable.  Failure is
recorded without stacking another speculative correction; the next hypothesis
must follow from the new LR/source/VI evidence.
