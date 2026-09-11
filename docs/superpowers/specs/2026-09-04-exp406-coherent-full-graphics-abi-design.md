# EXP406 Coherent Full Graphics ABI Admission Design

Date: 2026-09-04

Status: approved sequencing; offline reconstruction complete

## Goal

Advance the exact EXP405 boundary from `DxgkInitialize ->
STATUS_REVISION_MISMATCH` to the next naturally selected Full Graphics WDDM
3.0 boundary. EXP406 is an ABI and admission experiment, not a render-backend
experiment. It must keep unneeded physical AGX interrupt sources masked and
must not start AGX RTKit, create UAT tables, create AGX queues, or submit work.

## Sources Inspected

- Live J313 current-G2 baseline and EXP405 evidence in
  `investigation/EXPERIMENTS.md`: Code28/unbound after cleanup, eight CPUs,
  healthy SSH/input/NVMe/xHCI, no boot 41/129/1001.
- Byte-exact EXP214 source under
  `.local/experiments/EXP-20260904-398-exp214-control/source-exact/`.
- Pinned WDK/SDK `10.0.28000.2526` ARM64 headers copied without modification
  under `.local/experiments/EXP-20260904-406-coherent-abi-admission/`.
  `DXGKDDI_INTERFACE_VERSION_WDDM3_0` is `0xF003`; the compiled
  `DRIVER_INITIALIZATION_DATA` contract is 1296 bytes.
- Microsoft `graphics-driver-samples` commit
  `de4a2161991eda254013da6c18226f5ea06e4a9c`, specifically RosKmd's full
  initialization vector.
- Asahi Linux commit `77cb8f24c2381a8abb7272d7bbdec548d6426a8a`:
  T8103 exposes the GPU through the ASC mailbox and names physical 578 as the
  receive-not-empty mailbox interrupt; the DRM driver consumes firmware events
  through mailbox messages and event stamps rather than treating every raw SGX
  line as a generic completion source.
- Current m1n1 `hv_irq_routes.c`, `hv_vgic.c`,
  `hv_agx_power_mmio.c`, and `hv_agx_scanout_broker.c`.
- Current Mu `J313AppleAgx.asl.inc`, which publishes all nine physical AGX
  routes, and current render-admission driver sources.

The exact per-field comparison is
`investigation/EXP406_FULL_GRAPHICS_ABI_MATRIX.csv`.

## Structural Finding

EXP214 and EXP405 share the WDDM 3.0 version, 1296-byte structure, lifecycle,
render, allocation, context, paging, timeout and fence entry groups. EXP405
made one atomic structural change relative to the accepted control:

1. removed `DxgkDdiInterruptRoutine`, `DxgkDdiDpcRoutine`, and
   `DxgkDdiControlInterrupt`;
2. added the Full Graphics display branch but omitted `DxgkDdiSetPalette` and
   `DxgkDdiGetScanLine`.

RosKmd registers all five entries in the corresponding base/full-display
vector. EXP406 restores that exact structural shape as one invariant. It does
not infer that the presence of a pointer proves that the underlying feature is
implemented.

All WDDM 3.0 tail members that EXP214 and RosKmd leave unset remain zero.
Reserved, Reserved1, Reserved2, Reserved3 and the reserved SetPowerPState slot
remain zero. No capability bit is enabled solely to pass initialization.

## EXP406 Windows Contract

### Initialization vector

- `Version = DXGKDDI_INTERFACE_VERSION_WDDM3_0` and
  `sizeof(DRIVER_INITIALIZATION_DATA) == 1296` remain compile-time invariants.
- Preserve every EXP214 entry.
- Preserve EXP405's one-panel display/VidPn group.
- Add `SetPalette` and `GetScanLine` with validated arguments, an in-memory
  receipt, and an honest error result because the only admitted mode is
  32-bpp and no scanline counter is implemented.
- Restore ISR, DPC and ControlInterrupt only with the interrupt ownership
  contract below.
- Change the current `Present` false-success path to fail closed.
  Allocation/device/context/paging/render/submit callbacks remain registered
  but return an allowed failure until they become the next invoked boundary.

### Lifecycle and adapter information

- AddDevice allocates one nonpaged context and records entry/success.
- StartDevice copies the interface, gets translated resources, maps only the
  existing 4-KiB m1n1 broker page, validates the synthetic scanout interrupt
  resource, acquires POST ownership, and publishes one source and one internal
  child.
- Type 34 remains the 40-bit highest visible physical address.
- Type 35 remains zero IOMMU capability; no Windows or Apple UAT claim is made.
- The one fixed 2560x1600x10240 BGRA source/target and VidPn contract remains.

## Minimal Interrupt Contract

The EXP404 result prohibits connecting the raw level AGX routes to a driver
that cannot identify and dismiss them. EXP406 therefore uses one new
candidate-only Mu profile:

- it publishes the existing APPL0002 memory resources;
- it does not publish guest 880--888 physical AGX routes;
- it publishes exactly one exclusive edge-triggered synthetic scanout
  interrupt, guest INTID 889;
- current normal G2 and non-AGX profiles remain byte-behaviorally unchanged.

m1n1 already owns the scanout broker source. EXP406 changes that source from
the physical-route alias 887 to synthetic-only INTID 889. It injects 889 only
when `IRQ_STATUS & IRQ_ENABLE` becomes nonzero. No physical AIC route is
registered for 889, so a guest GIC enable cannot unmask an AGX line.

The driver ISR is nonpaged and bounded. It accepts only message number zero,
reads broker `IRQ_STATUS`, returns FALSE when no owned bit is set, clears the
owned bits by writing the observed value back to `IRQ_STATUS`, records only
interlocked in-memory counters, and returns TRUE. It performs no registry,
file, allocation, logging, wait, power, RTKit, UAT, queue, DCP, or pageable
operation. ControlInterrupt may enable or disable only the two documented
broker bits; every other interrupt type fails. The DPC callback is registered
for structural completeness but is queued only after a real handled source;
EXP406 has no consumer requiring a DPC, so the ISR does not queue it.

At StartDevice the broker interrupt-enable register is explicitly written
zero before the adapter is marked ready. StopDevice first writes zero, then
uses `DxgkCbSynchronizeExecution` when available to close ISR ingress, and only
then unmaps the broker page. This is the quiesce/ack/teardown invariant.

## Ownership

- Mu owns publication of the candidate APPL0002 resources and synthetic INTID.
- m1n1 owns the synthetic scanout source, pending-edge generation, and broker
  MMIO state.
- Dxgkrnl owns interrupt connection and DPC dispatch.
- render-admission owns source identification, broker status/ack, mask state,
  and nonpaged receipts.
- AGX power, ASC firmware, RTKit, UAT, queues, execution and completion remain
  unowned and inactive in EXP406.

## Hardware Experiment

Use the same no-`/install` two-phase flow as EXP405:

1. verify clean normal-G2 Code28 baseline;
2. boot current-compatible non-AGX, stage exact EXP406, and hold 180 seconds;
3. arm, shut down, and boot accepted m1n1 plus the EXP406 candidate Mu profile;
4. allow only natural APPL0002 selection;
5. require no enable of physical AGX routes 880--888 and observe synthetic 889;
6. collect the first callback and PnP boundary before cleanup;
7. require a separate 180-second health window for Code0;
8. clean exact package under non-AGX and restore ordinary current G2.

PASS is `DxgkInitialize` success followed by AddDevice, StartDevice, Type34,
Type35, the one-panel topology/VidPn group, a GPU LUID, and a stable system
past the watchdog interval. A render callback is not required for PASS. If an
allocation/device/context/paging/render callback is invoked, its honest return
and receipt become the next boundary.

## Recovery and Next Layer

The immutable current-compatible non-AGX Mu and accepted EXP377 m1n1 remain the
recovery pair. EXP406 package state is experiment-local and is always removed
after evidence collection.

After a passing Full Graphics admission, immediately use the first invoked
render-side callback to select the next coherent group: allocation, device and
context, then memory/paging and HVC/UAT as required, then the existing AGX
queue/submission backend, completion and Windows fences. No existing backend
piece is recreated merely because render-admission is separate.
