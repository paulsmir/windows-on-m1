# EXP407 live readiness matrix

Updated: 2026-09-04T16:32:00+02:00

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
| `MEMORY_PAGING` | Complete production chain: Segment1/2, allocation backing/lifetime, exact contiguous DXGK object/ADL/map, bounded HVC 0x4d31, 40-bit host pages, context-63 16-KiB UAT publication, BuildPagingBuffer encode/execute, bounded paging worker, synchronized DMA completion/fault and reverse cleanup | Commits `8cd1449`, `3380434`, `39f64c6`, `4779f02`; EXP412 stage-10 HVC/PA/UAT/TTBR/readback/cleanup PASS; pinned-WDK build zero warnings/errors | `IMPLEMENTED=yes`; `HW_PROVEN=yes`; BuildPagingBuffer/DMA completion still need Windows-driven exercise | yes |
| `DEVICE_CONTEXT` | Typed nonpaged adapter/device/context ownership, bounded counts, node 0/affinity 1 and busy destruction | Commit `7b0c771`; RED/GREEN object tests; pinned-WDK KMD/UMD build with zero warnings/errors | Allocation references and scheduler attachment remain later layers | yes |
| `SCHEDULER` | One-node lifetime and context attachment now share one monotonic queued/active/completed fence interval with the production paging path; completion is accepted only for the exact active fence | Commits `3df8e82` and `4d539ea`; 35-test gate and pinned-WDK build | Real non-paging RenderKm packet and AGX event/stamp completion | no |
| `DMA_BOUNDARY_PREEMPTION` | Exact scheduler last-submitted/active snapshot cancels queued unpublished work, blocks later dispatch, waits for the active boundary and permits one DMA_PREEMPTED claim/commit | Commit `421a1ac`; deterministic RED/GREEN and EXP414 WDK build | Real non-paging backend must use the interval before capability publication | no |
| `PER_ENGINE_TDR` | Query/status/reset/debug callbacks consume scheduler progress; reset returns active fence or completed boundary and clears outstanding work while preserving monotonic history | Commits `3df8e82` and `ec214ae`; deterministic RED/GREEN and EXP414 WDK build | Backend responsiveness and proven quiesce/recovery remain absent | no |
| `GDI_COMMAND_BUFFER` | One exact ColorFill/PATCOPY is normalized into a pointer-free record; Patch resolves one Segment-2 CPU/host-PA/GPU-VA tuple and seals the exact fence; SubmitCommand binds EXP208 object 40 to that tuple, reapplies all relocations and queues the same interval | Commits `2ee3398`, `7912547`, `b6ee1a6`, and `eead97f`; EXP415/416/419/421 pinned-WDK builds zero warnings/errors | Scheduler activation, EXP208 provider publication and hardware completion remain absent | no |
| `D589_SCANOUT` | m1n1 retained-owner A407/A408/D589 path exists; `render-admission` does not register a pool or submit a primary | EXP270 proves retained owner, Scanout ABI v2 and exact D589 through Windows login; EXP406 candidate ran ABI v1 and no Windows present | Current full-owner build, KMD primary mapping, synthetic 889 ISR/DPC | no |
| `KMD_DIRECT_FLIP` | SetVidPnSourceAddress is DIRQL-safe but deliberately returns NOT_SUPPORTED | In-memory receipt path only | Admitted primary allocation, nonblocking broker enqueue, exact D589 | no |
| `UMD_DIRECT_FLIP` | UMD exports `OpenAdapter10_2` and returns `E_NOTIMPL` | Package/build evidence only | Real UMD adapter/device/resource compatibility path | no |
| `INDEPENDENT_FLIP` | Not advertised or implemented | None | KMD and UMD DirectFlip plus real VSync completion | no |
| `NON_VGA_STOP` | Stop/release returns saved POST info and stops the adapter, but does not establish black fallback or latched handoff | Source-only partial implementation | Registered scanout pool, bounded quiesce, black fallback, accurate final POST | no |
| `AGX_COMPLETION` | Exact EXP208 clear/output binding and deterministic arena rebase are implemented; production borrows the upper 8-MiB tail, materializes the accepted graph, applies all 159 relocations and binds object 40 to the exact Windows destination tuple; the existing dual-queue/event/stamp provider is committed and supports external images | Commits `68172a3`, `45969de`, `abe363f`, `eead97f`, `4285cef`, `f9ad365`, `35f5a68`, and `c932a36`; generated graph and selected 24-test provider suites; EXP208 hardware evidence; EXP417-422 WDK builds where linked | Windows platform-owner linkage, queue publication, exact dual-event/stamp completion and DPC | no |

Current functional implementation result: `4/14 READY`; this is not a count of
hardware-proven layers. The atomic readiness evaluator therefore must
publish zero mandatory Type1 caps. Commit `52d3bf6` now enforces that rule in
the real QueryAdapterInfo path; the final capability writer remains absent and
fails closed even if an accidental all-ready state is presented early.

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

FIRST UNKNOWN: connect the existing external-image platform provider to the
Windows Start/Submit/PASSIVE poll/DPC lifetime. Completion and every readiness
bit remain false until actual queue publication plus dual TA/3D event/stamp
retirement advances the exact active Windows fence.
