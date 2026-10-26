# RC53: distinguish low memory from neutral headroom

2026-10-26. Offline candidate; no WDK build or hardware validation.
The integration owner's rc53/TASK.md explicitly authorizes revision of the
EXP1914-protected HighMemory admission rule. This supersedes that rule only;
DWM survival remains an acceptance requirement, not a demonstrated result.

## Cause and source evidence

D = drivers/apple-agx/render-admission. Old line numbers refer to base
2a7acf14; current line numbers below refer to this candidate.

- Base D/src/pool_windows.c:389-396 permits growth only when HighMemory exists
  and is set and LowMemory is absent or clear. Both-clear is therefore a
  deterministic refusal. Windows has separate low and high thresholds: the
  neutral interval is not a low-memory notification. See the stored Microsoft
  Standard Event Objects audit in M2-f7c-dwm-headroom.md:60-78.
- Base D/src/render_pool_slab.c:101-114 checks MayGrow before every new slab;
  a negative result increments FloorRefusals and ReserveFailures, rolls back
  the promise and frees only this transaction's new slabs. NewSlabFailures=0
  means the page allocator did not fail, not that allocation would succeed.
- Base pool_windows.c:537-551 samples events after failure/rollback. There is
  no cached headroom Boolean; the old gate reads the event each time. Those
  later event values cannot establish the decision-time values.
- Saved EXP1973-readiness-cache/evidence/EXP1973/kmd-receipts-probe-end.txt:161
  has ReserveFailures=FloorRefusals=64, CommitFailures=NewSlabFailures=0.
  Last request=24.75 MiB, reserved=3019.375, held=3020, committed=2985.875.
  Required promise=3044.125 MiB, below cap=3136. Both later samples are clear:
  entirely consistent with the old neutral veto, without requiring a race.
  The exact event responsible at the historical instant cannot be recovered.
- Saved EXP1975-hd-scanout/evidence/EXP1975/kmd-receipts-probe-end.txt:161 has
  ReserveFailures=FloorRefusals=2, later HighMemory=set, LowMemory=clear.
  That combination would PASS the old gate. A notification changed between
  decision and snapshot (or within the sequential samples); the stored values
  are provably not the veto's inputs. rec1975h/kmd-receipts-pre-disable.txt:161
  retains the same failure. This does not identify which event changed.
- The floor is min(live reservations, cap)+private leases, not a refusal at
  cap. D/src/render_pool_slab.c:22-31,82-119 retains the floor, permitting
  additional live promises over cap when the full segment is already backed.
  Held includes committed slots; do not add those quantities.

Saved paths above are under /Volumes/pwdev/public_windows/.local/experiments.
Handoff rc53/evidence.json pins original log hashes and full decoded fields.

## Windows / AGX / ownership contract

Pinned SDK km/wdm.h:23198-23205 permits KeReadStateEvent through DISPATCH_LEVEL;
:29827-29839 declares MmAllocatePagesForMdlEx. The existing fast mutex protects
all pool accounting; events may change concurrently with a sample. The stored
Microsoft study M2-f7c-dwm-headroom.md:36-78 documents nonpaged physical pages,
FULLY_REQUIRED, separate high/low notifications and graphics-memory accounting.
No supported numerical available-page query was established in those headers
or the stored study. This change uses the task's explicitly allowed LowMemory
notification option; it does not invent a private NT information class or turn
an old available-memory observation into a numeric threshold.

D/src/allocation_windows.c:531-542 reserves at CreateAllocation and propagates
STATUS_NO_MEMORY. The caller does not publish a successful allocation on
failure. D/src/render_pool_slab.c:129-158 leaves already-admitted paging ungated.
The RC28 serial restore invariant remains min(live,cap), with private leases
additional. No changes to segment capacity, budgets, memory types, PTE geometry,
CPU pointers, UMD residency or graphics capabilities.

Primary Asahi reference Mesa src/asahi/lib/agx_bo.c:355-390 tries cached backing,
allocation and eviction, then returns NULL on failure. It does not define a
Windows DWM headroom threshold. No external implementation is copied.
Saved m1n1 075e3980 source in handoff rc46/source-evidence/m1n1-075e3980,
src/hv_agx_pool_window.c:119-150,186-194 requires no broker holds and acknowledged
unmap/synchronization before reuse. Saved Mu 361a2134 source in handoff
rc30/source-evidence/Mu-361a2134, MemoryInitPeiLib.c:440-459,498-514 preserves
low RAM alias ownership and firmware carveout. Those contracts are unchanged.

VidMm owns placement, budget and backing store; KMD owns native MDLs, creation
admission and safe reclaim; m1n1 owns grants/stage-2/translation revocation; Mu
owns boot RAM/ACPI. Initialization, runtime interrupt/DMA protocols, power and
recovery retain their existing owners. Live ADT/registers were not queried:
hardware and guest access are prohibited. The task's saved launch evidence is
the reference; no assisted/standalone launch is newly certified.

## Admission, reclaim and DWM safety argument

D/src/pool_windows.c:390-432 separates two decisions:

1. Before EACH additional reservation slab, sample LowMemory. Set refuses;
   clear permits an attempt to obtain fully verified backing. A missing low
   event refuses as **signal_unavailable**, not as measured low memory. A
   missing or clear high event alone no longer refuses growth.
2. Retain the original early-reclaim predicate: absent/clear HighMemory OR
   absent/set LowMemory bypasses idle aging and the spare cache. It releases
   only safe surplus, retaining owners, pins, paging/sequence guards, broker
   acknowledgement and promised backing. Reserve still reuses held slots
   before reclaim, avoiding gratuitous discard-and-refill.
3. Refuse cleanly at creation if the floor cannot be backed: allocator failure
   or arithmetic overflow. Preserve exact rollback. An already backed promise
   needs no page allocation, so it remains admissible during pressure.

The safety improvement over a naked Low-only gate is keeping early surplus
return, including RC51 periodic retries after independent broker release, while
LowMemory still stops growth per 2 MiB slab. DWM is not protected by allocator
success alone. M2-f7c records DWM c00001ad after C73 opened the neutral interval,
and PRESSURE refusals at 347-390 MiB available; the old high-gate observation
was about 740 MiB. These are measurements, not universal safe thresholds.

This candidate has **no numeric system reserve and no proven DWM guarantee**.
If all backing is still promised/reachable, early reclaim cannot return it.
Other consumers, commit exhaustion, and event/allocation races can still exhaust
memory; admitted paging overlap remains allowed. The permitted low-notification
policy needs the DWM checkpoint below before replacing the validated package.
It makes neutral-state admission correct under the stated rule; it does not
prove every workload fits or that low-only headroom suffices on this machine.

## Refusal receipt v9

D/include/render_pool_demand.h:88-138 extends Wom1M2PoolDemand to 432 bytes,
version 9. The v7 prefix (304 bytes) and v8 field offsets (360 bytes) remain.
V9 changes event meaning explicitly: samples used at admission, not later
post-failure samples. High and Low are sequential, not a global atomic snapshot.
For arithmetic refusal neither event is sampled (code 2).

D/src/pool_windows.c:393-410 records the actual last growth decision; :550-589
copies it only on failed reserve. Original request/reserved/held/committed fields
remain after rollback and before reclaim. Added fields: Reason, RequiredGranules,
HeldGranules at decision, ReserveCapGranules and PrivateGranules. RequiredGranules
is zero/not evaluated for arithmetic failure. Reasons are 1 low_memory,
2 signal_unavailable, 3 backing_allocation, 4 arithmetic. Cumulative counts for
all four reasons distinguish repeated failures even when receipt staging coalesces
calls. A successful reservation/reclaim does not erase the previous failure.
Private-lease MayGrow calls may update scratch but do not publish a reservation
failure or increment its counters; private lease telemetry is outside this receipt.

tools/decode_wom1_gpu_fault.py:390-434 accepts exact v7/v8/v9 sizes, labels sample
semantics, retains unknown enums, and reports v9 decision terms/totals. It never
invents v9 reasons from historical samples. Registry receipt code uses sizeof
and needs no hand-coded length change.

Inherited limitation: pool unlock precedes staging (pool_windows.c:508-509);
concurrent publishers can stage an older snapshot after a newer one. Counts
include every refusal in memory; persisted receipts are snapshots, not a lossless
per-attempt journal or guaranteed newest totals. This task does not alter the
receipt worker/lifetime/lock-order protocol.

## Verification and next checkpoint

Tests execute extracted production Windows policy/wrappers with real portable
slab/demand allocators, in C and C++ linked to C, under ASan/UBSan. Neutral
admission and stale receipt sampling each failed before their fixes. Tests keep
early reclaim, missing/set/clear events, partial rollback, backing/overflow
reasons, success-preserves-evidence, serial restores under denied physical
allocation, over-cap promises, owner/pin/paging/sequence/busy safety. The decoder
fixture emits bytes through the actual C declaration and checks old layouts.
Commands and final results are in handoff rc53/REPORT.md.

WHY THIS HYPOTHESIS:
- EXP1973/1975 failed solely at the policy gate, with no allocation/commit failure.
- EXP1973's attempted floor is below capacity and both sampled events are clear.
- EXP1975's later samples would admit, proving decision provenance is missing.

Integration owner only: one hash-pinned candidate, unchanged firmware/caps,
known-good GPU-visible recovery preserved. Run **HD 100k then HD 200k** and
**FHD 100k then FHD 50k** as consecutive same-boot arms per pair. Both arms in
each pair must load and finish. Capture dimensions, scene load, pixels, score,
DWM PID/exit/WorkingSet/commit, system available physical and commit/pagefile,
process exit, v9 receipts before/after each arm and after reclaim settles.
Require desktop/SSH survival, no DWM c00001ad, no 0x10E, and byte-correct restore
of older resources while growth is refused. Neutral-memory refusal must now
be attributable to backing/arithmetic failure, not HighMemory absence. Also
exercise LowMemory refusal and recovery through ordinary allocation pressure.

Reject on DWM restart/starvation, corrupt restores, new paging failure, or either
arm failing to load. Collect dumps/receipts first, then exact-package cleanup by
the existing GPU-visible recovery contract; GPU-hidden only emergency fallback.
No experiment is run or preregistered by this offline branch. Remaining unknowns:
actual Windows scheduling/allocator/event timing, WDK build, physical/commit
headroom adequacy, cross-arm memory ownership and actual benchmark outcome.
