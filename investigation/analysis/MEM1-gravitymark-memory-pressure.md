# MEM1: recover unused BO cache storage on allocation refusal

Offline analysis of EXP1925; a software-tested pressure recovery candidate,
not proof that GravityMark fits, completes, or preserves DWM under pressure.
Task artifacts: shared `.local/handoff/codex/mem1/`. No hardware was accessed.

## Evidence and accounting

The original evidence is shared
`.local/experiments/EXP1925-heap-staging/evidence/EXP1925/`.
`gm1925b-umd.log:3875,3894,3916` records three failed 128 MiB creations;
`:3929` records the final 0x730000 (7.1875 MiB) failure, underlying 8007000e.
`gm1925a-umd.log:4163` records the same final size. These are failures, not
proof of which preceding resources remained live. The prior EXP1925 report
localizes the subsequent unchecked transition allocation; that guard already
exists on this branch and is not reimplemented here.

Decoded `kmd-receipts-probe-end.txt` Wom1M2PoolDemand v7 (304 bytes):

| Counter | Value | Interpretation |
| --- | ---: | --- |
| RAM | 5988.37890625 MiB | Managed RAM used for segment calculation |
| Segment 3 | 2688 MiB | Advertised capacity, not free physical memory |
| Peak reserved / committed | 2505.25 / 2505.25 MiB | Separate maxima, not necessarily simultaneous |
| Final reserved / committed | 127.375 / 127.375 MiB | Snapshot after workload, not failure-time ownership |
| Final held | 160 MiB | Includes committed pages; do not add to committed |
| Peak slabs | 1254 (2508 MiB) | Physical backing high-water mark |
| ReserveFailures | 7 | Creation-time promise refused |
| NewSlabFailures / CommitFailures | 0 / 0 | No failed physical slab allocator or placement commit recorded |

`render_pool_slab.c:78` increments ReserveFailures only for a MayGrow veto or
failed AddSlab; AddSlab increments NewSlabFailures at `:54`. Thus the seven
recorded refusals are headroom-gate refusals, assuming the saved receipt's
counters are coherent, not evidence that segment 3's numerical cap was reached.
The v7 wire receipt does not contain FloorRefusals or sampled event states.
`pool_windows.c:392` requires HighMemoryCondition and no LowMemoryCondition.
Do not remove that protected T8 policy to make allocations succeed.

Heap receipt reconstruction (`memory-evidence.txt`) finds cumulative successful
buffer-heap creation of 1984.375 MiB (10k) and 1856.4375 MiB (2k). In each run,
480 MiB is WRITE_COMBINE (three 32 MiB and three 128 MiB creations), 0.0625 MiB
WRITE_BACK, and the remainder NOT_AVAILABLE. The 2k run additionally refuses
three 128 MiB requests. Decode follows `agx_d3d12_ddi.cpp:1940,2130` and WDK
`d3d12umddi.h:294` (NOT_AVAILABLE=0, WRITE_COMBINE=1, WRITE_BACK=2).
These are cumulative creation receipts, not a peak live set or the entire
allocation inventory: native textures, batch pools, cached BOs, staging and
other processes also consume memory. The general native-allocate diagnostic
is incomplete (only 254.625 MiB of successes retained per run), so summing it
as if complete would be misleading.

`gm1925b-umd.log:2708,2712,2809,2813,2993,2997` records three 128 MiB CPU-facing
BOs, tokens 169/171/174, with private staging and live owner/heap CPU pointers.
The saved run has 1,149,861,888 bytes uploaded and no downloads; 10k has
1,151,078,400 uploaded and no downloads. Upload traffic is not retained memory.
No supplied lifetime trace proves that those CPU views can be freed after load.

## Candidate audit and owner contracts

Paths below are relative to `drivers/apple-agx/`, except references/tests.

| Candidate | Source evidence and decision |
| --- | --- |
| Canonical plus staging | `render-admission/umd/src/umd_win32_screen.c:642,646,696,707` creates one canonical allocation and then process VirtualAlloc storage for T13. It does not reserve the staging as a second pool allocation. This is real additional system commit/RAM, not duplicate pool accounting. GPU-only flags retain their existing path. |
| Lazy/free CPU views | `mesa/winsys/agx_d3d12_windows.cpp:439,466` eagerly maps CPU-visible owner heaps. Moving staging allocation to map would not avoid these mapped heaps. `umd_gpuva_windows.c:1024` adds a merge baseline only to GPU-written slots; zero downloads does not prove zero shadows. Freeing mapped storage without ending pointer ownership would violate the CPU contract. Defer a broader lifetime design. |
| Reservation vs residency | `render-admission/src/allocation_windows.c:522,530` reserves once per class allocation; `:665,667` forgets placement and unreserves on destruction. `render_pool_slab.c:22,28` retains backing for min(Reserved, ReserveCap), even after placement is evicted. Reservation and committed counters overlap; adding them doubles the apparent pool footprint. |
| Granule/slab rounding | `mesa/winsys/agx_win32_asahi_bo.c:368` rounds once to 64 KiB. `pool_windows.c:530` converts that aligned size to granules; `render_pool_slab.c:83` grows in 2 MiB slabs. The 128 MiB and 0x730000 failures are already granule-aligned, so rounding adds zero to those requests. There is no evidence supporting a page-size change. |
| Headroom floor | `pool_windows.c:392,401,534` gates new slabs and reclaims safe surplus. Existing promises and referenced placements survive. Refusals below 2688 MiB are consistent with this policy; preserve it. |
| BO retention | `mesa/winsys/agx_win32_asahi_bo.h:17` caps cache at 128 BOs / 64 MiB per backend. `agx_win32_asahi_bo.c:295,322` retains released allocations until cache turnover or shutdown. Before MEM1, failure at CreateBo immediately returned NULL. Upstream `.local/reference/mesa/src/asahi/lib/agx_bo.c:375` evicts cache and retries. This missing pressure path is the selected concrete defect. |
| VidMm eviction | `allocation_windows.c:558` sets EvictionSegmentSet=0; its existing comment at `:514` expects system-memory eviction for local-only BOs. `gpuva_g3_paging_windows.c:1066` implements local/system transfer validation. Neither establishes whether VidMm actually evicted this workload. Even successful eviction does not erase the lifetime pool promise. Changing segment sets cannot safely substitute for releasing unused BOs. |
| Reported budget | `render-admission/include/render_pool_demand.h:118` documents Windows graphics-memory accounting; `render_pool_demand.c:260` applies the RAM/aperture/broker bounds. `memory_windows.c:14,100` assigns budget groups and distinguishes reserved firmware DDR from system-populated pool memory. Pinned WDK `d3dkmddi.h:2605` defines segment Size/CommitLimit; SDK `winsdk26100/shared/dxgi1_4.h:1303` defines the OS Budget/CurrentUsage query. No captured QueryVideoMemoryInfo values or app budget decisions establish over-advertisement. Do not change capacity/caps based on that hypothesis. |
| Placed textures | `d3d12/agx_d3d12_ddi.cpp:2437` explicitly gives placed 2D textures separate twiddled BOs instead of suballocating the parent's backing. Committed 2D uses one owner texture and returns before CreateHeap. The trace contains no-heap texture calls but lacks full parent/range/live-ownership evidence. Possible excess backing needs a separate alias/lifetime design; not safe to fix by dropping the parent heap. |

Windows Mesa winsys owns the selected cache-lifetime defect. KMD owns physical
backing, reservations, paging/DMA and interrupts; m1n1 owns broker/stage-2;
Mu owns firmware exposure. Initialization, power and recovery contracts are
unchanged. The work uses saved EXP1925 launch/recovery evidence; no live ADT,
register, IRQ, guest or firmware measurement was made. m1n1/Mu submodules are
uninitialized here; no hardware-protocol conclusion depends on their source.
The inspected upstream agx_bo.c has an MIT notice and Mesa licenses/MIT permits
use with notice retention. No external implementation body is copied.

## Minimal change and regression

`agx_win32_asahi_bo.c:335` now reclaims idle cached BOs through existing dispose.
It preserves current Held and Older submission entries, their order and their
accounting. A refused dispose leaves its registered refcount-zero BO uncached,
so existing Collect can retry. `:416` retries allocation exactly once, only for
a callback refusal with no retained Backing.Live, actual reclamation, and a
healthy/nonclosing backend. Range/state refusals and ordinary success do not
purge the cache. A retained allocation still enters the existing terminal
UnpublishedBo path. No special resource size, content or app name is admitted.

`tests/test_g4_bo_pressure.py:19` compiles the production allocation transaction
and cache functions with ASan/UBSan. RED reproduces failure despite idle cache
storage. GREEN covers successful reclaim/retry, cache reuse, Held/Older skips,
order/byte accounting, refused disposal, persistent refusal, empty cache,
non-callback errors, retained rollback and failure during reclamation.
OS-facing creation/disposal are fixture substitutes; this proves the new
control flow, not Windows callback timing or end-to-end rollback.

The required baseline on the unchanged final implementation printed:

```
ran 432 tests in 182 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

Focused BO/cache/placement/staging/KMD-header run: 24 tests passed (one existing
missing-field initializer warning in the dispose fixture). Expanded targeted
run: 30 tests, 28 passed, two pre-existing errors outside the
baseline selection: missing m1n1 header in
`test_apple_agx_render_pool_demand.RenderPoolDemandTest.test_broker_capacity_matches_m1n1`,
and assertion line 173 in the existing
`test_apple_agx_pool_headroom.PoolHeadroomTest.test_windows_admission_and_pressure_reclaim`.
All code/tests consumed by those failures are unchanged from the starting HEAD.
They remain reported, not suppressed or added to known_failures.txt. The
independent read-only reviewer found no blocker and passed the new regression.

## Smallest checkpoint and unknowns

WHY THIS HYPOTHESIS: (1) the saved receipt proves creation headroom refusal;
(2) cache hit/miss counters show the cache active during the run, and the
implementation retains up to 64 MiB without pressure reclamation; (3) Asahi's
reference allocation path supplies the missing reclaim/retry contract.
Cache occupancy at the exact refusal is unknown. This is a bounded recovery
opportunity, not an explanation of the whole 2.5 GiB peak.

Integration should rebuild this UMD for the existing EXP1925 configuration,
keeping T7h/T11b/T13, protected C75c/T8 behavior, firmware, signer, caps and
recovery fixed. Repeat the same short 2k profile to the first refusal, then
10k only if appropriate to the registered series. Expected new perf reasons:
`BO cache pressure release` and `BO cache pressure retry`. They use the existing
aggregated perf channel, so absence in a truncated final window is inconclusive.
Correlate allocation results, peak/reserved/held bytes, Execute progression
past 47, GPU fault/reset and timestamped DWM/UMD errors. Where available collect
process private commit, system commit/available RAM and LOCAL budget/usage.

Success for this change: idle cached allocations are destroyed and one retry
succeeds, or persistent refusal is bounded and safely propagated by the existing
transition guard. Reject any new ownership fault, hang, AV, GPU fault or DWM
exhaustion; controlled OOM is not benchmark completion. If reclamation occurs
but still refuses, stop this hypothesis and measure live ownership/commit;
do not raise limits or disable the floor. Preserve ordinary GPU-visible exact
package recovery and collect evidence before cleanup, owned by integration.

Unknown: exact live heap/texture/cache ownership at failure; actual VidMm
paging; commit-limit vs physical headroom contributions; native WDK/MSVC
compilation; subsequent benchmark correctness/performance and DWM stability.
Reclaiming at most 64 MiB of canonical cache plus associated CPU storage may
help a 7.1875 MiB request, but does not promise a 128 MiB allocation or full fit.
