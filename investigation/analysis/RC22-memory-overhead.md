# RC22 — remove a redundant descriptor CPU copy

Offline source candidate from `108fb4914e8cac4f01619c8d0d91a2aacee6f07e`.
No hardware, guest, builder, package or other-worktree operation. Integration
owns experiment/change ledgers. This does not establish a GravityMark speedup
or solve the 10k OOM on hardware.

## Evidence and RAM accounting

Saved evidence: `/Volumes/pwdev/public_windows/.local/experiments/EXP1957-fast-paging/evidence/EXP1957/gm1957b-umd.log`.
Line numbers below refer to that file when prefixed `log:`; driver paths are
relative to `drivers/apple-agx/`. The handoff has decoded `memory.json`.
`investigation/EXPERIMENTS.md:64007,64019` records EXP1956/1957 ACTUAL:
2k 6.4 then 7.7 scene presents/min, still 20.7 s maximum reported wait; 10k
fails at LOAD. RC21 coalescing was not a demonstrated cure for long waits.

At log:2778–2787 the first refusal is a 16,842,752-byte GPU-only General
allocation, at AllocateCb, before private staging creation. Snapshot:

| Category | Observed size | Physical-RAM interpretation |
|---|---:|---|
| System RAM | 6,279,270,400 B; available 692,957,184 B | Adjacent OS observation, not atomic with the slot scan |
| System commit used | 7,647,506,432 B | Commit obligation; not a measurement of bytes currently on disk |
| GM private / working set | 2,629,246,976 / 1,853,960,192 B | Private commit and resident process pages overlap; do not add them |
| Canonical | 1,991,098,368 B, 173 slots | Logical allocation spans, including pool-resident storage; not another 1.99 GB on top of the pool |
| Private staging | 465,174,528 B = 443.625 MiB, 90 slots | Ordinary committed process memory, additional to canonical storage; part of process private usage |
| Mapped private staging | 448,921,600 B = 428.125 MiB, 76 slots | Persistent CPU pointers; cannot discard merely because an upload completed |
| Unmapped private staging | 16,252,928 B = 15.5 MiB, 14 slots | Upper bound for a hypothetical unmapped-staging release at this instant, before proving pending-copy safety |
| Merge baseline | 0 B | The earlier mapped-only baseline optimization already eliminated this cost here |
| Resident-reference spans | 1,901,445,120 B | UMD residency ownership, not a physical-placement census |

The snapshot implementation (`render-admission/umd/src/umd_win32_screen.c:622–674`)
counts logical bytes and explicitly overlaps ownership categories. `Mapped`
means a slot map, not necessarily a VidMm CPU lock: T13 maps point into
VirtualAlloc storage (`:849–865,1238`). Canonical's unmapped 1,542,176,768 B
therefore does **not** mean it all has an evicted system-memory copy.

Additional categories and safe bounds:

- **Pool:** the task reports 2016 MiB locked for Fraction=3. The source's
  2016 MiB segment/reserve bound is `render-admission/src/pool_windows.c:693–713`;
  demand physical allocation uses MmAllocatePagesForMdlEx at :278. A cap is not
  itself an instantaneous committed-page count. High&&!Low remains :395–396.
- **VidMm backing for evictions:** preserved canonical bytes may occupy ordinary
  system RAM/pagefile outside the native pool. This is OS-owned, not another
  UMD VirtualAlloc. The supplied receipt has no physical placement, pagefile
  I/O or backing-store census; exact bytes are unknown and cannot be derived
  as canonical minus pool. The WDK's paging fence and NumBytesToTrim contract
  is `.local/reference/wdk26100/d3dukmdt.h:1466–1498`.
- **Upload/CPU-visible heaps:** log:32,42,52 show three 32 MiB owner heaps;
  :147 shows one 128 MiB heap (224 MiB created total). They are creation
  observations, not a live upload-only census; usage/lifetimes are not all
  identified. `mesa/winsys/agx_d3d12_windows.cpp:552–594` creates/maps only
  CPU-visible owner buffers. Their private maps are already in the 443.625
  MiB staging category, not an additional independent total.
- **C70/C80 texture staging:** one UploadStaging and one DownloadStaging per
  owner (`mesa/winsys/agx_d3d12_windows.cpp:96–107,2328–2425`). Reused across
  maps; their canonical/private allocations are already in the slot census.
  No per-owner size breakdown in these receipts. Releasing every upload would
  undo the allocation-churn fix and can precede pending GPU readers.
- **Mesa CPU copies:** upstream `agx_pipe.c:1031–1070` frees transfer scratch
  and releases staging resources on transfer_unmap, but the BO map persists.
  Windows native_map caches its map at `agx_win32_asahi_bo.c:935–973`.
  Thus Gallium Unmap does not prove it is safe to free the UMD pointer.
- **BO cache:** `agx_win32_asahi_bo.h:17–18` caps 128 entries / 64 MiB of charged
  storage. `agx_win32_asahi_bo.c:277–279` charges canonical plus its optional
  winsys Shadow; this is **not** a complete process-memory cap: UMD private
  staging/baselines and allocator metadata are not charged there. Under T13
  up to another canonical-sized private staging can exist. No exact cache
  census here. :350–363 reclaims only non-in-flight entries; :429–435 retries
  allocation once after actual disposal. The next refusal already has 25
  fewer canonical/staging slots and 10,420,224 fewer staging bytes. This is
  consistent with reclaim, not proof that all 443 MiB is dispensable cache.
- **Command pools:** projected batch pool/pipeline pool use AGX_WIN32_BO_SHADOW
  (`mesa/scripts/native-asahi-batch-lifecycle.py:123–130`), which allocates a
  separate aligned CPU buffer and publishes by copy (`agx_win32_asahi_bo.c:80–101,
  965–970`). Its size equals mapped BO size, in addition to underlying slot
  staging. No live pool-bytes census: unknown, not zero; shared BO-cache limits
  apply only after release. Changing publication/retirement is a separate fix.
- **Descriptors:** log:239/244,312/317,339/344 show three shader-visible pairs:
  131072 * 160-byte resource descriptors and 1024 * 128-byte sampler descriptors,
  each with a 64 KiB guard. Each pair's extra CPU mirror is 20.25 MiB, so
  60.75 MiB of duplicate commitment is created, and that much is retained if
  all three pairs remain live. The six guard tails alone total 384 KiB per
  storage layer. CPU-only heaps have a single required CPU copy. This extra
  descriptor shadow is **outside** the slot-staging receipt but inside process
  private usage (`d3d12/agx_d3d12_ddi.cpp:2748–2755,3333,3353–3363`).

Commit greater than physical RAM and low available memory support pressure as
an explanation, but do not prove pagefile I/O caused each paging-fence wait.
Physical disk bytes, hard faults and timed VidMm transfers remain unmeasured.
Do not turn the earlier 235 MiB of dropped references / 13.8 s into measured
memcpy or disk bandwidth (RC17/RC21 reports make the same distinction).

## Contract, selected fix and verification plan

The Windows DDI exposes stable CPU descriptor handles for the heap lifetime;
heap creation ABI is `.local/reference/wdk26100/d3d12umddi.h:826–832,1921–1922`.
The owner creates the allocation and persistent map. T13 owns its write-watch
and uploads before consuming work. The DDI owns only its optional second CPU
shadow. VidMm owns residency/backing; KMD owns paging and pool allocation.
No firmware, interrupts, DMA, power or recovery ownership changes.

Asahi keeps BO maps and reuses released BOs; Windows adds canonical/private
coherence and reference/fence rules. Upstream cache expiry/allocation retry
(`.local/reference/mesa/src/asahi/lib/agx_bo.c:81–161,366–383`) is not evidence
that every Windows cached object is safe to destroy while held. No external
code copied. m1n1/Mu/ADT/live registers are not consulted: this is a CPU storage
ownership defect, and the user forbids live work; no inherited hardware or
ACPI behavior is being designed or changed.

Selected behavior: query actual owner slot identity under OwnerLock then the
shared ScreenBufferLock. A matching Active, non-Transition, HeapStaging, Mapped
slot with PrivateStaging == LockedBase == CPU proves cached process memory.
Use that persistent map directly for descriptors. Other mappings retain the
EXP1855 cached shadow. No environment flag, guessed pointer attribute, size,
trace-specific content or descriptor-count admission rule selects the path.

`AgxD3d12OwnerCpuMapIsPrivate` is declared in the existing included owner header.
CreateDescriptorHeap skips the redundant VirtualAlloc and clears the reused
map to preserve fresh-shadow zero initialization even for a recycled BO. MirrorHeapForms
is naturally a no-op without a second copy; initialization and all CPU writes
land directly in the watched map. RefDescriptorData now chooses the real map
when mirror is absent, preserving validation. DestroyDescriptorHeap continues
to free a separate shadow only when mirror exists and releases native storage
through ReleaseHeap. Guards, descriptor strides and logical counts are unchanged.

Regression plan executed: extend the real Create/Destroy/Mirror source replay,
observe the old implementation fail its allocation-count invariant and the
first candidate fail recycled-BO zero initialization, then prove
both private-map and WC paths under ASan/UBSan. Exercise owner identity rejects,
GPUVA-disabled fallback, guarded/unguarded heaps, descriptor writes, map/shadow
failures and destruction. Run staging/residency/ABI tests, then the prescribed
full baseline. rc6/rc7/rc11 hold, trim, retry and paging-fence code is unchanged.

## Deferred candidates and pool recommendation

Do not free a live upload map, descriptor map or command pointer on upload
completion. Even D3D12 Unmap does not end this internal cached BO map. Safe
unmapped private-staging reclaim needs an explicit map/coherence lifecycle,
retry/download behavior and pointer invalidation; its snapshot ceiling here
is only 15.5 MiB. Avoid replacing a 60.75 MiB proven duplicate with that wider
change. Existing cache reclaim runs on allocation refusal; proactive budget
notifications require a safe owner-thread handoff and must preserve in-flight
references. No evidence of a large idle cache justifies that added concurrency
in RC22. CPU-map aliasing for command shadows is another separately testable
optimization; not part of this descriptor change.

For the first candidate keep **Wom1GpuFaultInfoRead=1, Wom1PoolFraction=3,
Wom1PoolHalfSize absent/0**. This measures the overhead fix against EXP1957.
`Wom1PoolFraction=2` does NOT select half size: only value 3 is parsed at
`pool_windows.c:745–770`; value 2 restores the full ordinary bound, potentially
making RAM pressure worse. A separate half-pool experiment uses the existing
**Wom1PoolHalfSize=1** (takes precedence), yielding 1344 MiB vs 2016 MiB.
It can leave up to 672 MiB more RAM outside the pool, but reduces local
residency capacity by the same amount and may increase VidMm backing/paging.
It is not guaranteed to save 672 MiB net physical RAM or improve frames.
No pool parser/default/cap change is included.

## Smallest hardware checkpoint (integration only)

WHY THIS HYPOTHESIS: (1) EXP1957 has low physical headroom and 10k allocation
failure; (2) source proves an unconditional full descriptor duplicate despite
T13 cached mapping; (3) the log identifies 60.75 MiB of such allocations and
the executable regression proves they can share one CPU map without changing
descriptor writes, guard bytes or ownership.

Base = RC22 HEAD including the fix; pinned WDK build, exact preregistered package
hashes and existing known-good recovery are integration's responsibility.
Keep prior switches/guard/firmware/signer/caps; 2k then 10k at the same settings
and measurement windows as EXP1957. First checkpoint: 2k enters the scene with
correct textures/descriptors, no validation faults, removal or DWM loss. Then
10k must finish loading without OOM. Collect process private/working set and
available RAM at equivalent load points/refusal receipts, scene presents/min
(after Ready), longest paging wait, and imagery. A refusal-only receipt may
not occur on success, so take an equivalent OS memory sample as well.

Expected direct effect is lower process private commitment, NOT a 60.75 MiB
drop in measure-memory-storage private_staging (that census never included
this descriptor duplicate). Exact savings at the failure point depend on
heap lifetimes; speed and no-OOM remain unknown. If RAM improves but waits do
not, do not call the performance cause confirmed. Collect evidence before the
established exact-package ordinary GPU-visible rollback; this offline task
performs none of those operations.
