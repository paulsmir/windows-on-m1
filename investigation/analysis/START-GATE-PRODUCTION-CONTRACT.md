# Start gate for a production package: replacing the one-shot arm

Written 2026-11-09T23:04:06Z (architect). Class C (lifecycle contract), no FPS claim. Design only: nothing implemented,
nothing run. Scope: FULL GRAPHICS adapter start/stop, not display or render behaviour.

## Evidence

- EXP2307 part 1 (2026-11-09): after a clean live `pnputil /disable-device` the following enable ended in
  CM_PROB_FAILED_POST_START, `Wom1StartStage=1 Wom1StartStatus=0xC00000BB`,
  `Wom1GpuvaArmPhase=2 Wom1GpuvaArmStatus=0xC0000034`; m1n1 saw no broker call.
- Source, `drivers/apple-agx/render-admission/src/lifecycle.c`: `AdmissionDdiStartDevice` calls
  `AdmissionConsumeGpuvaArm(context, L"G3Armed", TRUE)` before any runtime owner starts. The helper requires the
  device-key value `G3Armed` = 1 (REG_DWORD), deletes it, flushes the key, then reports
  `HV_GPUVA_ARM_CONSUMED` to m1n1. Absent value -> STATUS_NOT_SUPPORTED.
- Source, m1n1 `src/hv_guest_ipa_pa.c` / `hv_exc.c` (closedv5 snapshot): the ARM_CONSUMED hypercall only
  validates the version and prints `HV: GPUVA ARM_CONSUMED`. m1n1 keeps no gate state for it.
- Source, `drivers/apple-agx/windows/scripts/arm-gpuva-experiment.ps1`: the value is written once per staged
  package by the stage script; `remove-experiment-driver.ps1` refuses while an arm remains.

What the arm buys today: an experiment package starts at most once per explicit operator action. After a stop,
a hang or an EL2 reset the next boot finds no arm, the adapter fails closed (Code 43) and the guest stays
reachable for rollback. That is the bench's boot-loop protection.

What it costs a production package: every boot and every StartDevice needs a script. A reboot, a live
disable/enable, a driver update without reboot and a PnP rebalance all end in Code 43.

## WINDOWS CONTRACT

- PnP may stop and start a display adapter several times in one boot (disable/enable, driver update,
  rebalance). dxgkrnl calls DxgkDdiStopDevice and later DxgkDdiAddDevice/DxgkDdiStartDevice again; a driver must
  be able to start after its own clean stop (observed order on this bench: EXP2307 cycle 1).
- A failing DxgkDdiStartDevice leaves the devnode with a problem code and the desktop on the firmware
  framebuffer path (observed: CM_PROB_FAILED_POST_START with the guest alive, EXP2307; the inert recovery
  profile runs that way permanently).
- Windows offers no per-driver "start once" facility; boot-loop protection for a boot-start display driver is the
  driver's or the installer's own business.

## AGX/ASAHI CONTRACT

- Nothing in the hardware needs an arm. Asahi's runtime-PM resume re-runs the RTKit boot on every resume
  (EXP2307B BEFORE cites it); m1n1 accepts a second RR_OPEN after RR_CLOSE by source (EXP2307B measures it).
- The start preconditions that are about the platform stay as they are: `AdmissionG3FirmwareResourcesPresent`
  (the _CRS shape Mu publishes only with a valid R64 receipt) and the system-segment refusal.

## TRANSLATION

A crash latch instead of an arm: armed by default, disarmed by evidence that a start did not survive.

Device-key values (names are new, to be fixed in the header by the implementer):

| Value | Meaning |
| --- | --- |
| `Wom1StartPolicy` | absent or 0 = one-shot arm, exactly today's behaviour; 1 = crash latch |
| `Wom1StartPending` | written and flushed before the first hardware or broker access of a start |
| `Wom1StartLatched` | set when a start finds a stale `Wom1StartPending`; start refused while it is 1 |
| `Wom1StartLatchCount` | number of latch events, never cleared by the driver (receipt) |

State machine under policy 1, evaluated at the point where `G3Armed` is consumed today:

1. `Wom1StartLatched` = 1 -> refuse (STATUS_NOT_SUPPORTED, receipt phase "latched"). Only an operator or the
   installer clears it.
2. `Wom1StartPending` present -> the previous start neither proved healthy nor stopped cleanly: set
   `Wom1StartLatched`, increment the count, delete the pending value, flush, refuse.
3. Otherwise write `Wom1StartPending`, flush, report ARM_CONSUMED as today, continue the start.
4. Clearing `Wom1StartPending` (delete + flush), whichever comes first:
   - StartDevice returns an error (no crash, nothing to protect against);
   - DxgkDdiStopDevice finished with the runtime stopped cleanly;
   - the adapter has been started for T seconds with SchedulerFaulted clear and no reset in progress
     (one-shot timer + passive work item; T = 120 s proposed, a registry-tunable constant).

Consequences:

- A stop, hang or EL2 reset during start or in the first T seconds latches: the next boot fails closed, as the
  bench relies on today.
- A crash after T seconds does not latch. Each following boot then gives at least T seconds of working system
  before the same fault can repeat; a late crash is a driver defect to fix, not a start-gate case.
- A live disable/enable needs no script: the clean stop cleared the pending value.
- A normal shutdown needs no shutdown hook: after T seconds nothing is pending. A shutdown inside the first T
  seconds latches the next boot; `Wom1StartLatchCount` and the receipt make that visible. Accepted for the first
  version; a shutdown-notification clear is an optional refinement once measured.
- Experiment packages keep policy 0, so the bench workflow, the stage scripts and the removal checks do not
  change. A production INF writes policy 1.

Ownership: the latch is KMD state on the device key, same durability mechanism as today's arm (delete/set +
ZwFlushKey). m1n1 and Mu are not involved.

## WHAT IS STILL UNKNOWN

1. Whether a second RR_OPEN + firmware start in one boot works on hardware: EXP2307B answers it. If it fails,
   that defect is fixed first; the latch does not hide it.
2. Whether dxgkrnl calls DxgkDdiStopDevice at system shutdown on this configuration. The design does not depend
   on the answer (rule 4, third bullet); the first hardware run records it from the start-stage receipts.
3. Whether the registry flush before the first hardware access is durable across an EL2 reset on the native SSD
   path (AppleAns). Today's arm relies on the same property and held in EXP2306 (the arm did not reappear), which
   is one observation, not a proof.

## Implementation shape (for the implementer)

- The decision is a pure function of (policy, latched, pending, event): a shared C module with host tests is the
  right place (deterministic state machine: RED/GREEN applies). The Windows part is the three registry
  operations and the timer/work item, with the timer cancelled and flushed in StopDevice before the runtime
  goes away.
- Receipt: extend the existing `Wom1GpuvaArm*` values with the policy, the decision and the latch count; do not
  reuse phase numbers 1-6.
- Tests must cover: stale pending -> latched; error return clears; clean stop clears; healthy timer clears;
  latched refuses without touching pending; policy 0 byte-identical to today (same phases, same statuses).

## Checkpoints

- L0 (class I): package with the module, policy absent. Must equal the accepted package (suite 119/0, perf gate
  PASS, arm receipts unchanged).
- L1: `Wom1StartPolicy` = 1, no arm script. Start at boot, three live disable/enable cycles, ordered restart.
  Expected: three "status OK" without re-arm, `Wom1StartPending` absent T seconds after each start and after each
  stop, `Wom1StartLatchCount` 0, BUGCHECK code 0.
- L2: policy 1, a deliberate stop inside the first T seconds (the DcomLaunch-kill trigger from EXP2304/2305).
  Expected next boot: Code 43 with the "latched" receipt and count 1, guest reachable; clearing the latch value
  and re-enabling starts the adapter. Failure: the adapter starts again after the early stop.
- Recovery for all three: the ordinary recXXXXh/recover.sh; the rollback must also delete the three new values.
