# Apple AGX Acceleration on Windows: Architecture and Bring-up Gates

## Status

Approved architecture on 2026-08-25. No accelerated Windows adapter is
implemented or claimed by this document. The validated J313 eight-core and
native-input checkpoint is the immutable recovery baseline for every GPU
experiment.

The first implementation scope is deliberately limited to G0 and G1: capture a
read-only, versioned J313 AGX resource contract, then prove bounded firmware
start, heartbeat, stop, and reset outside the Windows graphics stack. No AGX
device is published to Windows during these gates.

## Why this is not a Mesa DLL port

The Asahi Mesa driver provides essential public knowledge about AGX shader
compilation, command encoding, image layouts, and synchronization. Its Linux
execution path, however, relies on a DRM kernel driver and Linux memory,
scheduler, fault, and firmware interfaces.

Windows WDDM requires a different operating-system boundary:

- a kernel-mode display miniport (KMD) that integrates with Dxgkrnl, VidMm,
  VidSch, interrupts, faults, reset, power, and the display path;
- one or more user-mode display drivers (UMDs) loaded by the Direct3D runtime;
- GPU virtual-address isolation, allocation residency, fences, scheduling, and
  Timeout Detection and Recovery semantics expected by WDDM;
- an ARM64 package and legal review for every reused component.

The existing physical DCP framebuffer handoff proves scanout, not rendering.
An Indirect Display or display-only driver would improve presentation but would
not accelerate Direct3D workloads.

## Layering

### Firmware and m1n1

m1n1 performs the minimum platform work Windows cannot infer:

1. Inventory SGX/GFX ASC, DART, power, clocks, firmware regions, interrupts,
   and DCP relationships from the live J313 ADT.
2. Preserve or explicitly initialize required firmware and power state.
3. Provide stage-2 access only to the reviewed MMIO and shared-memory ranges.
4. Route bounded physical interrupts with correct level/EOI semantics.
5. Keep DCP scanout independent so a GPU reset cannot remove diagnostic video.

m1n1 must not become a synchronous command proxy for every draw call.

The diagnostic ownership used by G0 and G1 is temporary. Beginning with G2,
the Windows KMD owns runtime queues, address spaces, interrupts, fences, fault
handling, and reset. m1n1 remains outside the submission and presentation hot
paths. This is a hard architectural requirement: the final implementation must
not lose GPU throughput to per-command hypervisor or USB/proxy round trips.

### Windows KMD

The KMD owns the Windows-facing adapter contract:

- adapter discovery and capability reporting;
- per-process GPU address spaces and DART mappings;
- allocations, residency, contexts, command queues, fences, and interrupts;
- command validation and fault containment;
- bounded firmware boot and engine reset;
- WDDM TDR callbacks and crash diagnostics;
- the eventual VidPn/display integration with the existing DCP scanout path.

### Windows UMD

The UMD translates Direct3D runtime state into validated AGX work. The compiler,
layout, and command-stream knowledge should be factored behind a small portable
interface so licensing and Windows ABI work are explicit. A working OpenGL or
Vulkan stack on Linux is not automatically a Direct3D UMD.

## Milestones

### G0: Read-only inventory

- Capture the live `/arm-io/sgx` and `/arm-io/gfx-asc` ADT nodes, their MMIO
  ranges, interrupts, power and clock dependencies, firmware generation,
  RTKit-private region, GPU region, shared region, handoff region, UAT
  geometry, related DART resources, and DCP relationship.
- Serialize the inventory as one schema-versioned JSON contract with canonical
  ordering and a SHA-256 digest. Future m1n1, Mu/ACPI, and Windows components
  consume generated artifacts from this contract instead of duplicating J313
  constants.
- Record the exact root, m1n1, and Mu commits and the source ADT identity in the
  contract.
- Perform no clock, power, MMIO, DART, UAT, or interrupt-controller writes.
- Reject missing, duplicate, overlapping, misaligned, or unsupported resources
  instead of guessing defaults.

G0 is complete only when the same live machine produces a deterministic
contract, a fixture-backed host test rejects every malformed boundary, and a
write-audit proves that inventory performed no hardware mutation.

### G1: Firmware and reset harness

- Run only in the assisted diagnostic workflow. Production and standalone
  profiles remain byte-for-byte unchanged.
- Verify the exact G0 contract and known-good recovery artifact before enabling
  an AGX dependency.
- Enable only the contract-declared SGX/GFX-ASC power and clock dependencies.
- Allocate a private UAT with guard pages and no mapping of arbitrary guest
  physical memory.
- Boot or adopt the GPU firmware without exposing a Windows adapter or creating
  a user render context.
- Prove a bounded firmware heartbeat and management/event-channel progress.
- Record firmware state, pending interrupts, fault registers, UAT summary, and
  every deadline result into a per-run evidence directory.
- Stop firmware, remove mappings, and perform a bounded reset. Repeat the full
  start/heartbeat/stop/reset cycle ten times.
- After the tenth clean cycle, boot the unchanged stable Windows artifact and
  verify CPU, NVMe, USB, native input, and physical DCP scanout.

Every wait has an explicit deadline. A timeout fails closed: save evidence,
attempt one bounded reset, and otherwise reboot into the preserved recovery
artifact. The harness never continues into Windows with AGX ownership or an
unknown reset state.

### G2: Render-only KMD prototype

- Expose no display targets.
- Create one protected GPU address space and one context.
- Submit a fixed, validated no-op and fence operation.
- Reject invalid mappings and commands; demonstrate TDR recovery.

This is the smallest useful Windows milestone. Microsoft documents render-only
devices as a supported WDDM shape, while MCDM is an alternative only if the
initial goal is explicitly compute rather than graphics.

At the G1-to-G2 boundary, ownership is transferred rather than shared. Mu
publishes a generated ACPI device only after the KMD can bind safely. The KMD
then owns the runtime AGX lifecycle directly; m1n1 provides reviewed stage-2
access and interrupt routing but does not interpret or forward submissions.

### G3: First pixels off-screen

- Submit one known shader and render into a private linear allocation.
- Copy the result back and compare it with a golden image.
- Add tiled-layout and cache-maintenance tests before sharing with DCP.

### G4: Direct3D UMD

- Implement the minimum supported Direct3D feature level.
- Compile and submit shaders through the KMD ABI.
- Validate resource lifetime, synchronization, multi-process isolation, and
  malformed application input.

### G5: Accelerated desktop and display

- Integrate allocations and presentation with DWM and DCP scanout.
- Implement mode changes, hot paths, power transitions, and TDR-safe recovery.
- Keep the firmware framebuffer as a fallback until repeated cold boots pass.

## Hard safety rules

- Never map arbitrary guest physical memory into a GPU address space.
- Never trust a user-mode command buffer without bounds and object validation.
- Every firmware wait, fence wait, reset, and power transition has a deadline.
- A GPU fault may fail the adapter or process; it must not hang the hypervisor.
- GPU bring-up stays disabled in production profiles until the current gate is
  recorded on real hardware.
- No external source is copied into this MIT repository without explicit
  license compatibility review.
- Never allow m1n1 and the Windows KMD to own AGX firmware, UAT, or interrupts
  concurrently.
- Keep the physical DCP framebuffer and Basic Display recovery path independent
  until the full WDDM adapter passes its release gate.

## Final performance requirements

The diagnostic G0/G1 harness may be slow because it is outside normal guest
operation. The shipping accelerated path may not be paravirtualized per draw,
queue submission, fence, page fault, or present.

- Windows UMD writes validated command data directly into KMD-managed queues.
- The KMD manages residency and per-process GPU virtual address spaces directly
  through UAT data structures and the firmware ABI.
- Hardware completion and fault interrupts travel directly through the bounded
  physical-AIC-to-vGIC route; USB and proxy transports are diagnostics only.
- Normal rendering requires no host Python process and no synchronous EL2
  request per submission.
- Copies between AGX render targets and DCP scanout are eliminated once shared
  allocations and coherency are proven; any transitional copy path must be
  measured and removed before the final performance gate.
- Power and performance states are driven by measured load and firmware
  telemetry, not held permanently at a diagnostic minimum or maximum.

The performance target is architectural parity with the native direct-submit
model used by Asahi: remaining differences may come from Windows and API
overhead, but not from an avoidable m1n1 proxy, framebuffer copy, or serialized
single-queue design.

## Ownership by gate

| Gate | Initialization | Runtime queues/UAT | IRQ/fault/reset | Display |
| --- | --- | --- | --- | --- |
| G0 | none; read-only inventory | none | none | existing DCP/GOP |
| G1 | assisted m1n1 harness | private diagnostic UAT; no render context | assisted m1n1 harness | existing DCP/GOP |
| G2-G4 | Windows KMD | Windows KMD | Windows KMD through reviewed stage-2/vGIC routes | existing DCP/GOP |
| G5 | Windows KMD | Windows KMD | Windows KMD/TDR | WDDM presentation to DCP, GOP fallback retained |

## G0/G1 verification and recovery

Host tests must cover canonical serialization, schema versioning, required ADT
properties, address alignment, range overlap, interrupt counts, deterministic
hashing, read-only operation auditing, deadline behavior, evidence manifests,
and rejection of an artifact or contract mismatch. Tests are written and
observed failing before implementation.

The real-hardware experiment changes one variable at a time and is entered in
`investigation/EXPERIMENTS.md` before launch. It records exact commits, dirty
diff hashes, commands, artifact paths and SHA-256 values, phase timings, all
ten reset outcomes, and the post-harness Windows health result. The preserved
`j313-8core-native-input-v1` recovery directory is never overwritten.

## Sources inspected for this design

- live-project m1n1 AGX, AGXASC, UAT, context, channel, event, recovery, and
  render experiment implementations at the pinned J313 baseline;
- the J313 Mu DSDT and existing generated-resource pattern used by AppleInput;
- Asahi's AGX kernel-driver architecture notes and upstream m1n1 AGX firmware
  structures;
- Microsoft's WDDM architecture, render-only device model, MCDM guidance,
  display-miniport installation rules, Basic Display behavior, and official
  KMDOD sample;
- the validated J313 eight-core/native-input assisted launch and recovery
  contract in `documentation/STABLE_8CORE_INPUT.md` and EXP-20260825-072.

## Acceptance criteria for claiming acceleration

The project may call the adapter accelerated only after Windows reports a WDDM
adapter instead of Microsoft Basic Display, DWM uses it, representative
Direct3D work executes on AGX, invalid submissions are isolated, and repeated
TDR plus 60-minute stress testing does not regress CPU, NVMe, USB, input, or
physical display stability.

## Primary references

- Mesa Asahi driver documentation: <https://docs.mesa3d.org/drivers/asahi.html>
- Microsoft WDDM architecture: <https://learn.microsoft.com/windows-hardware/drivers/display/windows-vista-and-later-display-driver-model-architecture>
- Microsoft WDDM overview: <https://learn.microsoft.com/windows-hardware/drivers/display/windows-vista-display-driver-model-design-guide>
- Microsoft Compute Driver Model: <https://learn.microsoft.com/windows-hardware/drivers/display/mcdm>
- Microsoft synchronization and TDR: <https://learn.microsoft.com/windows-hardware/drivers/display/thread-synchronization-and-tdr>
