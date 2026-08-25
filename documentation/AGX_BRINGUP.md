# J313 AGX bring-up

This document describes the first hardware-qualified Apple AGX checkpoint for
the J313 MacBook Air.  It is an engineering bring-up boundary, not a Windows
graphics driver and not a performance claim.

## Current checkpoint

G0 records a reviewed, canonical hardware contract from the live Apple Device
Tree.  G1 proves that the matching Apple GPU firmware can be started, contacted,
observed, stopped, and released without submitting render work.

The accepted J313 contract is [`config/j313-agx.json`](../config/j313-agx.json).
It pins:

- J313/T8103 and the V13_5/G13 firmware schema;
- the SGX and gfx-asc MMIO apertures;
- the RTKit shared, private, handoff, and GPU virtual regions;
- all nine SGX interrupts;
- 16 KiB UAT pages, 40-bit GPU virtual addresses, and 64 contexts;
- the exact m1n1, Mu, root, and decoded ADT identities used for review.

The contract is intentionally strict.  Live resource discovery must match it
before clocks or firmware ownership can begin.  Node enumeration order is the
only canonicalized field because ADT traversal order has no hardware meaning.
Interrupt order and every address, size, firmware, platform, and UAT field stay
exact.

## What G1 does

One G1 cycle performs this bounded sequence:

1. validate the live ADT resources against the reviewed contract;
2. select the live V13_5/G13 Construct schema;
3. enable the reviewed AGX clocks and construct the upstream m1n1 AGX stack;
4. initialize context-zero UAT state and start firmware management;
5. require a management Pong inside the fixed one-second deadline;
6. snapshot firmware-owned shared fault state and SGX interrupt counters;
7. stop firmware management and clear both context-zero UAT roots;
8. report release only when the bounded software cleanup completed;
9. physically reboot before another cycle and prove a fresh proxy identity.

The physical reboot is part of the ownership contract.  Management stop and
UAT invalidation quiesce software, but G1 has no proven symmetric pmgr power
reset.  Reusing the same boot caused the next firmware start to time out, so the
gate must not pretend that software cleanup restores cold hardware state.

G1 reads the versioned firmware RegionC fault record.  It does not read the
physical SGX fault register because the later render power domain is
intentionally disabled at this milestone.

## Reproduce the firmware gate

Start from the validated J313 assisted setup with the Air stopped at m1n1
`Running proxy...`.  Use a fresh evidence directory; evidence is never
overwritten or reused.

```sh
cd /path/to/windows-on-m1

./scripts/run-agx-gate.sh \
  --proxy /dev/cu.usbmodem.PROXY \
  --contract config/j313-agx.json \
  --artifact-dir .local/recovery/STABLE-j313-8core-native-input-v1 \
  --evidence-dir investigation/artifacts/EXP-YYYYMMDD-NNN-agx-g1 \
  --cycles 10
```

Replace the proxy device and experiment identifier.  The literal cycle count
must remain ten.  Add `--launch-stable-windows` only when the same registered
experiment also defines post-gate Windows checks.

Before touching hardware, the runner verifies the contract, the complete
immutable recovery manifest, all artifact hashes, repository provenance, and
the absence of another active owner.  During execution, the first lifecycle,
reboot, receipt, identity, or aggregation failure stops the experiment and
blocks Windows.

Verify the atomic aggregate independently:

```sh
./proxyenv/bin/python -m tools.agx_gate verify-result \
  investigation/artifacts/EXP-YYYYMMDD-NNN-agx-g1/gate-result.json
```

A valid result must state all of the following:

```text
gate_version = 2
cold_reset_between_cycles = true
requested_cycles = 10
completed_cycles = 10
verdict = passed
windows_launch_permitted = true
```

Do not retry into the same evidence directory, change the heartbeat deadline
inside an experiment, skip a reboot, or manually edit a result.

## Qualified result

EXP-20260825-078 passed all ten cold-reset firmware cycles.  Every heartbeat
completed in 2.18-2.38 ms, all firmware fault fields and sampled SGX IRQ counts
were zero, and all fresh-proxy receipts proved a changed randomized m1n1 base.
The aggregate SHA-256 is
`d5683820a5efc4d065e98f395f377bc7496f4ffbf91144090fc64373281183e2`.

The unchanged stable guest subsequently reached runtime with eight CPUs, NVMe,
physical xHCI, and an advancing 2560x1600 framebuffer without a bugcheck or AGX
fault.  Windows entered Recovery instead of the lock screen, so the separately
registered post-gate Windows criterion was not accepted and native input was
not requalified in that run.  See
[`investigation/EXPERIMENTS.md`](../investigation/EXPERIMENTS.md) for the exact
attempt history and rejected predecessors.

## Explicit non-goals of G1

G1 provides none of the following:

- a Windows-enumerated graphics adapter;
- a WDDM kernel-mode driver or user-mode driver;
- a render or compute context;
- command queues, work submission, fences, or preemption;
- accelerated desktop composition, Direct3D, OpenGL, OpenCL, or video decode;
- render-domain power management, thermal policy, suspend, or GPU reset.

Windows continues to use the boot framebuffer and Microsoft Basic Display.

## No-loss target architecture

The performance target is a direct Windows driver stack, not per-command
hypervisor emulation:

1. Mu describes the physical AGX device, interrupt, coherent memory, and the
   minimum firmware ownership interface required by Windows.
2. A signed ARM64 WDDM kernel-mode driver owns AGX firmware, UAT address spaces,
   scheduling, fences, interrupts, timeout detection, reset, power, and thermal
   transitions.
3. A matching user-mode driver lowers Direct3D work directly into validated AGX
   command streams and submits them through shared rings.
4. GPU buffers are mapped through hardware UAT with page-level isolation and
   explicit cache maintenance.  Bulk command and resource traffic never crosses
   a Python proxy or an emulated MMIO command interpreter.
5. DCP scanout remains a separate display concern.  The physical panel can keep
   scanning a framebuffer while AGX renders into presentable allocations.
6. The hypervisor is limited to platform boundaries that cannot yet be exposed
   natively; these must not sit in the steady-state draw, dispatch, fence, or
   present path.

This design can approach native hardware throughput.  It does not promise
parity by itself: compiler quality, Windows scheduling, memory residency,
power/thermal policy, and firmware ABI coverage still determine real results.

## Next hardware gate

G2 must define render-domain power ownership and reset before it submits any
work.  The first accepted submission should be a bounded, non-display buffer
operation with:

- a dedicated non-zero UAT context;
- mapped guard pages and explicit read/write permissions;
- one reviewed queue and one fence;
- interrupt-backed completion with a fixed deadline;
- before/after buffer hashes and untouched canaries;
- firmware and physical fault capture while the render domain is powered;
- mandatory reset and cold-reboot recovery on every failure;
- no change to the stable Windows or standalone launch artifacts.

Only after bounded buffer submission and reset recovery are repeatable should
the project expose a Windows WDDM adapter.
