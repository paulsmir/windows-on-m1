# EXP415 render path provenance audit

Updated: 2026-09-04T15:35:00+02:00

## Scope and fixed boundary

The current boundary is:

Windows RenderKm -> immutable DMA/private-data record -> exact Patch set ->
SubmitCommand -> common queued/active/completed scheduler interval -> existing
AGX publication -> TA/3D event/stamp completion -> exact Windows fence.

EXP412 memory/HVC/UAT proof and EXP413/414 scheduler state are inputs and are
not redesigned here. Type1 remains the truthful 4/14 zero-capability state.
Physical AGX IRQ 880--888 remains unpublished.

## Primary evidence inspected

- Pinned WDK 10.0.28000.2526 d3dkmddi.h: DXGKARG_RENDER,
  DXGK_RENDERKM_COMMAND, DXGKARG_PATCH, DXGKARG_SUBMITCOMMAND and their IRQL,
  interval, allocation-list and patch-list fields.
- Current shared apple_agx_gdi and apple_agx_dma_shadow implementations and
  their portable tests.
- Current accumulated Windows gdi_windows, scheduler_windows and
  backend_windows implementations.
- Current shared submission queue, completion transaction, backend runtime,
  submission coordinator, event allocator, G13 queue runtime and EXP208
  adapter/dynamic materializer.
- EXP208 materialized artifact and evidence:
  materialized-job.agx SHA-256
  082b987c05c2ebdd44aa9b09df05a609b7d9772c5a15f25cf707c1e6732b4c7f;
  relocation analysis
  9ea9efe1059dadb16818edeb7ed949785048b6b4d1a89f47b40e195dd39218fc;
  hardware result
  053bab6b03f0872799ac956f5d9befee99076f141abd1d97abbc952cc1ec6ea3.

## Provenance classification

### REUSE AS-IS

- apple_agx_gdi portable validation and pointer-free DMA record:
  checked WDDM command byte layout, record-size arithmetic, inline rectangle
  copy, canonical record validation and lowering receipt. It carries no WDK
  pointer into the DMA record.
- apple_agx_dma_shadow:
  bounded non-overlapping DMA intervals, exact patch containment, immutable
  seal bound to one nonzero fence, exact retry comparison and exact submission
  copy.
- render-admission AdmissionMemoryLocalAddressToGpuVa:
  the only allowed Segment-2 offset -> AGX VA translation. It validates segment
  identity, allocation range, offset and overflow against the hardware-proven
  local mapping.
- apple_agx_submission queue and completion transaction:
  bounded FIFO, monotonic fence, publish-before-complete, exact completion
  claim/commit and duplicate/stale/future rejection.
- apple_agx_backend_runtime terminal transaction and
  apple_agx_g13_queue_runtime dual TA/3D event/stamp/done-pointer matching.
  These portable owners already distinguish publication, observation,
  Windows acceptance and retirement.
- EXP208 relocation encodings and dynamic event/stamp patcher:
  exact/32-KiB-page/TA-flagged relocations and the accepted G13/V13_5
  73-object dependency graph are hardware-proven and must not be reconstructed.

### ADAPT

- windows gdi_windows RenderKm/Patch:
  adapt only the WDK layout assertions, command normalization, exact patch-list
  construction and DMA-shadow seal flow to render-admission's typed
  ADMISSION_RENDER_CONTEXT, ADMISSION_OPEN_ALLOCATION and
  ADMISSION_MEMORY_CONTRACT. Remove accumulated adapter types, GDI memory-pool
  ownership and all one-shot experiment gates.
- Windows allocation validation:
  use the already committed render-admission allocation/open lifetime. A patch
  must reference an exact live opened allocation owned by the submitting
  context's device; write operations reject read-only opens.
- windows scheduler_windows:
  later adapt the bounded queue/private-data ownership and exact backend
  completion transaction into the EXP413 common scheduler interval. Do not
  import its parallel scheduler counters.
- windows backend_windows:
  later adapt only platform IO bindings into current render-admission memory and
  ownership. Do not import its older StartDevice, qualification or recovery
  policy.
- EXP208 job materializer:
  reuse its graph and relocation engine, but add a separately derived binding
  from the accepted Windows GDI record to actual destination/source objects.
  Current APPLE_AGX_EXP208_SUPPORTED_GDI_PRIMITIVE_MASK is zero, so EXP208
  cannot yet execute even ColorFill truthfully.

### SUPERSEDED

- EXP218/219 one-shot package, TestContext, private D3DKMT trigger and
  AppleAgxOneShot phase fields. They were tied to the accumulated AppleAgx
  package and EXP164-era platform and failed before a GPU LUID/trigger.
- Accumulated AppleAgx adapter/device/context/allocation structs where the
  separate render-admission equivalents now exist.
- Paging-only LastSubmitted/Active shadow counters as scheduler truth.
  EXP413/414 owns one common queued/active/completed interval.
- Start-time self-submit, private Escape and enqueue-as-completion paths.

### DO NOT USE

- APPLE_AGX_GDI_MINIMUM_OPCODE_MASK as current capability evidence. It describes
  the Microsoft minimum contract, not the current backend.
- APPLE_AGX_EXP208_SUPPORTED_GDI_PRIMITIVE_MASK as anything other than zero
  until a real GDI-to-graph binding is implemented and verified.
- CPU/host physical addresses as GPU virtual addresses.
- Any raw pointer from DXGK_RENDERKM_COMMAND after RenderKm returns.
- Patch locations not emitted by the current RenderKm transaction, patch
  writes outside its prerecorded DMA records, or a sealed shadow with another
  fence.
- Physical IRQ 578/GSIV887 artifacts, old EXP164/EXP218/EXP219 m1n1/Mu
  packages, or any physical AGX IRQ 880--888 before current source supplies
  status, ownership, source acknowledgement, dual completion extraction,
  exact-fence progression and DPC.

## First source batch contract

The first batch supports preparation only for one exact ColorFill/PATCOPY
command. RenderKm copies every required scalar and subrectangle into a
pointer-free APPLE_AGX_GDI_DMA_COMMAND, emits exactly one destination patch
location, and appends the exact DMA interval to apple_agx_dma_shadow. Patch
accepts only that prerecorded relocation, resolves it through
AdmissionMemoryLocalAddressToGpuVa, writes the resulting AGX VA to both DMA and
shadow, and irreversibly seals the shadow to the dxgkrnl fence.

Unsupported opcodes/ROPs, malformed command sizes, non-inline subrectangles,
foreign/stale/read-only allocations, invalid segments/offsets, additional or
missing patch locations, interval mismatch and post-seal mutation fail before
hardware publication. SubmitCommand remains fail-closed for non-paging work
until the second batch owns its exact prepared record.

## Current unknown after the audit

The current portable and Windows preparation code is sufficient for a truthful
RenderKm/Patch batch. The hardware graph is not yet a GDI backend: EXP208 proves
the captured graph executes, but its supported GDI primitive mask is
deliberately zero. The later publication batch must derive and test one exact
ColorFill destination binding before it can accept a Windows submission.
