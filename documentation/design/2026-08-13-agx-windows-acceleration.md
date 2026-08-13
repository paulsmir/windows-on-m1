# Apple AGX Acceleration on Windows: Architecture and Bring-up Gates

## Status

Research and architecture only. No accelerated Windows adapter is implemented
or claimed by this document. Platform stability and native input remain the
prerequisites for hardware implementation.

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

- Capture live SGX, ASC, DART, firmware, interrupt, and reserved-memory nodes.
- Generate one versioned contract consumed by m1n1, ACPI, and Windows.
- Perform no clock, power, MMIO, or DART writes.

### G1: Firmware and reset harness

- Boot or adopt the GPU firmware without exposing a Windows adapter.
- Record firmware heartbeat and faults.
- Prove ten bounded resets without affecting NVMe, USB, input, or DCP scanout.

### G2: Render-only KMD prototype

- Expose no display targets.
- Create one protected GPU address space and one context.
- Submit a fixed, validated no-op and fence operation.
- Reject invalid mappings and commands; demonstrate TDR recovery.

This is the smallest useful Windows milestone. Microsoft documents render-only
devices as a supported WDDM shape, while MCDM is an alternative only if the
initial goal is explicitly compute rather than graphics.

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
