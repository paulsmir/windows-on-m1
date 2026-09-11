# EXP407 Full Graphics mandatory-feature vertical slice

## Decision

EXP406 is a successful structural discriminator and a failed adapter-admission
candidate. It proved this sequence on J313:

`DxgkInitialize -> AddDevice -> StartDevice -> QueryAdapterInfo(Type 1/592,
SUCCESS) -> StopDevice/RemoveDevice -> Code 43`.

The next candidate must not change the 162-field initialization vector and must
not probe individual callback pointers. The failure is the coherent content of
the Type 1 contract. Microsoft classifies the following as mandatory for a
Full Graphics WDDM 1.2-or-newer driver, and WDDM 3.0 inherits those
requirements:

| Feature | EXP406 | Required truthful EXP407 contract |
| --- | --- | --- |
| Modern WDDM identity | pre-Start Type WDDMDEVICECAPS = 3.0; Type 1 modern reserved field = 0 | unchanged |
| Non-VGA PnP start/stop | advertised; POST acquired; stop/release DDI exists | finish black fallback and return exact usable POST information |
| Smooth rotation | not advertised; only identity path accepted | keep unadvertised because this fixed panel exposes no rotated modes |
| GPU preemption | not advertised; callback fail-closed | one-node DMA-buffer-boundary scheduler; pending work can be removed, active work retires before the next packet; valid preemption completion |
| MMIO flip on VSync | not advertised; source-address DDI fail-closed | real DCP Scanout ABI v2 present, exact D589 latch, CRTC_VSYNC notify, and DPC |
| Per-engine TDR | not advertised; callbacks fail-closed | responsive query, one-node reset, coherent last-completed/last-aborted fences, level-zero debug collection |
| DirectFlip | not advertised | one exact compatible full-screen BGRA primary path, KMD validation and matching UMD rejection of every other case |
| GDI kernel command buffer | not advertised; allocation and RenderKm fail-closed | real cache-coherent aperture allocation, bounded supported GDI op set, patch/submission/completion |
| Independent flip | not advertised | advertise only with the same real direct-flip present/source-address/latch path |

`SupportSmoothRotation` remains zero because the candidate advertises only
identity rotation. This is truthful under Microsoft's rotation contract: the
feature is required for drivers that support rotated modes, while EXP407 does
not expose one. `MiscCaps.DisplayableSupport`, hardware flip queue, runtime
power, MPA, and other optional WDDM 3.x capabilities remain zero.

## Primary sources inspected

### Microsoft / pinned WDK

- Pinned `10.0.28000.2526` `d3dkmddi.h`: `DXGK_DRIVERCAPS`,
  `DXGK_PRESENTATIONCAPS`, `DXGK_FLIPCAPS`, `DXGK_VIDSCHCAPS`,
  `D3DKMDT_PREEMPTION_CAPS`, and the exact 1296-byte
  `DRIVER_INITIALIZATION_DATA` layout.
- Microsoft *WDDM driver and feature caps*: all render- and display-specific
  required DDIs for Full Graphics, plus the mandatory WDDM 1.2 feature group.
- Microsoft *GPU preemption*: context allocation, paging context resource,
  MultiEngineAware/PreemptionAware, granularity, replay, and mandatory
  FlipOnVSyncMmIo coupling.
- Microsoft *DXGK_FLIPCAPS*: no-DMA Present, DIRQL source-address programming,
  CRTC_VSYNC interrupt notification and DPC.
- Microsoft *TDR in Windows 8 and later*: engine dependency/status/reset,
  valid aborted-fence interval and level-zero debug collection.
- Microsoft *Direct flip of video memory*: KMD source-address path plus the UMD
  compatibility decision.
- Microsoft *GDI hardware acceleration*: cache-coherent aperture plus
  CreateAllocation, GetStandardAllocationDriverData and RenderKm.
- Microsoft *PnP start and stop*: POST ownership, initial blanking, black
  fallback, and accurate release information.
- Microsoft RosKmd Full Graphics vector and Type 1 construction. RosKmd is a
  structural/contract reference only; its broad capability assignments are not
  copied when the corresponding Apple path is not implemented.

### Asahi Linux

- `drivers/gpu/drm/asahi/mmu.rs`, `pgtable.rs`, `vm/`, `alloc.rs`, and
  `gem.rs`: 16 KiB UAT leaves, VM ownership, guarded mappings and object
  lifetimes.
- `drivers/gpu/drm/asahi/fw/workqueue.rs`, `fw/event.rs`, `event.rs`,
  `queue/render.rs`, `workqueue.rs`, and `gpu.rs`: queue publication, event
  stamps, completion, faults and recovery ordering.
- `drivers/gpu/drm/asahi/fw/vertex.rs` and `fw/fragment.rs`: distinct TA and 3D
  work records and completion semantics.

These files define observable hardware sequencing. Their code is not copied.

### m1n1

- `src/dcp.c`, `dcp_iomfb_bootstrap.c`, `dcp_iomfb_present.c`,
  `dcp_iomfb_latch.c`, and `display.c`: single IOMFB owner, canonical
  A407/A408 and exact D589 latch.
- `src/hv_agx_scanout_broker.c`, `hv_agx_scanout_service.c`, and
  `hv_agx_power_mmio.c`: registered pool, bounded present, latch/error status,
  write-one-to-clear acknowledgement, and synthetic INTID 889.
- `proxyclient/m1n1/agx/`: T8103 v13.5 UAT, RTKit, queue, event and work-command
  reference behavior.

The current m1n1 tree differs from hardware-proven EXP270 in the relevant
retained-owner/scanout sources only by replacing physical completion routing
with EXP406 synthetic INTID 889. The full-owner code itself is unchanged.

### This project

- EXP208 materialized G13/v13.5 graph and generated relocation template.
- `shared` UAT, residency, local segment, event allocator, G13 codec/queue,
  backend runtime, fixed-panel, scanout, scheduler, GDI, completion and recovery
  components.
- `windows` allocation, segment, context, paging, scheduler, GDI, scanout and
  platform-provider wrappers.
- Separate `render-admission` project and EXP406 lifecycle/receipts.

Existing components are reused only after their preconditions match this
contract. The separate project remains the admission vehicle; this design does
not switch back to the accumulated `AppleAgx` package.

## Ownership

| Concern | Owner in EXP407 |
| --- | --- |
| WDDM ABI, objects, caps, allocations, contexts, scheduling and fences | Windows `render-admission` KMD |
| UMD resource compatibility for the one direct-flip case | `render-admission` UMD |
| Synthetic interrupt resource and APPL0002 memory publication | candidate Mu profile |
| DCP RTKit, one IOMFB endpoint, DART scanout mapping, A407/A408, D589 and INTID 889 injection | current candidate m1n1 |
| AGX power, ASC/RTKit, UAT context, TA/3D queues and event stamps | existing project backend, called by KMD only after Windows memory/context admission |
| Physical AGX interrupts 880--888 | masked and unpublished until a later separately derived AGX completion mapping requires one |
| Recovery | current-compatible non-AGX Mu for exact package removal, then ordinary current G2 |

## Windows-to-AGX translation

### Allocation and memory

- Segment 1 is a cache-coherent, CPU-visible 4 KiB PFN aperture/staging segment
  used for GDI-visible system allocations.
- Segment 2 is contiguous J313 memory used for local primaries, the immutable
  EXP208 command arena, firmware objects and AGX-visible backing.
- AGX UAT uses 16 KiB leaves. Windows segment offsets are translated through
  one checked local-segment mapping; a CPU physical address is never treated as
  an AGX virtual address.
- Type34 remains the 40-bit hardware-visible ceiling. Type35 remains zero until
  a Windows IOMMU remapping DDI exists.

### Device and context

- Exactly one WDDM engine/node exists. Device/context handles own explicit
  nonpaged objects and reference only admitted allocations.
- Context creation allocates/publishes one AGX UAT context and the corresponding
  Windows context resource. Destruction is blocked while work is outstanding.
- Invalid flags, ordinals, affinities, handles, ranges and alignments fail before
  hardware mutation.

### Render, scheduler and preemption

- RenderKm accepts only the bounded operation set implemented by the existing
  GDI translator. It emits immutable driver-private command data and patch
  locations; unsupported ROPs fail before submission.
- BuildPagingBuffer implements only the operations required by admitted
  segments and context resources. Replay preserves paging fence IDs.
- SubmitCommand enqueues one immutable packet and monotonically assigns the
  dxgkrnl fence to the existing backend submission.
- Preemption granularity is DMA-buffer boundary. A packet not yet published to
  AGX is aborted; an already published packet completes normally and blocks
  later publication. The completion reported to dxgkrnl is derived from the
  queue/event stamp, never from enqueue alone.
- Per-engine reset returns an aborted fence inside dxgkrnl's submitted/completed
  interval and rebuilds only the one logical engine after a proven backend
  quiesce. If quiesce is not proven, reset fails and adapter-wide recovery owns
  the fallback.

### Presentation

- StartDevice requires current m1n1 Scanout ABI v2 and the latched-receipt
  capability before returning one source/target.
- Primary allocations are exact 2560x1600 BGRA8888 with stride 10240 from the
  registered contiguous pool. No arbitrary system allocation is directly
  scanned out.
- Present produces no DMA buffer. SetVidPnSourceAddress, callable at DIRQL,
  validates a precomputed allocation/offset tuple and posts a bounded broker
  request without waiting.
- m1n1 performs DART mapping and A407/A408. Only exact D589 equality sets the
  latched status and injects 889. ISR reads owned status, acknowledges it and
  sends `DXGK_INTERRUPT_CRTC_VSYNC` for the recorded source/physical address;
  DPC calls dxgkrnl only for that real completion.
- DirectFlip and FlipIndependent are limited to the exact primary format,
  dimensions, stride, segment and modifier. The UMD rejects all other
  candidates.
- Stop/release first blocks new presents, masks 889, waits only at PASSIVE_LEVEL
  for a bounded final receipt, presents a black fallback, releases the pool,
  and returns accurate POST information.

## Capability publication rule

All mandatory Type 1 bits are derived from one immutable `FEATURE_READY`
state established in StartDevice. `FEATURE_READY` is true only after memory,
scheduler, TDR, UMD package identity and Scanout ABI v2/latch capabilities have
all passed. QueryAdapterInfo never publishes a partial mandatory group.

The group contains only:

- `GpuEngineTopology.NbAsymetricProcessingNodes = 1`;
- `SchedulingCaps.MultiEngineAware = 1`;
- the selected preemption policy and DMA-buffer-boundary graphics granularity;
- `FlipCaps.FlipOnVSyncMmIo = 1` and `FlipIndependent = 1`;
- `SupportNonVGA = 1`;
- `SupportPerEngineTDR = 1`;
- `SupportDirectFlip = 1`;
- `PresentationCaps.SupportKernelModeCommandBuffer = 1`;
- exact non-capability limits such as `HighestAcceptableAddress` and allocation
  slots.

No other feature bit is enabled by this milestone.

## Implementation sequencing

This is one architectural vertical slice but not one unreviewable patch. It is
implemented and committed in deterministic offline layers:

1. Pure contract/state machine for allocation, context, fence, preemption and
   reset with host tests.
2. KMD object wrappers and segment/paging validation with host/static tests.
3. GDI operation admission and immutable command encoding.
4. Current-source full-owner m1n1 build and Scanout ABI v2 contract; no new DCP
   protocol or ownership model.
5. KMD scanout/ISR/DPC and exact primary address translation.
6. Narrow UMD direct-flip compatibility path.
7. Existing AGX backend integration using the EXP208 template, initially one
   submit and then repeated submits.
8. Atomic Type 1 capability enablement only after all preceding gates are
   green.

No hardware bind occurs between these steps. The first hardware candidate is
the integrated slice, with receipts at every Windows callback and backend phase
so it stops at the first real boundary rather than hiding it.

## Offline and hardware gates

Offline gates require:

- deterministic RED/GREEN tests for object lifetime, range/alignment, paging,
  replay, fence monotonicity, preemption/reset intervals, scanout request/latch
  ordering and UMD compatibility;
- all existing shared, Windows, render-admission, m1n1 scanout and Mu profile
  suites green;
- pinned-WDK ARM64 Release KMD+UMD build, code analysis, Universal validation,
  Inf2Cat, signing and exact hashes;
- current-source full-owner m1n1 build proving only synthetic 889 for Windows;
- candidate Mu AML still exposing exactly one edge 889 and no 880--888.

Before hardware, the experiment ledger must contain `WHY THIS HYPOTHESIS`, the
atomic Type 1 group, exact commits, dirty hashes, commands, artifacts and
recovery hashes.

Hardware PASS requires:

1. full-owner m1n1 reports Scanout ABI v2;
2. Windows reaches SSH with eight CPUs;
3. natural exact-package bind reaches Type1, Type34 and Type35;
4. GPU LUID, one child and one VidPn appear with Code 0;
5. one Windows primary is allocated and one Windows-driven flip produces
   A407/A408 plus exact D589 and CRTC_VSYNC/DPC receipts;
6. one bounded AGX submission produces TA, 3D, hardware event/stamp completion
   and the matching Windows fence;
7. ten additional submissions complete with monotonically increasing fences;
8. no physical 880--888 route is enabled, no TDR, watchdog, Event129, bugcheck,
   reset, corruption or loss of SSH/input/xHCI/storage occurs.

Any earlier honest callback/backend failure is the next boundary and ends the
candidate. Evidence is collected before exact rollback. The package is always
removed under current-compatible non-AGX Mu and ordinary current G2 is restored.

## Self-review

- This design keeps the approved separate `render-admission` project and WDDM
  3.0 model.
- It does not advertise preemption, flip, TDR, DirectFlip or GDI capability
  before the matching implementation exists.
- It reuses the hardware-proven DCP owner and EXP208 backend pieces instead of
  rediscovering protocols.
- It does not expose physical AGX interrupts or introduce a second DCP owner.
- It explains why EXP406 cannot progress through another admission-only caps
  edit: the next Windows requirement is an indivisible implementation group.
- No new architectural choice remains that requires human approval under the
  user's approved sequencing.
