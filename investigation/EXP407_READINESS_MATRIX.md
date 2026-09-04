# EXP407 live readiness matrix

Updated: 2026-09-04T13:33:00+02:00

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
- `MEMORY_PAGING_HW_PROVEN = NO`: EXP408 selected the exact qualification
  package and remained system-healthy but produced no memory proof; it does not
  prove the live chain
  Windows KMD -> HVC `0x4d31` -> current m1n1 EL2 -> host PA -> context-63 UAT
  publication -> cleanup. A stage/status-only follow-up must locate the exact
  pre-proof boundary. This document must not treat the implementation bit as
  hardware evidence.

## Fourteen prerequisites

| Feature | Implementation | Evidence | Dependencies | Ready |
| --- | --- | --- | --- | --- |
| `WDDM3_IDENTITY` | Exact 1296-byte WDDM 3.0 initialization vector and pre-Start WDDMDEVICECAPS 3.0 response | EXP406: `DxgkInitialize=SUCCESS`, Add/Start reached, Type1/592 called successfully | Pinned WDK 10.0.28000.2526 | yes |
| `ONE_NODE_TOPOLOGY` | Type1 names one asymmetric node and GetNodeMetadata describes ordinal 0 as 3D | Source and offline tests only; no admitted scheduler-visible AGX engine exists | Scheduler, context, completion | no |
| `MEMORY_PAGING` | Complete production chain: Segment1/2, allocation backing/lifetime, exact contiguous DXGK object/ADL/map, bounded HVC 0x4d31, 40-bit host pages, context-63 16-KiB UAT publication, BuildPagingBuffer encode/execute, bounded paging worker, synchronized DMA completion/fault and reverse cleanup | Commits `8cd1449`, `3380434`, `39f64c6`; 238-test gate; pinned-WDK KMD/UMD build, analysis, Universal, Inf2Cat and signing pass with zero warnings/errors | `IMPLEMENTED=yes`; `HW_PROVEN=no`; EXP408 inconclusive before proof | yes |
| `DEVICE_CONTEXT` | Typed nonpaged adapter/device/context ownership, bounded counts, node 0/affinity 1 and busy destruction | Commit `7b0c771`; RED/GREEN object tests; pinned-WDK KMD/UMD build with zero warnings/errors | Allocation references and scheduler attachment remain later layers | yes |
| `SCHEDULER` | SubmitCommand and scheduler callbacks remain fail-closed | No `render-admission` scheduler implementation | Memory, context, backend queue | no |
| `DMA_BOUNDARY_PREEMPTION` | PreemptCommand remains fail-closed; no progress/fence accounting | None in `render-admission` | Scheduler, monotonic completion, replay | no |
| `PER_ENGINE_TDR` | QueryEngineStatus, ResetEngine and CollectDbgInfo remain fail-closed | Node metadata alone is insufficient | Scheduler, valid submitted/completed interval, quiesce/recovery | no |
| `GDI_COMMAND_BUFFER` | CreateAllocation, standard allocation data and RenderKm remain fail-closed | Existing accumulated GDI translator is not linked or end-to-end complete here | Cache-coherent aperture, allocation, patch, submit, hardware completion | no |
| `D589_SCANOUT` | m1n1 retained-owner A407/A408/D589 path exists; `render-admission` does not register a pool or submit a primary | EXP270 proves retained owner, Scanout ABI v2 and exact D589 through Windows login; EXP406 candidate ran ABI v1 and no Windows present | Current full-owner build, KMD primary mapping, synthetic 889 ISR/DPC | no |
| `KMD_DIRECT_FLIP` | SetVidPnSourceAddress is DIRQL-safe but deliberately returns NOT_SUPPORTED | In-memory receipt path only | Admitted primary allocation, nonblocking broker enqueue, exact D589 | no |
| `UMD_DIRECT_FLIP` | UMD exports `OpenAdapter10_2` and returns `E_NOTIMPL` | Package/build evidence only | Real UMD adapter/device/resource compatibility path | no |
| `INDEPENDENT_FLIP` | Not advertised or implemented | None | KMD and UMD DirectFlip plus real VSync completion | no |
| `NON_VGA_STOP` | Stop/release returns saved POST info and stops the adapter, but does not establish black fallback or latched handoff | Source-only partial implementation | Registered scanout pool, bounded quiesce, black fallback, accurate final POST | no |
| `AGX_COMPLETION` | Accumulated EXP208/UAT/G13 queue/completion components exist but are not linked to `render-admission` | EXP208 proves a standalone materialized TA+3D graph; no Windows fence mapping | Windows allocation/context, UAT, queue publication, event/stamp ingress | no |

Current functional implementation result: `3/14 READY`; this is not a count of
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
- No experiment has executed `render-admission` QuerySegment4,
  CreateAllocation, BuildPagingBuffer, UAT publication or context paging.

FIRST UNKNOWN: turn the metadata-only node into one real scheduler-visible
engine with monotonic render/paging fence accounting. Then implement
DMA-buffer-boundary preemption and per-engine TDR before GDI command-buffer
readiness. The m1n1 HVC handler and the Windows owner remain hardware-unqualified
until the integrated candidate; no separate safe production-code standalone
harness was available and no incomplete Full Graphics package was installed.
