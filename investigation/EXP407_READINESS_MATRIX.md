# EXP407 live readiness matrix

Updated: 2026-09-04T12:53:00+02:00

This matrix is evaluated after memory source commits
`32cd23e5244427148adf69525d4d32381778ca77` and
`1e76707aa9fe34d1e4e69de478b991574a06a386`. `READY=yes` means an
operational contract exists; callback registration, a success return, a data
structure, or a source-presence test alone is insufficient.

## Fourteen prerequisites

| Feature | Implementation | Evidence | Dependencies | Ready |
| --- | --- | --- | --- | --- |
| `WDDM3_IDENTITY` | Exact 1296-byte WDDM 3.0 initialization vector and pre-Start WDDMDEVICECAPS 3.0 response | EXP406: `DxgkInitialize=SUCCESS`, Add/Start reached, Type1/592 called successfully | Pinned WDK 10.0.28000.2526 | yes |
| `ONE_NODE_TOPOLOGY` | Type1 names one asymmetric node and GetNodeMetadata describes ordinal 0 as 3D | Source and offline tests only; no admitted scheduler-visible AGX engine exists | Scheduler, context, completion | no |
| `MEMORY_PAGING` | Segment1/Segment2, PFN aperture, local address translation, paging plans, gated QuerySegment4 and allocation callbacks are production-linked; BuildPagingBuffer and the physical-memory owner remain incomplete | Commits `32cd23e` and `1e76707`; 220-test gate and two pinned-WDK zero-warning builds pass; EXP393/396 never reached any memory callback | DXGK physical-memory object/ADL/map, HVC translation, context-63 UAT publication, paging execution | no |
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

Current result: `2/14 READY`. The atomic readiness evaluator therefore must
publish zero mandatory Type1 caps.

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

### ADAPT

- `apple_agx_memory`: reuse the ownership/state transitions, but instantiate it
  under `render-admission` storage and allocation handles.
- `apple_agx_residency`, `apple_agx_uat`, `apple_agx_uat_table`, and
  `apple_agx_uat_publication`: reuse 16 KiB leaf encoding and rollback, but
  defer actual context-root publication until the render backend owns power and
  firmware state.
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

FIRST UNKNOWN: current DXGK physical-memory object/ADL/map to HVC `0x4d31`
translation owner, followed by context-63 16-KiB UAT publication and
BuildPagingBuffer execution. The m1n1 HVC handler is committed and its host
test passes, but no safe standalone current hardware harness exists yet; do not
install an incomplete Full Graphics driver merely to exercise it.
