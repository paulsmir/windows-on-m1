# RC29 — GravityMark allocation footprint and retained CPU copies

Offline base `5c4f1188d035fb040890fb2ee0130555e9769394`, branch `codex/rc29`.
Saved trace: `.local/experiments/EXP1957-fast-paging/evidence/EXP1957/gm1957b-umd.log`
in the shared repository data directory. No hardware or guest access.

The approximately 2 GB canonical footprint is predominantly requested storage,
not padding. The trace contains 119 creation requests: 73 buffer/heap requests,
29 texture requests (three fail), and 17 placed buffers that add no allocation.
Six descriptor backing heaps bring the heap receipt count to 79. Successful
creations total 1,812,398,080 heap/buffer bytes and 162,004,992 texture BO bytes.
These are **creation totals**, not a reconstructed live set: releases and cache
reuse are not completely logged. Do not substitute their sum for the first
refusal's measured 1,991,098,368 canonical bytes in 173 slots (trace:2778–2787).

## Reproducible per-resource audit

The handoff `rc29/audit.py` produces `resource-audit.json`, `heaps.csv` (79 rows),
`textures.csv` (29 rows), `placed-buffers.csv` (17 rows), `create-attempts.csv`
(119 rows), and `descriptors.csv` (10 rows). Every row names its trace line;
failed texture requests remain explicitly failed. `memory.json` is the existing
`tools/decode_umd_memory.py` output. The helper compiles the existing native
layout replay's actual constructor/query plus the pinned AIL library; generated
MIT-noticed source stays local in `.local/rc29-audit/layout.c`, outside Git.

Texture nominal bytes are the sum of whole format blocks over requested mips
and layers. The helper then executes the native modifier/setup/AIL policy,
including separate depth/stencil allocation, followed by the real BO granule
rule. It records native bytes, BO bytes, metadata, allocated mip count and the
DDI's reported size separately. The DDI's 16x16 estimate is not the native BO
allocation: `agx_d3d12_ddi.cpp:1837`, `agx_d3d12_windows.cpp:1155`, and native
`agx_pipe.c:582` demonstrate the distinct paths.

Receipts omit sample count. Matching the DDI sizing formula against legal
1/2/4-sample candidates uniquely gives one sample for 25 native texture rows.
The 16x16 Z32 texture at trace:1916 remains ambiguous; every legal sample count
still rounds to one 64 KiB BO. Its AIL/metadata columns use the explicit
single-sample assumption (1152 / 128 bytes); the BO total is unaffected.
Three ordinary RGBA8 presentation surfaces use the existing linear 1280x720
allocation path, 3,735,552 bytes each. No extra multisample native texture
receipt precedes them. All 26 native texture rows are immediately followed by
their corresponding committed-texture receipt.

Only 23 `measure-bo-create` rows exist: that timer receipt logs allocations
lasting at least 10 ms and truncates size to MiB (`agx_win32_asahi_bo.c:20,514`).
It cannot supply an exhaustive byte-exact BO census. Heap descriptions omit
some fields and have repeated size queries; the audit counts **creation**
receipts once, and does not join repeated `heap-sizes` queries as allocations.

## Largest sources, bytes and disposition

Driver paths below are relative to `drivers/apple-agx/`; primary Mesa paths
are under `.local/reference/mesa/`.

| Source | Bytes in saved evidence / reconstruction | Evidence and disposition |
| --- | ---: | --- |
| GPU-only application buffer requests | 1,225,038,220 nominal; 1,228,537,856 allocated, 65 creations | `d3d12/agx_d3d12_ddi.cpp:1929,2116,2534`; `mesa/winsys/agx_d3d12_windows.cpp:560`. Only 3,499,636 B rounding (0.286%). Keep requested content and 64 KiB allocation granule. |
| CPU-facing buffer requests | 369,164,288 nominal and canonical, six creations | Trace:15,29,39,49,2582,2683; five upload buffers are 3*32 MiB + 2*128 MiB, plus a 64 KiB runtime buffer. Allocation has no padding here. Persistent pointers prevent freeing their private copies at copy retirement. |
| Private CPU staging | 465,174,528 live B, including 448,921,600 mapped and 16,252,928 unmapped | Trace:2780; `render-admission/umd/src/umd_win32_screen.c:790,851,908`. Overlaps process private bytes. RC25 already provides an opt-in direct-pool alternative with pinning/WC tradeoffs; this task does not promote it. |
| Texture-only heap backing plus separate placed textures | 150,994,944 B in two parent heaps; 10,420,224 B in successful separate placed texture BOs | Trace:144,2555; WDK `d3d12umddi.h:307` flags=2; `agx_d3d12_ddi.cpp:2448` explicitly allocates each placed texture independently. Parent capacity is application-requested. Proper suballocation could avoid extra texture BOs, but must implement native offsets, alias contents, ownership and lifetime; dropping the heap promise or merely inventing smaller texture sizes is not that implementation. Deferred. |
| Descriptor backing | 63,700,992 canonical B = three 20.25 MiB resource/sampler pairs; same amount private | Trace:238/243,311/316,338/343; `agx_d3d12_ddi.cpp:3337,3352`. The saved run predates RC22's removal of a third descriptor CPU copy. RC22 is already in this base; its 63,700,992 B saving is not new RC29 savings. Preserve 393,216 total guard bytes per backing layer. |
| Texture physical overhead | 3,747,504 B over 158,257,488 nominal B across 26 successful textures | `textures.csv`: native/format expansion, tiling, metadata, layers, tails and 64 KiB rounding. Includes 1,359,872 B for the 1280x720 depth/stencil split and 327,680 B for a 32-layer 64x64 RG8 texture. Required by current AIL, unchanged. |
| Compression metadata | 164,096 B in successful textures under the stated sample assumption | AIL `layout.c:269–313`; this is a subset of texture overhead, not another additive category. Largest BC cubes have zero AGX metadata. Removing it cannot explain or cure GB-scale usage. |
| Command encoder/pool CPU shadow over existing private T13 memory | One entire BO-sized extra allocation per mapped SHADOW BO; saved live total is not logged | `mesa/winsys/agx_win32_asahi_bo.c:81,978`; `mesa/scripts/native-asahi-batch-lifecycle.py:123`. **Fixed:** authenticate and reuse the UMD's cached pointer. Separate WC shadow remains where needed. |
| BO cache's omitted private storage | Previously a 67,108,864 B charged cap omitted up to two owner-private copies | `agx_win32_asahi_bo.c:283–292`. **Fixed:** charge private staging and merge baseline as well as canonical/winsys shadow, and retain insertion charge for all removals. Cache contents at the snapshot are not logged. |
| Merge baseline | 0 live B in all four refusal snapshots | `umd_gpuva_windows.c:1150` allocates only for mapped GPU-written T13 slots. No savings claimed for this run, but cache accounting must cover such workloads. |
| Remaining process private usage | 2,164,072,448 B after subtracting measured private staging | Process private total is 2,629,246,976 B, trace:2787. App assets, compiler/CPU objects, descriptor/command copies and other process allocations are not separately traced. This remainder cannot be attributed to GPU BO padding or to this driver from these receipts. |

The saved C70 upload-staging receipts show 256x256 and 512x256 reused textures,
not a retained 128-MiB conversion texture (`agx_d3d12_windows.cpp:2332–2371`).
The two 128-MiB CPU buffers are application upload heaps. C70/C80 resources keep
one reusable allocation each; freeing one after each copy would reintroduce
allocation/paging churn, and would not release application upload maps.

## Byte-exact layout contract

No AIL layout, alignment, metadata, allocation granule, format or mip-tail rule
is changed. Primary source requirements inspected:

- `src/gallium/drivers/asahi/agx_pipe.c:136–159,508–532,582`: native setup,
  inferred image bind, modifier policy and allocation of `layout.size_B`.
- `src/asahi/layout/layout.c:73–214`: 16 KiB tiles, POT mip tail, cache-line
  level padding and page-aligned writable/renderable array layers. Small
  4x4 BC arrays can need 16 KiB **per layer**, despite nominal bytes below 1 KiB.
- `layout.c:269–313,351–387`: exact metadata, all hardware-addressed mip levels,
  and final cache-line alignment. Truncating the tail to requested mip count
  violates hardware stride requirements.
- `agx_pipe.c:2492–2497` and `src/gallium/auxiliary/util/u_transfer_helper.c:206–228`:
  split stencil and convert Z24 to Z32F before native layout.
- `mesa/winsys/agx_win32_asahi_bo.c:397–400`: 64 KiB BO rounding/alignment;
  `d3d12/agx_d3d12_ddi.cpp:2410–2436`: placed buffers already alias heap ranges,
  so the 17 placed-buffer receipts must not be counted as separate allocations.
- WDK `.local/reference/wdk26100/d3d12umddi.h:294–328,573–591`: CPU page property,
  heap flags/size/alignment and separate resource/additional-data size fields.

## Implemented storage contract

`umd_asahi_owner.c:121–147` queries an exact active native key, backend and
construction serial under ScreenBufferLock, refusing transitional or closing
state. `PrivateMap` is exposed only for a mapped T13 allocation whose locked
base equals its private staging, without SystemDirect or borrowed staging.
The storage query also returns actual retained private-copy bytes. All existing
owner factories initialize the optional callback; owners without it retain the
original map fallback.

`agx_win32_asahi_bo.c:978–994` reuses that exact pointer for a command shadow BO
without allocating a winsys Shadow. `:95–107` recognizes publication through
that retained backing pointer. The established UMD write-watch/coherence path
(`umd_gpuva_windows.c:1136–1265`) uploads its writes; canonical storage remains
separate. `release_map` clears exposed pointers and disposal frees only a
separately owned winsys Shadow. No application pointer lifetime changes.

Cache admission sums all copies with bounded arithmetic. `CacheStorageBytes`
records that charge at insertion; every take/evict/flush/reclaim debits the
snapshot. Reuse still requires exact size/flags/class and no in-flight reference.
A failed owner query refuses cache admission. A refused destruction remains
registered for collection; the 64 MiB cache cap is not a bound on all pending
in-flight or failed-destruction storage. The UMD creates a required merge copy
before submission (`umd_gpuva_windows.c:1946`, then upload at `:1150`), so a
cached BO does not acquire a new merge copy on completion.

UMD owns CPU storage/coherence; winsys owns extra shadows and cache references;
Mesa owns layout; VidMm/KMD own placement, paging and guaranteed pool backing.
m1n1/Mu, initialization, interrupts, DMA, power, signer, caps and recovery are
unchanged. No new live machine facts are inferred, no external code is copied,
and no hardware-validated assisted/standalone launch contract is altered.

## Expected savings and limits

Command savings are exactly the sum of mapped SHADOW BO sizes that authenticate
as private T13 storage. The saved receipt set does not identify this sum. If all
created CPU/descriptor heaps remain live at the first refusal, subtracting their
432,865,280 B from mapped staging leaves at most **16,056,320 B (15.3125 MiB)**
for other mapped private BOs, including command pools and conversion staging.
That is a conditional upper bound on command savings, not a measured result.
The broader private remainder is 32,309,248 B including unmapped BOs.

With zero merge copies, a full old cache could retain 64 MiB of uncharged T13
copies in addition to its charged 64 MiB. The new budget constrains the charged
sum to 64 MiB. With merge copies, old hidden storage could be larger. Actual
savings depend on occupancy, safe destruction and reuse; do not add this generic
maximum to the command upper bound because their storage sets overlap.

These are two verified removable overheads, not a claim that the 10k workload
now fits RAM or runs faster. Exact attribution of the remaining process private
memory needs process heap/VirtualAlloc profiling in the next integrated run.

## Verification and next checkpoint

The original implementations failed the new command-shadow and owner-private
cache regressions (`red-shadow.log`, `red-cache.log`). Final targeted suite:
83 tests pass; fresh reviewer: 17 focused tests pass, no blocking findings.
Actual C/C++ replays run under ASan/UBSan. The prescribed baseline reports:

```
ran 541 tests in 232 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

Six known failures remain, without edits to known_failures.txt. The handoff
REPORT names them and supplies exact commands, commits and artifact hashes.
Native Windows compilation and hardware remain unvalidated.

Integration candidate: base HEAD plus both RC29 fixes; preserve firmware,
caps/signer, recovery, T13 default and all other settings. Set
`Wom1GpuFaultInfoRead=1`, `Wom1PoolFraction=3`, HalfSize absent/0. First verify
command/descriptor bytes through reuse and retirement, then GravityMark D3D12
**2k, then 10k**. Record canonical bytes, private-staging and process-private
bytes at matching phases (also on success), scene presents/min, longest/count
of paging waits, load OOM/device removal and rendered correctness. Compare with
a same-base control; more cache eviction can trade memory for allocation churn.
Preserve known-good recovery and collect evidence before ordinary exact-package
GPU-visible cleanup. No experiment was run or preregistered by this task.
