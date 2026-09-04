# J313 AGX G1Q Assisted Queue Qualification

## Status

Approved on 2026-08-25.  G1Q is a diagnostic hardware gate between the
qualified G1 firmware lifecycle and the Windows-owned G2 render-only KMD.  It
does not expose an adapter to Windows, render pixels, or change the stable boot
artifacts.

## Purpose

G1 proved that the J313 AGX firmware can start, answer a management heartbeat,
release its software-owned UAT state, and survive ten cold-reset-separated
lifecycles.  It did not prove that a firmware command queue can consume one
bounded command and report completion.

G1Q closes only that gap.  It qualifies the smallest reviewed queue operation
that can exercise the firmware submission boundary without executing a shader
or touching a display allocation.  G2 remains the first Windows driver gate.

## Architectural boundary

G1Q runs only in assisted mode while m1n1 is stopped at `Running proxy...` and
no guest owns the proxy.  The Python harness may construct and observe the
single diagnostic command because this path is discarded after qualification.
It is forbidden from becoming a Windows submission transport.

Beginning with G2, the Windows KMD owns AGX firmware, UAT contexts, command
queues, completion interrupts, faults, reset, and power transitions.  Normal
draw, dispatch, fence, paging, and present traffic must never cross USB, a
Python process, or a synchronous hypervisor command interface.

## Qualified operation

One G1Q lifecycle performs the following sequence:

1. Validate the exact reviewed J313 AGX contract and immutable stable recovery
   manifest before enabling any AGX dependency.
2. Confirm that no guest runner, standalone launcher, or other proxy owner is
   active.
3. Start the already-qualified G1 firmware stack and require its management
   heartbeat within the G1 deadline.
4. Create one dedicated non-zero UAT context.  Context 63 is reserved for this
   gate.  Its page tables map one gate-owned 16-KiB canary page, use the reviewed
   16-KiB page geometry, and retain one unmapped guard page on both sides.  The
   command does not access the canary page; the mapping independently qualifies
   protected-context construction and teardown.
5. Construct queue index 1's 3D queue, one completion event, and one
   already-satisfied barrier/no-op command using the pinned upstream m1n1
   firmware structures.
   The command contains no shader, render target, display address, arbitrary
   physical address, or guest allocation.
6. Submit the command once.  Success requires both queue-consumer progress and
   the expected firmware completion event before a fixed monotonic deadline.
   Polling may observe evidence but cannot substitute for the completion event.
7. Verify that every guard and canary remains unchanged and that the dedicated
   context contains no mapping outside its declared gate-owned ranges.
8. Capture firmware shared fault state, physical SGX fault state only when its
   power domain is readable, all SGX interrupt counters, queue pointers, event
   identity, stamps, deadlines, and allocation hashes.
9. Stop firmware management, invalidate both roots for context zero and the
   dedicated context, and save the complete cycle receipt atomically.
10. Reboot physically before another lifecycle.  A changed randomized m1n1
    identity is required before the next cycle may start.

The first accepted qualification consists of ten successful lifecycles.  No
cycle may be retried in place and no evidence directory may be reused.

## Completion contract

The gate uses three independent observations:

- the queue `GPU_DONEPTR` reaches the single submitted producer position;
- the allocated event ID is reported exactly once on the firmware event
  channel;
- the barrier stamp and every protected memory canary have the expected final
  value.

A missing, duplicate, late, or spurious event fails the cycle.  Queue progress
without the event also fails because Windows G2 must ultimately complete work
through an interrupt-backed fence path rather than host polling.

The initial completion deadline is 500 ms.  It is a versioned constant in the
gate contract, not a command-line tuning parameter.  Changing it creates a new
experiment and requires a new preregistration.

## Failure and recovery

Every failure is fail-closed:

- stop submitting immediately;
- snapshot all still-readable firmware, queue, UAT, IRQ, fault, deadline, and
  canary state before cleanup;
- perform only the bounded software release already proven by G1;
- mark AGX hardware state unknown;
- physically reboot before any retry or Windows launch;
- never launch Windows from a failed or incomplete G1Q result.

G1Q does not claim a warm GPU reset.  Cold reboot remains mandatory because G1
demonstrated that software cleanup alone does not restore cold hardware state.

## Components

The implementation keeps policy separate from hardware access:

- `tools/agx_queue_gate.py` owns the pure lifecycle state machine, deadline
  checks, receipt schema, aggregation, and verification CLI.
- `tools/agx_m1n1_queue_backend.py` is the only hardware adapter.  It wraps the
  pinned m1n1 AGX context, queue, event, UAT, and fault primitives.
- `scripts/run-agx-queue-gate.sh` verifies repository and recovery provenance,
  enforces sole proxy ownership, and performs cold-reset-separated cycles.
- host tests use a deterministic backend and real receipt serialization; they
  do not mock the state machine whose behavior they assert.
- `investigation/EXPERIMENTS.md` preregisters exact commits, artifact hashes,
  command, deadline, evidence directory, pass criteria, and stop rules before
  hardware access.

The existing G1 implementation is reused as a dependency rather than copied.
Normal assisted Windows launch, standalone launch, Mu firmware, ACPI, and the
stable recovery directory remain unchanged.

## Host acceptance criteria

Before hardware access, automated tests must demonstrate all of the following:

- an exact one-command queue receipt passes;
- missing, duplicate, spurious, or late completion events fail;
- consumer-pointer mismatch and wraparound ambiguity fail;
- context zero, an out-of-range context, unexpected mappings, writable guard
  pages, modified canaries, or undeclared physical ranges fail;
- firmware, physical-fault, IRQ, queue, UAT, and deadline evidence is preserved
  on every failure path;
- cleanup failure blocks reboot continuation and Windows launch permission;
- aggregate verification rejects a changed receipt, reordered cycle, reused
  proxy identity, absent cold reset, or fewer than ten cycles;
- the operator script refuses an active guest and any source, contract,
  artifact, or recovery-manifest mismatch;
- repository tests prove that production and standalone launch defaults are
  unchanged.

Every production behavior is introduced with a failing test, observed RED,
then implemented minimally and observed GREEN.

## Hardware acceptance criteria

The gate passes only when ten cold-reset-separated cycles complete with:

- exactly one submitted barrier/no-op command per cycle;
- exactly one matching completion event per cycle;
- exact queue-consumer progress per cycle;
- zero firmware fault fields, zero nonzero physical AGX faults whenever the
  register is readable, zero unexpected SGX interrupts, guard mutations,
  canary mutations, and undeclared mappings; an unreadable power-gated physical
  fault register must be recorded explicitly and is not treated as zero;
- immutable evidence receipts and a verified aggregate digest;
- a fresh proxy identity after every physical reboot;
- the unchanged stable Windows artifact subsequently reaching runtime with
  eight CPUs, NVMe, physical xHCI, native keyboard and Precision Touchpad, and
  physical DCP scanout.

Passing G1Q proves firmware queue and interrupt-backed completion plumbing.  It
does not prove shader execution, render-domain execution, memory bandwidth,
graphics acceleration, WDDM integration, TDR, or display presentation.

## Rejected alternatives

### Start with the Windows KMD

This preserves the final ownership model but combines PnP, Dxgkrnl DDIs,
memory management, firmware ownership, queue encoding, interrupt delivery, and
TDR in the first hardware submission.  A failure would not identify which
boundary is wrong and could destabilize the known-good Windows installation.

### Submit a captured 3D frame through `GPURenderer`

The pinned renderer expands one frame into TA and 3D work, buffer-manager
state, multiple queues, microsequences, shaders, tiled allocations, and many
versioned firmware structures.  The repository has no reviewed J313 frame
capture.  This is too broad for the first command and cannot provide a clean
failure boundary.

### Treat polling as completion

Polling a queue pointer can prove firmware progress, but it cannot qualify the
interrupt-backed fence contract that WDDM requires.  Polling is therefore
evidence only, never the pass signal.

## Transition to G2

After G1Q passes, G2 begins from the stable Windows branch and adds a
render-only ARM64 WDDM KMD.  The KMD directly owns one protected AGX address
space, one context, one queue, hardware completion, timeout detection, and
reset.  Its first operation remains a fixed no-op/fence.  No display targets or
UMD shader path are exposed until that Windows-owned lifecycle passes repeated
TDR and invalid-submission tests.

## Primary references

- pinned m1n1 `AGX`, `GPUContext`, `GPUWorkQueue`, `GPUCmdQueueChannel`,
  `GPUEventManager`, `WorkCommandBarrier`, and renderer experiments;
- Mesa Asahi driver documentation:
  <https://docs.mesa3d.org/drivers/asahi.html>;
- Microsoft WDDM operation flow:
  <https://learn.microsoft.com/windows-hardware/drivers/display/windows-vista-and-later-display-driver-model-operation-flow>;
- Microsoft WDDM driver and render-only feature requirements:
  <https://learn.microsoft.com/windows-hardware/drivers/display/wddm-driver-and-feature-caps>;
- Microsoft synchronization and TDR guidance:
  <https://learn.microsoft.com/windows-hardware/drivers/display/thread-synchronization-and-tdr>.
