# J313 AGX G2 Direct Firmware Ownership

## Decision

The Windows AGX driver will own firmware, RTKit endpoints, initdata, UAT,
queues, completion interrupts, faults and reset while the WDDM adapter is
started. m1n1 remains the stage-2 and physical-AIC-to-vGIC boundary. It must
not interpret, copy or forward steady-state submissions. The existing bounded
EL2 power broker may perform protected start and stop transitions, but it is
not part of draw, dispatch, fence or present paths.

This preserves the no-loss performance target: after adapter startup, Windows
communicates directly with AGX MMIO, shared memory and translated interrupts.
USB, Python and synchronous EL2 work remain diagnostics only.

## Starting evidence

EXP-20260826-127 proved the following live Windows boundary on J313:

- one exact translated resource list containing the two required memory
  regions, two system-private descriptors and nine translated interrupts;
- successful resource and platform-state validation;
- successful bounded power sequence ON, QUERY, OFF;
- deliberate fail-closed return at StartDevice stage 7 with
  `STATUS_NOT_SUPPORTED`;
- no firmware, RTKit, SGX MMIO, interrupt, UAT, queue or render action.

The candidate also recorded two `stornvme` Event 129 resets. Consequently,
this design authorizes offline code and tests only. A later hardware step must
have its own preregistration and must preserve a zero-reset storage gate.

## Considered approaches

### Direct Windows ownership — selected

The KMD owns the runtime lifecycle and uses generated, version-pinned AGX and
RTKit structures. This has the highest implementation cost but matches the
native direct-submit architecture and permits full performance.

### EL2 firmware service — rejected for runtime use

Keeping firmware and queues in m1n1 would shorten bring-up, but every resource
and synchronization operation would cross a virtualization boundary. It also
creates split ownership during PnP, TDR and process teardown.

### Captured-command replay — rejected

Replaying a fixed m1n1/Mesa command stream is useful as a diagnostic oracle,
not as a Windows graphics architecture. It cannot safely accept arbitrary
allocations, shaders, processes or presentation work.

## Ownership contract

| Resource | Adapter stopped | Adapter started | Recovery owner |
| --- | --- | --- | --- |
| Physical power domains | off; EL2 broker accepts bounded requests | KMD requests start/stop only | EL2 broker forces reviewed off sequence |
| SGX and ASC MMIO | stage-2 mapped but unused | Windows KMD | Windows KMD until unmap; then EL2 |
| RTKit endpoints 0x20/0x21 | stopped | Windows KMD | KMD reverse-stop with deadline |
| Firmware initdata and channels | absent | KMD-owned nonpaged allocations | KMD invalidates UAT, zeroes and frees |
| UAT context 0 | firmware-private mappings only | Windows KMD | KMD invalidates before firmware stop |
| User/render UAT contexts | absent | Windows VidMm/KMD policy | KMD quarantines then unmaps |
| Nine translated IRQs | disconnected | Windows KMD ISR/DPC | KMD masks, drains and disconnects |
| DCP scanout and GOP framebuffer | existing firmware path | unchanged | unchanged |
| USB/Python diagnostics | optional observer | never required for progress | optional evidence only |

m1n1 and the Windows KMD must never concurrently own firmware, UAT, queues or
interrupt acknowledgement.

## Components

### Generated J313/V13_5 contract

`config/j313-agx-g2.json` remains the human-reviewed source. Its generator will
publish only values proved by the accepted J313 inventory and the pinned m1n1
V13_5 implementation: SGX/ASC subranges, page and address geometry, endpoint
IDs, mailbox layout, firmware-private VA bounds, IRQ roles and deadlines.

No register offset or firmware structure size may be handwritten in the WDK
wrapper. Missing provenance, an unsupported firmware version, an overlapping
range or a contract hash mismatch is a build or StartDevice failure.

### Pure firmware lifecycle core

A freestanding C core will express the ordered state machine without WDK or
hardware dependencies. It consumes explicit callbacks for register access,
mailbox send/receive, monotonic time, memory publication and cache maintenance.
The initial sequence mirrors the pinned m1n1 source:

1. require validated resources and successful bounded power-on;
2. create the firmware-private UAT root and required mappings;
3. boot ASC and negotiate management power state with a deadline;
4. start firmware endpoint `0x20` and doorbell endpoint `0x21`;
5. publish versioned initdata and channel descriptors;
6. send initdata, device-control init and idle-timestamp update;
7. require a bounded heartbeat before entering `FirmwareOwned`.

Every completed phase sets one durable bit. Rollback walks only completed
phases in reverse order and is idempotent. Any timeout, unexpected endpoint,
mailbox value, address, state regression or cleanup failure enters `Failed`;
it never loops indefinitely or reports a started adapter.

### Windows transport

The WDK layer maps the exact translated SGX and ASC ranges through dxgkrnl,
allocates nonpaged DMA-visible memory through reviewed helpers, implements the
pure callbacks and records bounded diagnostics. StartDevice does not advertise
a render node until the heartbeat gate passes. StopDevice, surprise removal
and failed start all use the same reverse teardown routine.

The first hardware package remains qualification-only and still returns
`STATUS_NOT_SUPPORTED` after heartbeat and teardown. It creates no render
context, accepts no user bytes and leaves Basic Display plus DCP scanout intact.

### Interrupt and fault boundary

The firmware-heartbeat milestone may inspect mailbox progress without using it
as a render completion signal. Hardware completion qualification is separate:
translated interrupts are connected only after firmware startup, ISR work is
bounded to acknowledge/snapshot, DPC validates state, and polling cannot make
an interrupt gate pass.

## Memory and security rules

- Use 16 KiB pages and the generated 40-bit GPU address limit.
- Never map arbitrary guest PFNs or context zero for user allocations.
- Firmware-private mappings and future render mappings use disjoint ranges.
- Reject overlap, overflow, misalignment, unknown cache attributes and W+X.
- Guard every allocation and keep an exact inventory for reverse teardown.
- Treat all mailbox, firmware and user-controlled lengths and addresses as
  untrusted before bounds checking.
- Zero and unmap shared control memory before freeing it.

## Deadlines and failure handling

Every power, ASC boot, management-state, endpoint, initdata, heartbeat,
interrupt-drain and stop wait has a generated finite deadline and uses a
monotonic clock. Clock regression is failure. A timeout records the current
phase and last bounded mailbox/fault snapshot, masks GPU interrupts, performs
reverse teardown and leaves the adapter stopped or failed. It must not block
NVMe, input, guest timers or EL2.

## Test strategy

### Offline tests

- literal ordered startup and reverse rollback;
- failure injection after every phase;
- timeout exactly at and one tick beyond every deadline;
- clock regression, duplicate endpoint, stale mailbox and bad heartbeat;
- unknown firmware version and generated-contract mismatch;
- mapping overlap, overflow, context-zero, W+X and cleanup inventory checks;
- idempotent stop after partial start and repeated PnP callbacks;
- source audits proving no USB/proxy or per-command EL2 API is reachable.

Tests must fail before implementation, then pass under the host compiler and
the ARM64 WDK CI package. The canonical repository suite and generated-file
round-trip must also pass.

### Hardware gate

The first hardware experiment changes only the qualification driver against a
manifest-pinned G2 firmware and the exact EXP-123 recovery pair. It permits one
power-on, firmware heartbeat and complete power-off. It forbids UAT render
contexts, queues, commands, shaders and presentation.

Pass requires a responsive eight-core Windows guest, exact phase receipts,
zero critical events, zero Event 129, live AppleInput/NVMe/xHCI, bounded normal
shutdown and a clean non-force rollback. Any violation rejects the run without
retry and does not authorize the next layer.

## Delivery sequence

1. Extend and regenerate the immutable J313/V13_5 firmware contract.
2. Add the pure lifecycle core and exhaustive failure-injection tests.
3. Add the WDK mapping/allocation/mailbox wrapper without enabling hardware.
4. Build and sign a qualification-only package in CI.
5. Preregister one firmware-heartbeat hardware experiment.
6. Only after a clean storage gate, add interrupt-backed fence ownership.
7. Add protected UAT allocations, one queue, TDR and finally rendering in
   separately qualified increments.

## Acceptance boundary

This milestone is complete when Windows can repeatedly start and stop AGX
firmware with a heartbeat and complete teardown while still exposing no render
adapter. It is not graphics acceleration. Acceleration begins only after a
later Windows-owned queue completes an interrupt-backed fence, and desktop
acceleration requires the subsequent UMD and presentation gates.

## Primary sources

- pinned project m1n1 `AGX`, `AGXASC`, management endpoint, UAT, initdata and
  channel implementations;
- accepted J313 G1Q/G1R contracts and EXP-20260826-111 evidence;
- Microsoft WDDM operation flow, render-only feature requirements,
  synchronization and TDR contracts;
- Mesa Asahi architecture documentation and the project-pinned AGX structures.
