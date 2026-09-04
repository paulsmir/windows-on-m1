# EXP407 live readiness matrix

Updated: 2026-09-04T17:51:22+02:00

This matrix is evaluated after memory source commits
`8cd1449550b253862a3b770b9782b7b7fb2f776e`,
`338043409a3306fb594a147dde0ddd309015c9ae`, and
`39f64c6c1312adb554d6c86745cd387a6af2e491`, followed by atomic Type1
wiring `52d3bf6182e6ae60e4a3520b68d857bdc7eb5167`. `READY=yes` means an
operational contract exists; callback registration, a success return, a data
structure, or a source-presence test alone is insufficient.

Readiness has two independent axes:

- `MEMORY_PAGING_IMPLEMENTED = YES`: the complete production source contract
  is linked and offline-green, so the functional 14-bit implementation gate
  may count this bit.
- `MEMORY_PAGING_HW_PROVEN = YES`: EXP412 produced the complete 128-byte
  stage-10 proof for the live chain
  Windows KMD -> HVC `0x4d31` -> current m1n1 EL2 -> host PA -> context-63 UAT
  publication/readback -> cleanup. This does not claim that dxgkrnl separately
  exercised BuildPagingBuffer or its DMA completion during the qualification
  branch.

## Fourteen prerequisites

| Feature | Implementation | Evidence | Dependencies | Ready |
| --- | --- | --- | --- | --- |
| `WDDM3_IDENTITY` | Exact 1296-byte WDDM 3.0 initialization vector and pre-Start WDDMDEVICECAPS 3.0 response | EXP406: `DxgkInitialize=SUCCESS`, Add/Start reached, Type1/592 called successfully | Pinned WDK 10.0.28000.2526 | yes |
| `ONE_NODE_TOPOLOGY` | Type1 names one asymmetric node; GetNodeMetadata describes ordinal 0 as 3D; adapter/context scheduler lifetime owns exactly node 0 and engine 0 | Commit `3df8e82`; focused scheduler tests and pinned-WDK build | Render submission/progress remains the next SCHEDULER dependency | yes |
| `MEMORY_PAGING` | Complete production chain: Segment1/2, allocation backing/lifetime, exact contiguous DXGK object/ADL/map, bounded HVC 0x4d31, 40-bit host pages, context-63 16-KiB UAT publication, BuildPagingBuffer encode/execute, bounded paging worker, synchronized DMA completion/fault and reverse cleanup; local geometry is exact 56-MiB scanout plus 8-MiB backend | Commits `8cd1449`, `3380434`, `39f64c6`, `4779f02`, and `8974048`; EXP412 16-MiB PASS and EXP425 64-MiB stage-10 PASS; pinned-WDK builds zero warnings/errors | `IMPLEMENTED=yes`; `HW_PROVEN=yes` for exact 64-MiB object; BuildPagingBuffer/DMA completion still need Windows-driven exercise | yes |
| `DEVICE_CONTEXT` | Typed nonpaged adapter/device/context ownership, bounded counts, node 0/affinity 1 and busy destruction | Commit `7b0c771`; RED/GREEN object tests; pinned-WDK KMD/UMD build with zero warnings/errors | Allocation references and scheduler attachment remain later layers | yes |
| `SCHEDULER` | One-node lifetime and context attachment share one monotonic queued/active/completed fence interval; the production passive worker activates the exact packet and only the provider completion transaction advances it | Commits `3df8e82`, `4d539ea`, and `3a4b55e`; 39-test gate and EXP423 WDK build | `IMPLEMENTED=yes`; `HW_PROVEN=no` | yes |
| `DMA_BOUNDARY_PREEMPTION` | Exact scheduler snapshot cancels queued unpublished work, blocks dispatch, waits for the active provider boundary and permits one DMA_PREEMPTED claim/commit after the same completion path | Commits `421a1ac` and `3a4b55e`; deterministic tests and EXP423 WDK build | `IMPLEMENTED=yes`; `HW_PROVEN=no`; hardware boundary evidence remains absent | yes |
| `PER_ENGINE_TDR` | QueryEngineStatus uses provider progress age; PASSIVE ResetEngine drains the worker, quiesces and retires the exact active fence without DMA completion, reports LastAbortedFenceId and recreates provider/backend before success | Commits `3df8e82`, `ec214ae`, and `660c187`; focused tests and EXP424 WDK build | `IMPLEMENTED=yes`; `HW_PROVEN=no` | yes |
| `GDI_COMMAND_BUFFER` | One exact ColorFill/PATCOPY is normalized into a pointer-free record; Patch resolves one Segment-2 CPU/host-PA/GPU-VA tuple and seals the exact fence; SubmitCommand binds object 40, queues and schedules the exact provider job | Commits `2ee3398`, `7912547`, `b6ee1a6`, `eead97f`, and `3a4b55e`; EXP415/416/419/421/423 WDK builds | `IMPLEMENTED=yes`; `HW_PROVEN=no` | yes |
| `D589_SCANOUT` | `render-admission` registers only the EXP425-proven 56-MiB pool after POST ownership and requires current m1n1 ABI v2 plus repeated-present latched-receipt and latched-IRQ caps; exact primary offsets enter the existing A407/A408/D589 path | Commit `230a99a`; shared sanitizer suites; EXP426 pinned-WDK build; EXP270 separately proved the retained-owner D589 source | `IMPLEMENTED=yes`; integrated KMD hardware path `HW_PROVEN=no`; current-source full-owner artifact still required | yes |
| `KMD_DIRECT_FLIP` | Present validates only one NULL-DMA full-screen A8R8G8B8 primary; SetVidPnSourceAddress validates the same allocation and Segment-2 range then performs bounded nonblocking MMIO enqueue; matching latch ISR reports exact CRTC_VSYNC and DPC | Commit `230a99a`; verified RED/GREEN wiring/IRQL tests and EXP426 pinned-WDK build | `IMPLEMENTED=yes`; `HW_PROVEN=no`; Type1 remains zero until UMD/independent flip complete | yes |
| `UMD_DIRECT_FLIP` | ARM64 UMD supplies a real `OpenAdapter10_2` WDDM1.3 adapter/device, exact resource create/open identity, flags-zero `CheckDirectFlipSupport`, SetDisplayMode/rotation and interval-one Present/Present1 callback path; 3D caps remain zero | Commit `7cf5495`; sanitizer-backed exact-pair predicate; EXP428 WDK/PE/export gates | `IMPLEMENTED=yes`; Windows load/callback path `HW_PROVEN=no` | yes |
| `INDEPENDENT_FLIP` | Atomic Type1 advertises `FlipIndependent` only with the same exact KMD+UMD primary compatibility, one-pending broker queue and D589-derived VSync completion; immediate flip remains unsupported | Commits `230a99a`, `7cf5495`, and `6c96d53`; exact writer tests and EXP429 build | `IMPLEMENTED=yes`; `HW_PROVEN=no` | yes |
| `NON_VGA_STOP` | Stop closes new presents and synchronously RELEASEs the broker; current m1n1 re-presents and latches the saved firmware POST surface before unmapping the Windows pool, after which KMD returns the exact saved POST information | Commit `230a99a`; current m1n1 `display_scanout_quiesce_*` plus broker release tests; EXP426 pinned-WDK build | `IMPLEMENTED=yes`; integrated stop `HW_PROVEN=no` | yes |
| `AGX_COMPLETION` | Production borrows the upper tail and context 63, materializes/rebases/binds EXP208, uses the existing G13 provider for D3 then TA publication, polls the event ring and requires both matching event/stamp/done-pointer observations before exact DMA_COMPLETED and DPC | Commits `68172a3`, `45969de`, `abe363f`, `eead97f`, `4285cef`, `f9ad365`, `35f5a68`, `c932a36`, and `3a4b55e`; EXP423 39-test and WDK gates | `IMPLEMENTED=yes`; `HW_PROVEN=no`; no physical AGX IRQ is used | yes |

Current functional implementation result: `14/14 READY`; this is not a count of
hardware-proven layers. Commit `6c96d53` publishes the approved mandatory Type1
group only when the portable evaluator sees exactly all fourteen bits after a
complete StartDevice. Every incomplete mask still returns a successful but
fully zero mandatory group. This is implementation readiness, not evidence that
Windows admitted the vector or exercised UMD, scanout, render, preemption or TDR.

## Existing allocation and memory implementation inventory

### REUSE AS-IS

- `apple_agx_aperture`: checked aperture offsets and ranges.
- `apple_agx_software_aperture`: bounded 4 KiB PFN map/unmap/dummy semantics.
- `apple_agx_local_segment`: checked Segment-2 offset to AGX-VA translation.
- `apple_agx_physical_paging`: bounded transfer/fill/discard plans without
  treating physical addresses as GPU virtual addresses.
- `apple_agx_physical_topology`: authoritative two-segment descriptors.

Their portable sanitizer-backed suites pass and their contracts match the
approved Segment1/Segment2 model.

### ADAPTED INTO PRODUCTION

- `apple_agx_memory`: aligned/page-list ownership and abort-safe reuse now back
  render-admission physical objects.
- `apple_agx_residency`, `apple_agx_uat`, `apple_agx_uat_table`,
  `apple_agx_uat_memory`, and `apple_agx_uat_publication`: context-63 roots,
  16-KiB leaves, mapping rollback and exact gpu-region TTBR publication are in
  the Start/Stop lifetime.
- `windows/src/segment_windows.c`: extract QuerySegment4 and two-segment
  description; do not carry its EXP208 materialization and backend startup into
  the allocation commit.
- `windows/src/paging_windows.c`: adapt physical paging and software-aperture
  operations to typed `render-admission` allocations; preserve no-allocation
  submit paths and paging-fence replay semantics.
- `windows/src/device_context_windows.c`: preserve allocation/context ownership
  checks, but use the already committed `render_objects` model instead of its
  accumulated adapter structures.
- `windows/src/memory_windows.c`: reuse the supported Windows physical-memory
  callbacks only behind the current DXGK interface and exact resource contract;
  keep HVC optional until a measured callback requires it.

### SUPERSEDED

- EXP393/396 physical-object-only qualification flow: both runs failed before
  StartDevice; physical object, ADL, map and HVC were never executed and are not
  memory success evidence.
- EXP139 private physical mapping of `gpu-region`: rejected; only translated
  assigned resources or the approved current transport may be used.
- Disconnected UAT snapshot/qualification profiles and old lifecycle-only
  stubs: they do not establish production memory ownership.

### DO NOT USE

- Old EXP379 or other incompatible recovery/HVC artifacts.
- CPU physical address as AGX virtual address.
- Direct arbitrary system-memory AGX residency.
- `MapApertureSegment2`/logical ADL claims without a Windows IOMMU contract.
- Any physical AGX IRQ 880--888 before status/ownership/ack/completion/fence is
  derived and proven.

## Hardware evidence boundary

- EXP175 hardware-accepted the truthful T8103 40-bit Type34 visibility limit.
- EXP140 hardware-validated Mu/m1n1 assignment of SGX, context-zero gpu-region
  and broker resources; its Windows UAT snapshot did not run.
- EXP208 hardware-validated the m1n1-owned G13/v13.5 materialized TA+3D graph
  and completion, not Windows allocation or paging.
- EXP412 executed the production physical owner, HVC translation, local
  allocation, context-63 UAT mapping and gpu-region TTBR publication/readback.
  No experiment has yet caused dxgkrnl to execute render-admission
  QuerySegment4, CreateAllocation, BuildPagingBuffer/DMA completion or a
  non-paging render submission.

FIRST UNKNOWN: one commit-pinned integrated hardware candidate must determine
whether current Windows accepts the complete Type1 vector after StartDevice.
If it advances, collect the first actual downstream callback/UMD/scanout/render
boundary rather than assuming the entire stack ran. KMD scanout/D589/VSync/
POST release, UMD, render, preemption and TDR all remain hardware-unproven until
their exact receipts occur.
