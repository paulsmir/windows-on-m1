# M3: VidMm "migration churn" under GravityMark D3D12 (EXP2163), offline

Date: 2026-11-04. Read-only analysis; no code, ledger or hardware change.
Evidence: `.local/experiments/EXP2163-beginsplit/evidence/EXP2163/` (`kmd-receipts-mid-a.txt`,
`kmd-receipts-after-d.txt`, `gm2163a-umd.log`, `gm2163a-out.txt`), probe `../probe2163.sh`.
Decoders were throwaway scripts (struct layouts below); times are seconds since the first UMD
log line (QPC 2 910 000 000). QPC frequency is **24 MHz** (`Wom1G3PagingProfile` word 2 = 24000000).

## Verdict (read first)

**The premise does not hold: there is no steady-state VidMm migration.**

1. Operation 9 in the census is `DXGK_OPERATION_VIRTUAL_FILL`, not `VIRTUAL_TRANSFER` (8)
   (`d3dkmddi.h:4604-4605`). VIRTUAL_TRANSFER count is 170 for the whole boot and identical in both
   dumps (profile Build[8] = 170 / 170).
2. The census window (25.04-25.70 s) and the allocation-life window (23.9-25.7 s) are the
   **first ~2 s of rendering** (first present at ~24 s; GravityMark "Updating Scene" at 18.9 s
   process time), not steady state.
3. The census and AllocLife rings were published at **71.3 s** (profile publish QPC 4 621 234 233;
   all three are written together, `receipts.c:3071-3125`) and their newest entries are at 25.7 s.
   The census records every UPDATE_PAGE_TABLE/VIRTUAL_FILL/VIRTUAL_TRANSFER unconditionally
   (`paging_windows.c:188-235`, called at `:244`), and `Next` = 55834 = UPT 45215 + VFILL 10449 +
   VXFER 170 exactly. So **from 25.7 s to 71.3 s (45.6 s, ~1700 presents) VidMm built zero
   UPT/VFILL/VXFER paging operations and the KMD saw zero CreateAllocation/DestroyAllocation.**
4. The "845 batches / 190k UPDATE_LEAF in 40 s" is the delta between the mid dump (71.3 s) and the
   after dump (111.0 s): UPT +845, VFILL +3, PTEs +971 959, broker UPDATE_LEAF +190 652,
   REVOKE_BACKING +188 932. That interval contains **process exit**: the after-d census holds
   only teardown (99.39-99.63 s: Repeat|NotifyEviction UPTs of 8192/2048 entries on levels 0-2,
   `fl=0x5`, `det=0x200/0x201/0x202`), and the after-d AllocLife ring is 256 destroys of pid 10336
   at 99.04-99.58 s. How much of the 845 belongs to 71-97 s cannot be split from these two dumps;
   the measured 45.6 s steady interval had none.

The per-frame cost EXP2163 actually located (V5 LEASE on every GravityMark<->DWM switch,
`EXPERIMENTS.md` EXP2163 ACTUAL) is unrelated to VidMm paging.

## Q1. Segments, budgets and allocation placement (current source)

Topology: `shared/src/apple_agx_physical_topology.c:33-48` (aperture 1, local 2, paging buffer in
segment 1, 4 KiB) and `:61-84` (pool 3: Use64KPages, CpuVisible, PopulatedFromSystemMemory,
CommitLimit = Size). QAI: `render-admission/src/memory_windows.c` (QuerySegment5 `:60-124` for the
G1b page profile, QuerySegment4 `:126-210`).

| Seg | Flags reported | Size / CommitLimit | Budget group |
|---|---|---|---|
| 1 aperture | SYSMEM type, Aperture=1, CpuVisible=0, PopulatedFromSystemMemory=0, 4 KiB slab (`:41-57`) | topology ApertureSize (256 MiB per `M2-uma-fact-base.md`; not re-measured here) | NonLocal (`:14-19`) |
| 2 local | LOCAL type, CpuVisible=1 (G3), PopulatedByReservedDDRByFirmware=1, PopulatedFromSystemMemory=0 (`:105-110`), CpuTranslatedAddress = reserve IPA | `LocalAllocationBytes` (`:95-96`). Wom1LocalReserve: 0x18000000 (384 MiB) at IPA 0x8E0000000. GravityMark reports "Video Memory: 312.00 MB" (= 384 - 64 private scenes - 8 backend, inference) | Local |
| 3 pool | LOCAL type, CpuVisible=1, PopulatedFromSystemMemory=1, slab per G1b profile, CpuTranslatedAddress = pool IPA (`:111-121`) | Wom1M2PoolDemand v15 SegmentBytes = 0x100000000 (4 GiB) | Local |

QuerySegment5 has no CommitLimit field (`d3dkmddi.h:11102-11122`); only the QS4 path writes
`CommitLimit` (`memory_windows.c:37`, local `:176-177`). No DXGK_SEGMENTGROUP/
QueryAdapterInfo budget override exists; the budget is VidMm's from the Local/NonLocal groups.

CreateAllocation (`render-admission/src/allocation_windows.c`):
- default class BO: preferred 2, set {2} or {1,2} if CpuVisible (`:500-511`);
- class0 CPU-visible (CDD/GDI staging): aperture only (`:518-522`);
- any Mesa class BO while the pool exists: pages reserved from Windows at create
  (`AdmissionMemoryPoolReserve`, `:555`), preferred 3, set {3}, plus aperture {1} as SegmentId1
  for CPU-visible (`:561-568`); segment 2 deliberately excluded so no memory-to-memory move exists;
- GPU-only General: set {3} only (`:575-587`); opt-in `SysmemSpill` adds aperture (`:596-603`);
- `EvictionSegmentSet = 0` (`:605`), `AccessedPhysically` only for class0 (`:618`);
- native staging (`APPLE_AGX_NATIVE_STAGING`): aperture only, `ExistingSysMem` (`:627-643`).

Observed in AllocLife (Flags bits 8-15 preferred, 16-23 read set, `render_admission.h:481-491`):
game GPU-only BOs `fl=0x40300` (preferred 3, set {3}); game CPU-visible class 1/3 BOs
`fl=0x10101` (preferred 1, set {1}: native staging, ExistingSysMem); DWM CPU-visible
`fl=0x50301` (preferred 3, set {3,1}). The UMD reports `UMA=TRUE, IOCoherent=TRUE,
CacheCoherent=FALSE` (`d3d12/agx_d3d12_ddi.cpp:518-520`).

## Q2. What the census actually shows

`ADMISSION_PAGING_CENSUS` (`render_admission.h:445-456`): 16-byte header, 128 x 72-byte entries
{Qpc, Operation, Detail, Flags, Entries, Allocation, Process, Offset, Bytes, SourceVa, DestVa}.
For UPT, Detail = level | UpdateMode<<8, Flags = DXGK_UPDATEPAGETABLEFLAGS (`d3dkmddi.h:2081-2095`:
bit0 Repeat, bit1 InitialUpdate, bit2 NotifyEviction).

Mid-a census (25.04-25.70 s), repeating per new allocation (paging process 0x..c5439000, game
0x..bf102000, DWM 0x..d33c8000):

```
UPT  paging-proc  Repeat(fl=1) n=16..512  alloc=0   VA 0x2410000   <- clear the previous window
UPT  paging-proc  fl=0         n=16..512  alloc=A   VA 0x2410000   <- map A into the paging VA
VFILL             pattern 0    0x10000..0x200000 bytes of A        <- VidMm zero-initialises A
UPT  game         fl=0         n=16..512  alloc=A   VA 0x11a4..    <- map A for the game
```

Only GPU-only pool BOs (fl=0x40300) get the fill; native-staging BOs (ExistingSysMem, user pages)
get only the game UPT. Every handle A is a **fresh allocation created a few ms earlier** in
AllocLife (e.g. 0x..cf3ecee0 1 MiB created 1272.09 ms after the ring start, filled at 278.9 ms
census time; same handles throughout). In the 1.82 s life window pid 10336 created ~200 BOs
(37x64K+15x64K GPU-only, 32x256K, 25x512K, 18x1M, 18x2M, 1x8M) and DWM ~30. This is creation
plus first-residency zeroing, not eviction or migration: the census has no VIRTUAL_TRANSFER and
no UPT with NotifyEviction in that window.

Who creates them: the UMD BO cache (`mesa/winsys/agx_win32_asahi_bo.c:320-399`, limit 128
entries / 64 MiB, `agx_win32_asahi_bo.h:18-19`). measure-perf-reason totals: "BO cache miss" 768,
**last reported at present 71**; "BO cache evict" 83 (last at present 71); "BO cache hit"
149 638 (~50/present, to present 2859). Present 71 is ~2 s after the first present, i.e. the end
of the census window. `measure-bo-create` (114 lines) logs only creations >= 10 ms
(`agx_d3d12_windows.cpp:559-586`), so "114 BOs" undercounts creations; its lines also stop at
~26 s (37 of them in 24-26 s).

Pool/budget state at 71.3 s (Wom1M2PoolDemand v15): segment 4 GiB, committed 46 759 x 64 KiB
(2.85 GiB), peak 2.91 GiB, ReservedBytes 2.85 GiB (peak 2.92), RAM 6.72 GiB, graphics share
5.04 GiB, BaseSegmentBytes 3.06 GiB; **Moves 0, ReserveFailures 0, FloorRefusals 0,
HeadroomRefusals 0**, NativeStagingBytes 60 MiB. No over-commit, no trimming signal, no M2-f7c
reclaim pressure on placement. Cross-segment migration is also structurally excluded for the
game's BOs: every set is a single memory segment ({3}) or the aperture alone ({1}).

Hypotheses for "~35 transfers/s steady": local over-commit -- rejected (no VXFER, 384 MiB local
not in the game's sets, Moves 0); residency trimming -- rejected (no NotifyEviction UPT,
no evictions in steady interval); M2-f7c reclaim -- rejected (no refusals, no paging at all);
pool eviction -- rejected (Moves 0, zero ops 25.7-71.3 s). Actual: warm-up BO-cache filling.

## Q3. Microsoft UMA model and what would remove migrations

WINDOWS CONTRACT (pinned WDK plus Microsoft docs as previously cited in `M2-vidmm-64k-decision.md`,
`M2-uma-fact-base.md`; not re-fetched in this pass):
- An integrated GPU may expose system memory through an aperture segment and/or a memory segment
  that is `PopulatedFromSystemMemory` (driver-allocated) or `PopulatedByReservedDDRByFirmware`
  (firmware carve-out), `d3dkmddi.h:2559-2603`. Budgets come from Local/NonLocal budget groups
  (`:2586-2587`); VidMm caps PopulatedFromSystemMemory totals by the graphics share
  (`render_pool_demand.h:186-200` cites "Calculating graphics memory").
- D3D12 UMA is a UMD cap (`d3d12umddi.h:160`, `:6809`), already TRUE here.
- VidMm zero-fills a new allocation's memory before an application can see it (VIRTUAL_FILL via
  the paging process); `ExistingSysMem` allocations use the caller's pages and are not filled.
  This is a security contract, not a policy the KMD can disable.
- VidMm migrates only between segments in an allocation's supported set, or evicts to system
  memory under budget/commit pressure.

AGX/ASAHI CONTRACT: Asahi keeps a userspace BO cache (agx_bo_cache) and never zeroes on reuse;
the hardware sees only GPU VAs (UAT); there is no dedicated VRAM on M1.

TRANSLATION already in place: GPU-only and CPU-visible BOs live in segment 3 (Windows pages,
16 KiB-contiguous) with single-segment sets; native staging is aperture-only ExistingSysMem; the
pool is sized under the graphics share; DWM headroom is M2-f7c (`pool_windows.c:318`, `:571`,
`:923`). This already is the UMA model Microsoft permits and it shows zero steady-state paging.

What would remove the remaining (warm-up only) paging without reintroducing the OOM/c00001ad:
nothing in the segment model; the levers are UMD-side reuse (larger/pre-sized BO cache, size-
class rounding so 1 MiB/2 MiB requests hit) and they move work out of the first ~2 s only.
Dropping segment 3 for aperture-only would reintroduce 4 KiB scattered placement the UAT cannot
map (EXP1265-D/1267/1268) and the EXP1027 refusal; enlarging segment 2 contradicts the fixed
firmware reserve. Neither is justified by this evidence.

WHAT IS STILL UNKNOWN: (a) the split of the 845 UPT between 71.3-97 s and teardown at 99 s;
(b) whether any later GravityMark phase (camera/scene change) re-enters BO-cache misses.

## Q4. Ranked options

| # | Option | Removed work per frame (steady) | Risk | Single variable | Verifying receipts |
|---|---|---|---|---|---|
| 1 | **No memory-model change; proceed with V1 retained VM slots** (EXP2163 verdict) | memory: 0 (there is none); LEASE+TLBI ~2/frame x 160-272 us | none from memory side | none (memory) | n/a |
| 2 | Receipt-only: one more mid dump (e.g. probe iteration 18 in addition to 12) | 0 (diagnostic) | none | dump time | delta of profile Build[8/9/11], census Next, AllocLife Next between dumps inside the run; expected 0/0/0 |
| 3 | UMD BO-cache warm-up: raise `AGX_WIN32_BO_CACHE_LIMIT`/BYTES or round sizes to classes | 0 steady; up to ~768 creates + ~250 VFILL/UPT groups in the first ~2 s | cache holds more pool commit (pool 2.85/4 GiB now; M2-f7c headroom) | cache limit only | perf-reason "BO cache miss/evict" counts, AllocLife creates/s during 24-26 s, census VFILL count |
| 4 | Single aperture or single segment UMA rewrite | 0 (nothing to remove) | reintroduces EXP1265-D / EXP1027 / OOM, c00001ad | segment set | not recommended |

Recommendation: close the "migration churn" hypothesis as REJECTED by existing receipts; do not
spend a hardware run on segment policy. If confirmation is wanted, option 2 is receipt-only and
can ride along with the next V1 experiment (no extra boot).

## Decoding notes (reproducible)

- Census entry `<QIIIIQQQQQQ>` at 16 + 72*i; AllocLife entry `<QQQIIIIIIIIII>` (64 bytes) at
  16 + 64*i; sort by Qpc; divide by 24e6.
- Wom1G3PagingProfile: u64 {1, publish qpc, freq}, then 32 x {count, ticks, max} by DXGK
  operation, then UpdatePageTableEntries, 4 x 3 timing triplets, then V5 client Calls[32],
  Ticks[32], Max[32] (`receipts.c:3090-3113`; command ids `apple_agx_gpuva_broker_v5.h:34-49`,
  5 = UPDATE_LEAF, 7 = LEASE, 12 = REVOKE_BACKING).
- Mid-a: publish 71.3 s; UPT 45215, VFILL 10449, VXFER 170, FLUSH_TLB 42155; UPDATE_LEAF 1 155 291.
  After-d: publish 111.0 s; UPT 46060, VFILL 10452, VXFER 170, FLUSH_TLB 42880; UPDATE_LEAF
  1 345 943; LEASE 12630 -> 18157.
