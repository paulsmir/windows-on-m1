# M2-f2: a hypervisor window over driver-allocated chunks (design, 2026-10-13)

Evidence: EXP1246/EXP1247 got a 1 GiB physically contiguous block at StartDevice (123.5 / 340.6 ms);
EXP1248, the identical flow, did not (MmAllocatePagesForMdlEx SkipBytes 0 + REQUIRE_CONTIGUOUS_CHUNKS
returned NULL in 0.1 ms). EXP1246 obtained 1 GiB as 64 KiB-aligned 64 KiB blocks (2.3 s) and as 2 MiB
blocks (3.9 s). A single-block pool is opportunistic; segment 3 must not depend on Windows physical
contiguity.

## Sources inspected

- Microsoft: DXGK_SEGMENTFLAGS (CpuVisible: "accessible to the CPU at the address that is specified by
  CpuTranslatedAddress"; PopulatedFromSystemMemory: "populated from system memory that the driver
  allocated during initialization" -- an accounting statement, no requirement that the CPU address be RAM);
  MmAllocatePagesForMdlEx (SkipBytes = power of two >= PAGE_SIZE with REQUIRE_CONTIGUOUS_CHUNKS: every
  block exactly SkipBytes long and SkipBytes-aligned; FULLY_REQUIRED: NULL unless complete; max 4 GiB -
  PAGE_SIZE per call; free with MmFreePagesFromMdl + ExFreePool).
- m1n1 (m1n1_windows/src, subagent report 2026-10-13): stage 2 hv_vm.c (16 KiB granule, L3 pages and
  32 MiB L2 blocks, Normal WB inner-shareable attributes, HCR_EL2.FWB clear so the guest's stage-1 type
  wins; hv_map records every call in a 64-entry launch recorder; no runtime TLBI helper); guest RAM is
  identity-mapped except the low window (IPA 0x100000 -> PA 0x8a0100000); 36-bit IPA ("HV: Initializing
  for 36-bit PA range", EXP1248 full.log); broker page 0x300000000 layout (power 0x000, config 0x100,
  prefix 0x280, scanout 0x400, RR 0x600, GPUVA v5 0x700, FW IO 0x800-0xb60, HWDATA 0xc00, local receipt
  0xd00-0xd28; 0xe00-0xfff free); GPUVA v5 REGISTER_(SHARED_)BACKING translates the page IPA through the
  stage-2 walk (translate_guest: guest RAM PA, below 2^40, outside the firmware root, GUEST_RAM contract
  region) and does not require IPA == PA for system backings; one PA under two IPAs is OWNERSHIP; broker
  state is re-created at every firmware epoch (AGX_RR_ACTIVATE -> hv_agx_gpuva_v5_init).
- Live J313: /arm-io ranges 0x200000000-0x780000000 (EXP476 j313-live.adt); guest RAM ends 0x9df708000
  (8 GB) or 0xc00000000 (16 GB); vGIC 0xf00000000-0xf30000000, ADT alias 0xf30000000. Free IPA:
  0xc00000000-0xf00000000.
- Mu/ACPI: the local reserve is published through the broker receipt, reserved by MemoryInitPeiLib and
  declared in AGX0._CRS (LRNG).
- KMD: pool_windows.c, memory_windows.c QuerySegment4/5, the M2-f2b segment-3 consumers (B-G), which
  assume one IPA-contiguous range starting at the pool's GuestIpaAddress.

## Contract

WINDOWS CONTRACT: segment 3 stays a CPU-visible memory segment flagged PopulatedFromSystemMemory (the
pages are system memory the driver allocated at start); its CpuTranslatedAddress is the window IPA, one
contiguous range as the descriptor requires. The pages come from MmAllocatePagesForMdlEx: one block first
(SkipBytes 0), else 64 KiB blocks (SkipBytes 0x10000, every block 64 KiB-aligned, which Use64KBPages
needs), both FULLY_REQUIRED, zeroed, MmWriteCombined. The KMD's own CPU view is the MDL mapping (WC);
window offset k and MDL byte k are the same page.

AGX/ASAHI CONTRACT: UAT leaves map 16 KiB groups; the broker registers a page by IPA and resolves it
through stage 2; the GPU sees PAs only. Nothing changes for the GPU.

TRANSLATION: the window gives the B-G code the IPA-contiguous range it already assumes (pool IPA + offset
= window IPA + offset). Stage 2 resolves window IPA -> chunk PA for the CPU (VidMm CPU views) and for the
broker (REGISTER_SHARED_BACKING of window IPAs becomes a system record of the chunk PA).

OWNERSHIP: m1n1 owns the window (selection, stage-2 entries, invariants: each window page maps one guest
RAM page, no page twice, never a reserve/firmware/broker-backed page, never unmapped while the broker
holds a backing for it); the KMD owns the chunks (allocation, order, lifetime) and asks m1n1 to map them
in order and to unmap all before freeing them. The window outlives broker epochs (firmware restarts) and
is bounded by the KMD's StartDevice..Stop.

## m1n1 ABI (broker page 0xe00, shared header apple_agx_pool_window_abi.h)

- 0xe00 MAGIC 'AGXW', 0xe04 VERSION 1, 0xe08 STATUS (bit0 window available)
- 0xe10 WINDOW_IPA (u64), 0xe18 WINDOW_BYTES (capacity, u64), 0xe20 MAPPED_BYTES (u64)
- 0xe28 SOURCE_IPA (u64, write), 0xe30 BYTES (u64, write)
- 0xe38 COMMAND (u32 write: 1 MAP_APPEND, 2 UNMAP_ALL), 0xe3c RESULT (u32)
- MAP_APPEND maps [MAPPED_BYTES, MAPPED_BYTES + BYTES) of the window onto SOURCE_IPA.. (16 KiB-aligned,
  every page a guest RAM page through the stage-2 walk, outside the local reserve, the firmware root and
  the window, not already in the window, no live broker backing for its PA); all-or-nothing.
- UNMAP_ALL: BUSY while any window page's PA has a live broker backing; else every window entry becomes
  invalid, then TLBI VMALLS12E1IS + DSB ISH + ISB, MAPPED_BYTES = 0.
- Invalid -> valid needs no TLB maintenance (faulting entries are not cached); stores are made visible
  with DSB ISH before the MMIO completes. Runtime window entries bypass the launch recorder.
- J313 window: IPA 0xd00000000, capacity 4 GiB (below the vGIC, above any J313 RAM end), validated at
  selection against guest RAM and the ADT arm-io ranges. Other platforms select their own base.

## Differences and gaps

- Asahi Linux allocates GPU memory from normal pages and maps it by PA in the UAT; it has no VidMm and no
  CPU-visible segment, so it needs no window. m1n1 upstream has no guest-driven stage-2 mapping; this is
  new hypervisor ABI owned by this project.
- ACPI: the window is not declared in AGX0._CRS in this step (VidMm maps CpuTranslatedAddress itself,
  as for segment 2's receipt-published reserve). Declaring it (with the Mu MemoryInitPeiLib pattern) is a
  follow-up so PnP never assigns the range to another device.

## Hardware checkpoints (one variable each)

1. EXP1249: m1n1 with the window ABI, EXP1248's package (does not touch it). Checkpoint: every EXP1248
   check unchanged, boot log reports the window, no stage-2 regression. Recovery: rec1249h with the
   EXP1248 m1n1.
2. EXP1250: KMD pool through the window (block, else 64 KiB chunks). Checkpoint: the pool exists on every
   boot (Wom1M2PoolProbe status ok with the run count), segment-3 groups published (shape trait 0x10),
   every check passes. Failure readings: MAP refused (result code names the invariant), Code 43 /
   VidMm bugcheck (PopulatedFromSystemMemory with a non-RAM CpuTranslatedAddress), GPU faults at window
   pages (broker translation).
