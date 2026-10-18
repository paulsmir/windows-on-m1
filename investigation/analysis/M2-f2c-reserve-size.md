# M2-f2c: a receipt-sized AGX local reserve (design, 2026-10-13)

Evidence: EXP1252 measured VidMm segment 2 (the m1n1 local reserve's VidMm part, limit 952 MiB) at a
167 MiB peak during the CS 1.6 benchmark while segment 3 (the KMD pool through the hypervisor window,
1 GiB) peaked at 666/685 MiB committed/resident. The reserve keeps 1 GiB of the 8 GB machine away from
Windows for a segment that uses a sixth of it; the pool is the memory that grows with workloads.

## Inventory (subagent report 2026-10-13, file:line at integration 5ff86d12)

S = reserve bytes (0x40000000), P = private span (64 MiB), B = backend arena (8 MiB),
A = segment 2 = S - P - B (0x3b800000), backend offset = S - B (0x3f800000). Limits: S a multiple of
64 KiB, below 4 GiB (CM resource Length and Mu's UINT32 cast), A >= one scanout surface (0xfa0000);
G3 page tables also live in segment 2.

Every layer pins S independently and requires the receipt's BYTES (broker+0xd20) to equal it:
- m1n1: HV_AGX_LOCAL_BYTES (hv_agx_local_reserve.h:10) -> selection, carveout checks, VALID register
  (bytes == S), public span (S - 0x4800000, power_mmio.c:113-128), scanout_allows (S - 0x1800000,
  stale vs the current split), HV_AGX_SCANOUT_J313_POOL_SIZE 0x3b800000 (scanout_broker.h:31, exact
  match at .c:46/:83); private 64 MiB fixed (retained_platform.c:316-320, gpuva_v5.c:356);
  HV_AGX_GPUVA_V5_LOCAL_PAGES 65536 is only an upper bound.
- Mu: AGX_LOCAL_RESERVE_BYTES (AgxLocalReserveValidation.h:10-13, RangeValid requires equality);
  MemoryInitPeiLib.c:498 fatal on mismatch, 512-513 reserve/HOB with the constant; ASL generator
  (tools/generate_j313_agx_abi_admission.py) emits AGXLR_BYTES/AGXLR_MAX, LBYL == AGXLR_BYTES, a static
  LRNG _LEN and 0xFFC0000000 (= 2^40 - S).
- KMD: APPLE_AGX_LOCAL_RESERVE_BYTES (apple_agx_local_reserve_abi.h:9-12, MatchesResource equality),
  lifecycle.c:124-125 (resource length == S before the receipt is read), memory_runtime_windows.c:5-9
  (ADMISSION_LOCAL_BYTES/ALLOCATION_BYTES/BACKEND_OFFSET constants, LocalView checks 795-800,
  C_ASSERT 848, scanout view 854-861), APPLE_AGX_SCANOUT_J313_POOL_SIZE (apple_agx_scanout.h:39 and
  bounds in scanout/fixed_panel/visible_scanout/scanout_windows).
- Already parametric: KMD borrow/map size, render_memory.c partition/backend/paging, QuerySegment4/5,
  physical topology, residency, private/backend views, G3 code; ASL _MAX; m1n1 pool window exclusion,
  gpuva_v5 local/private lookups, scanout service (request.PoolSize).
- ~20 tests pin S, A or the backend offset by value or source string (list in the subagent report:
  test_g3_local_reserve_abi/aml, test_g3_mu_*, render_memory/umd_submit source strings, fixtures
  agx_local_reserve_v2_test.c, agx_kmd/mu_local_reserve_test.c, g3_start_resource_replay.c,
  g3_segment_query_replay.inc, g3_memory_lifecycle_replay.inc, g3_r144/g3_r168 cases,
  test_g4_render_local_view_replay.py, hv_agx_gpuva_v5_test.c, scanout/fixed_panel tests).

## Decision: the receipt carries S; m1n1 owns the size

The producer (m1n1) chooses S per machine and publishes it; every consumer validates a range
(64 KiB-aligned, S - P - B >= the scanout minimum plus table headroom, S < 4 GiB, base + S <= 2^40)
and derives A = S - P - B and the backend offset S - B from the receipt. The reserve ABI version
becomes 3 in every layer (a v2 consumer refuses a v3 receipt and vice versa: Mu's mismatch is fatal,
the KMD fails closed at StartDevice). Compile-time constants stay only as upper bounds
(HV_AGX_GPUVA_V5_LOCAL_PAGES). The scanout pool becomes the receipt-derived A in both m1n1 and the KMD
(EXP953: the pool covers all of segment 2).

WINDOWS CONTRACT: segment 2 keeps CpuVisible + PopulatedByReservedDDRByFirmware with Size/CommitLimit =
A from the receipt (QuerySegment4/5 already take LocalAllocationBytes); the AGX0._CRS memory resource
length follows the receipt (ASL patches _MIN/_MAX/_LEN); MemoryInitPeiLib reserves exactly S as
EfiReservedMemoryType, so the rest returns to Windows RAM.

AGX/ASAHI CONTRACT: unchanged semantics; the broker's local span = A + private P, grants sized by A.

TRANSLATION: none new; the partition rule (A + P + B == S) already exists in render_memory.c.

## Ladder

1. f2c-1 (offline + one hardware run): parametric consumers and ABI v3 in m1n1, Mu (+ASL), KMD, tests;
   S stays 1 GiB. Checkpoint: every EXP1253 check unchanged (atomic contract: Mu, m1n1 and the KMD
   must agree on the v3 receipt or the boot fails closed by design).
2. f2c-2: m1n1 selects a smaller S (candidate 384 MiB: A = 312 MiB, ~1.9x the measured 167 MiB peak);
   single variable; Windows RAM +640 MiB; segstats confirms segment 2 headroom.
3. f2c-3: the KMD sizes the pool per machine (fraction of Windows RAM, registry override, capped by the
   window capacity); single variable.
