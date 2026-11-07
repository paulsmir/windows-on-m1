# T11b — GPU-only BC transfers and omitted-native-leaf evidence

Offline candidate, not hardware validation or proof of the EXP1922 fault's
allocation. TASK.md supplied the two changes and the no-hardware boundary.
The BC7 1024×1024/96-layer/11-mip candidate remains a ranked hypothesis from
T11; no sequential-VA estimate is used as an eligibility rule.

## Primary sources and ownership

Paths below are relative to this worktree. Mesa paths are under
`.local/reference/mesa/src/`; Windows headers are the supplied offline copies.

- `investigation/analysis/T11-dcmp-unmapped.md:139` documents accepted-residency
  completion; `:182` onwards documents native-leaf representability and the BC
  exception. The supplied EXP1922 DCMP VAs are 0x119e720d40 and 0x11ac135080.
- `gallium/drivers/asahi/agx_pipe.c:803` selects staging formats, `:830` creates
  staging, `:871`/`:891` blit both ways, `:912` maps, and `:1032` writes back.
  `:697` is the direct shadow memcpy that the existing GPU-only flag disables.
- `gallium/drivers/asahi/agx_blit.c:232` admits compute blits, `:314` builds
  their normalized coordinates/grid, `:522` records them with normal hazards,
  and `:618` copies resources with a CPU transfer fallback at `:674`.
- `gallium/drivers/asahi/agx_state.c:637` packs sampler descriptors;
  `:1131` packs PBE descriptors. Before this patch, overriding a BC resource
  with a raw UINT format still used pixel base dimensions and selected LOD.
- `asahi/vulkan/hk_image_view.c:149`, `:273`, `:441` establish the existing
  AGX raw-block-view protocol: uncompressed view of compressed storage,
  single mip/layer, rebased layer+level address, dimensions in blocks, LOD 0.
  The original layer stride and allocation remain authoritative.
- `asahi/layout/layout.c:73`, `:148`, `:191` calculate BC miptree offsets,
  power-of-two tails and layer stride in elements; `:266` forbids combining
  BC block compression with AGX lossless compression metadata. They are
  distinct uses of the word compression.
- `.local/reference/wdk26100/d3dkmddi.h:4693` supplies hAllocation,
  AllocationOffsetInBytes, FirstPteVirtualAddress and input PTEs for leaf
  updates. `.local/reference/winsdk26100/um/d3d12umddi.h:294` defines
  NOT_AVAILABLE separately from WRITE_COMBINE/WRITE_BACK.
- `drivers/apple-agx/render-admission/src/allocation_windows.c:533` retains
  aperture for CPU-visible allocations; `:545` selects native-only placement
  for the existing explicit GPU-only request. This file is unchanged.

Mesa/winsys own resource intent, transfers, hazards and fences; KMD owns
logical/native publication; VidMm owns placement and residency. m1n1 owns
broker/stage-2, Mu owns firmware reserve and the integration owner owns
launch/recovery. No initialization, interrupt, DMA, power, firmware or
recovery contract changes. The task forbids fresh live measurements and
hardware access; none was made. No external implementation body was copied.
The existing projection retains upstream notices in the generated Mesa files;
inspected Asahi/Honeykrisp implementation notices are MIT.

## Change 1: a bounded historical omission receipt

`render-admission/` below means `drivers/apple-agx/render-admission/`.
`include/render_omitted_leaves.h:1` defines v1 `Wom1G4OmittedLeaves`, REG_BINARY,
2976 bytes, little-endian. Header (32 bytes): four u32 fields Version, Bytes,
Next, Count, followed by u64 TotalEvents and TotalGroups. Eight 368-byte events
follow in ring order. Event layout:

- 12 u64: Sequence, Qpc, ProcessId (broker graph identity), OsProcessId,
  Allocation (KMD handle), AllocationVa, AllocationBytes, UpdateVa,
  UpdateBytes, GroupVa, FirstOmittedVa, LastOmittedVa.
- Four u32: Segment (input PTE segment, 0 means system backing, all-ones
  means mixed), OmittedGroups, GroupCount, Flags.
- 256 bytes: bit n identifies the omitted native group at GroupVa+n×16384.
  First/last are group bases, not inclusive byte ends. Holes stay explicit.
- Flags: bit 0 says allocation size metadata is valid; bit 1 says the
  allocation VA base could be derived without underflow from the update VA
  minus AllocationOffsetInBytes. This can describe an alias, not a unique
  lifetime-long reservation.

`src/gpuva_g3_paging_windows.c:512` records identity and metadata while the
callback owns hAllocation. The publication loop at `:748` marks incomplete
or broker-unavailable groups containing valid PTEs from this update.
Only after successful logical commit (`:805`) is the event added to the ring.
Failed/rolled-back updates, complete groups, CPU-only paging, and wholly invalid
updates add no events. Partial updates identify the current update's allocation;
retained neighbour allocations are not falsely attributed to it.

`src/receipts.c:2070` stages the snapshot under its spin lock, and `:2134`
writes/flushed it through the existing worker. No registry I/O is introduced
inside BuildPagingBuffer. This preserves the EXP1073 deadlock remedy documented
at `receipts.c:2007`. The recorder is inside the G3 build guard.

Use the existing offline decoder:

```sh
python3 tools/decode_wom1_gpu_fault.py Wom1GpuFault.bin Wom1G4OmittedLeaves.bin
python3 tools/decode_wom1_gpu_fault.py state.json
python3 tools/decode_wom1_gpu_fault.py device.reg
```

`tools/decode_wom1_gpu_fault.py:301` validates size/version, ring bounds,
bit count and first/last, then emits chronological events and half-open
`omitted_ranges`, intersected with the update span. Match fault `process_id`
to receipt `os_pid`, then VA against these ranges. The bitmap identifies
native groups, not the validity of every 4 KiB logical PTE within them.

This is bounded **history**, not a current-mapping inventory: later repairs,
unmaps, process exits and handle/VA reuse may supersede events. Old events
can be overwritten; the deferred worker can lose its newest unsaved snapshot
on a sudden crash. Absence of a match is inconclusive. TotalEvents counts
updates with omissions and TotalGroups counts their groups, including repeated
updates; neither is a count of unique allocations or currently missing leaves.
Pure invalidations do not enumerate retained neighbours outside their update.

## Change 2: BC resource and transfer contract

`mesa/` below means `drivers/apple-agx/mesa/`.

- `winsys/agx_d3d12_windows.cpp:951` selects R32G32_UINT for eight-byte BC1/BC4
  blocks and R32G32B32A32_UINT for sixteen-byte BC2/3/5/6H/7 blocks.
  `:985` opts in only with existing committed NOT_AVAILABLE intent, non-MSAA
  storage and supported RT+sampler staging format. The DDI intent gate at
  `drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp:2448` is unchanged.
- `winsys/agx_d3d12_windows.cpp:1920` reuses C84's validated source/destination
  boxes for a raw GPU blit. `:1942` selects it when a reinterpret copy touches
  GPU-only BC. `:1992` accounts pending work so copy-only commands participate
  in reserved mapping changes' flush gates (`:482`, `:1060`).
- C70/C80 cached staging at `:2163`/`:2222` uses raw block dimensions and CPU
  visibility. Only staging and the upload/readback buffer are mapped; the BC
  texture is copied by blit. `:2324` extends the cache to BC arrays for single
  layer transfers. Multi-layer transfers use the existing Gallium transfer
  staging and per-layer GPU blits, without a direct BC BO map.
- `scripts/build-native-asahi-state.py:370` extends the existing staging
  projection with raw formats/block boxes. It preserves the existing forced
  staging for UNMAPPABLE, staging flag removal, read synchronization, write
  flush, NULL cleanup and direct/persistent/coherent-map refusal.
- `scripts/build-native-asahi-state.py:459` projects raw sampler/PBE rebasing,
  block-sized compute normalization and dispatch bounds, and splits layered
  blits into single-layer views. It runs in production at `:6567`. The
  original BO, AIL offsets/stride, batch references and hazard tracking stay
  in the existing Asahi path; no alias BO or second copy engine is created.

Reachability audit: application DEFAULT texture maps are excluded by DDI
intent. Internal buffer↔texture replay takes cached or Gallium staging;
C84 raw↔BC copies take the raw GPU blit. Same-format texture copies may take
Asahi's util_resource_copy_region fallback, whose texture maps now map
CPU-visible staging; they never directly map the GPU-only BC BO. Shadow-copy
memcpy is disabled by T7h. Ordinary sampling/drawing uses GPU descriptors;
BC render-target/UAV/depth/MSAA operations are outside the accepted API format
contract. Internal shader/query/pool BOs do not inherit the resource flag.
`agx_win32_asahi_bo.c:881` and
`render-admission/umd/src/umd_win32_screen.c:1022` still refuse accidental
direct GPU-only BO maps. Allocation flags/placement remain T7h's unchanged
General/CpuVisible=0/native-only contract.

Exceptions retained: placed resources (no committed GPU-only intent), imported
shared/presentation allocations (separate constructors), CPU-accessible heaps,
BC volumes, MSAA, unsupported staging formats and existing combined/stencil
exceptions. BC 2D arrays are supported, not excluded by layer count or size.
No trace-derived dimensions, content, offsets or bind combinations are gates.

## Verification and remaining unknowns

The new eligibility and staging tests failed before their fixes. Actual owner,
projected transfer, descriptor and compute-coordinate excerpts execute under
ASan/UBSan. Controls include BC1–BC7 representatives, both block widths,
non-power-of-two dimensions, 2×2 tails, two array layers, padded footprints,
cache reuse, allocation/map failure cleanup, direct-map refusal and existing
color/depth/volume/shared behavior. Leaf replay covers scattered/repaired/
partial/misaligned maps, first-invalid groups, a published hole, ring wrap,
publication failure and CPU-only paging. Decoder validates malformed input.

Independent review found the missing PendingCopies accounting; the owner
replay reproduced it before the correction, then passed. Author review found
and regression-tested the G3-only recorder's compile guard. Exact test totals,
BASELINE_OK, commit IDs and known/environment failures are in handoff REPORT.md.
No baseline suppressions or experiment/change ledgers were edited.

Unknown: native Windows compilation, real GPU raw-block byte preservation and
coherence, timing/performance, VidMm placement/eviction under load, durable
receipt capture at a crash, and the actual allocation behind EXP1922. Host
callbacks are an oracle, not a hardware blitter. No claim of curing DCMP or the
separate post-fault quiesce failure is made.

## Smallest integration checkpoint (not run here)

WHY THIS HYPOTHESIS: T11 identifies the existing BC CPU-visible exception;
KMD deliberately omits unrepresentable native groups; sampler targets can
escape G4's enumerated command-range validation. These support eliminating
CPU access for BC and measuring actual omitted ranges, without claiming the
sequential-VA candidate was proven.

Build a matched package including the changed native Mesa objects and KMD.
First check BC1/BC7 upload/readback and raw integer↔BC copies with a small mip
and array layers. Then GravityMark D3D12 10k/2k short profiles with
Wom1GpuFaultInfoRead=1. Expected: no DCMP unmapped fault, actual GPU completion
through execute #47/#48, no removal/TDR, correct BC content, and no required
GPU-only-map refusal. If a fault remains, collect the new receipt and compare
PID/VA against its retained ranges; a match names the historical allocation
and omission, while no match is inconclusive.

Preserve/hash the known-good recovery artifact; integration collects receipts
before exact-package cleanup through the existing ordinary GPU-visible Code28
recovery. The immutable GPU-hidden image remains emergency-only. No hardware,
guest, package build, launch or recovery was performed by this offline task.
