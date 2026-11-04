# L2: splitting the adapter-wide G3 lock

Status: offline design only (no code changed). Evidence: EXP2157 lock profile
(the DWM completion waits 0.14-4.4 ms on `ADMISSION_G3_STATE::Lock` while
GravityMark holds it in the preflight parse). All paths below are relative to
`drivers/apple-agx/render-admission/src/` unless they say otherwise; `g3.c` is
`gpuva_g3_windows.c`, `pg.c` is `gpuva_g3_paging_windows.c`, `graph.c` is
`shared/src/apple_agx_gpuva_g3_graph.c`.

## 0. Facts that constrain the design (from the source)

- F1. Graph "reads" write to the graph. `find_table` sets `graph->TableHint`
  (graph.c:129). `ContainsRangeAccessCached` writes `Proofs[]`/`NextProof`
  (graph.c:786-832). The parse callback `AdmissionG4GraphAccess` uses the
  cached variant (g3.c:4398). Two concurrent walks of one graph can tear a
  `Proofs` entry, and a torn proof can admit an unmapped range. So every graph
  walk needs **exclusive** ownership of that graph. Shared (reader) mode is
  not safe unless the caches change.
- F2. The graph refers to adapter-global objects. `Graph.Client` is
  `&state->Client` (g3.c:2947). `Graph.Registry` is `&state->Registry`, which
  holds frames shared by all processes (graph.c:189-246). The V5 client is not
  safe for concurrent calls. It has one mailbox page, `++client->Sequence`, the
  `Failures` ring and the `Calls/Ticks` arrays, all unlocked
  (`shared/src/apple_agx_gpuva_broker_v5_client.c:17-126`). Any broker call or
  Registry change still needs a global (or broker) lock.
- F3. The parse callbacks never call the broker or touch the Registry. The walk
  in graph.c:689-840 does not touch `Frame`, `Registry` or `Client`.
  `AdmissionMemoryRuntimeLocalView` (memory_runtime_windows.c:791) takes no
  lock.
- F4. The preflight is advisory. The UMD comment at
  `umd/src/umd_gpuva_windows.c:2582-2584` says to keep "the ordinary
  submit/BeginJob validation". Submit re-parses at g3.c:5965 and BeginJob
  re-proves at g3.c:3812-3884. A preflight result that goes stale is harmless.
  Memory safety during the walk is the only hard requirement.
- F5. The profiled-lock macros assume every mutex is the state lock. g3.c:13-15,
  pg.c:5-7 and paging_windows.c:9-11 remap `ExAcquireFastMutex(m)` to
  `AdmissionG3LockAt(CONTAINING_RECORD(m, ADMISSION_G3_STATE, Lock))`
  (gpuva_g3_private.h:190-199). A second FAST_MUTEX in those files would
  corrupt memory through the bogus `CONTAINING_RECORD`. The new lock must be a
  different primitive with its own helpers.
- F6. Fast mutexes save `OldIrql` per mutex, so nested releases must be LIFO.

## 1. Hot sites: state read and written under `state->Lock`

| Site | Process-local (P) | Adapter-global (G) |
|---|---|---|
| `AdmissionGpuvaG4PreflightEscape` g3.c:6206-6224, then `AdmissionG4PreflightLocked` g3.c:6124 | Reads `FindProcess` (walks the `Processes` list, G). Reads `Poisoned`, `CpuOnlyMappings`, `Graph.{Created,Uncertain,ProcessId}`, the `Contexts` list plus context fields, the `PrivateScenes` list plus flags, `PrivateManager.{Generation,Blocks}`, `PrivateVa`, `TvbVa`. Parse walks: `Graph` tables/slots, `TableShadowBrokerBuckets→LogicalPtes` (g3.c:4627). Writes `Graph.TableHint` and `Graph.Proofs` (F1). | Reads only `adapter->SchedulerFaulted` (interlocked) and `LocalView` (lock-free). No G writes. |
| `AdmissionGpuvaG3CompleteJobTimestamp` g3.c:4240-4285 | `PublishTimestampLocked` (g3.c:4161) walks `Graph` (ContainsRangeAccess and TranslateVa write `TableHint`) and `ResidentPtes` (`CopyPte`). `GraphEndJob` writes `JobNested/JobInFlight/LeaseToken/Slot/LastStatus/Uncertain`. Writes `PrivateScenes[].GpuDone` and `Poisoned`. | Reads and writes `ActiveProcess/ActiveContext/ActiveFence/SuccessorContext/SuccessorFence/LastCompletedFence/PrivateCompletionFence`. `Client` gets JOB_END and RELEASE (graph.c:964-989). Sets `JobEvent` after the unlock. |
| `AdmissionG4SubmitVirtualEnvelope` g3.c:5887-5960/6037; queue g3.c:6082-6099 | Reads `Graph.{RootIpa,Uncertain,MappingGeneration}`. Scene lookup (`FindPrivateScene`/`FindPrivateResubmission`). Full parse at g3.c:5965 (walk plus caches). `ResolveOutput`. `SnapshotFailure` (shadows). Writes scene `HeapBlocks/ResumeFence/Submitting/Queued/Fence` (g3.c:6008-6036). | Reads `PrivateCompletionFence`. `SchedulerLock` is nested inside the lock at g3.c:6083 (RenderPacket queue). The compute branch is `SubmitComputeLocked` (g3.c:5497-5562). |
| `AdmissionGpuvaG3BeginJobAs` g3.c:3805-4062 | Re-parses (walk plus caches) through `FindPrivateScene`. `LogicalEnvelopeAccess`, `ContainsRange`, `OutputAccessibleFor`. `GraphBeginJob`/`BeginNestedJob` write `LeaseToken/Slot/JobInFlight`. Writes `scene->Started` and `context->GpuvaG3MappingGeneration`. | Writes `state->G4ReparseV5` (a 4 KiB+ shared scratch, g3.c:3822-3830). Reads `BackendImage`. `Client` gets LEASE and JOB_BEGIN. Reads and writes `Active*/Successor*`, reads `PrivateCompletionFence`, writes `G4DrawSnapshot`. |
| `AdmissionGpuvaG3BuildPagingBuffer` pg.c:1880-1975 (FLUSH_TLB), 2052-2235 (UPDATE_PAGE_TABLE) | Mutates the graph (`UpdateLeaf/Parent`, `RegisterTable`, `Flush`), shadows (`ResidentPtes/LogicalPtes/PendingPtes`) and `NativeReady` through the invalidate callback. | Receipts: `UnpublishedGroups`, `Leaf64KPtes`, `SystemGroups`, `SystemShapes`, `LeafHistory`, `AllocTrack`. `Registry` frames (`MappingAcquire/Release`). `Client` broker calls. Waits on `JobEvent` with the lock dropped (pg.c:1877-1900, 2048-2086). |

The measured case is a foreign completion waiting on a preflight. The two
sites share **no** P state, and the preflight touches no G state except
`FindProcess`. The contention comes only from the lock layout.

## 2. Code that must take the per-process lock (P lock)

P-owned state: `Graph` (all fields), `TableShadows`/`TableShadowBrokerBuckets`
and the shadows' `Logical/Resident/PendingPtes`, `PrivateScenes` and their
fields, `Contexts`, `PrivateManager`/`FirmwareManager`, `Tvb*`,
`PrivateTables`, `PresentArena`, `Poisoned`/`PoisonSite`.

**Graph mutations** (each also needs G, because of F2):

- Creation: `AdmissionDdiCreateProcess` g3.c:2947, 2960, 2975, 2977.
  `GraphInit/Create` runs before the process is on the list. Initialize the P
  lock before `InsertTailList` (g3.c:2964/2979/2988).
- `AdmissionDdiDestroyProcess`: g3.c:3102 (`GraphDestroy`), 3110-3135 (shadow
  free, `MappingRelease`).
- `AdmissionDdiSetRootPageTable` g3.c:3384-3423: `BindRoot`, AttachPrivate at
  3409/3411, and the context root.
- `AdmissionGpuvaG3BeginJobAs` g3.c:3912/3913. `AdmissionGpuvaG3CompleteJobTimestamp`
  g3.c:4260. `AdmissionGpuvaG3AbortJob` g3.c:4310.
- Private and TVB storage: `AdmissionG3PrivateMapExtentObserved` g3.c:702,
  `AdmissionG3PrivateTables` 739-746, `AdmissionG3TvbUnmapPages` 2377,
  `AdmissionG3TvbEnsureLeaf` 2397/2405, `AdmissionG3TvbMapBlock` 2432,
  `AdmissionG3TvbGrow` 2454, `AdmissionG3TvbRelease` 2511. All are reached
  from `AdmissionGpuvaG3PrivateEscape` g3.c:2640-2830 and
  `AdmissionGpuvaG3PrivateRetireContext` g3.c:1083-1126.
- Paging: `AdmissionG3RetireSystemSubtree` pg.c:108-127,
  `AdmissionG3ResetTableShadow` pg.c:145-162, `AdmissionG3RegisterTable`
  pg.c:189, `AdmissionG3PrepareTableReuse` pg.c:232-263,
  `AdmissionG3UpdateParent` pg.c:339/367, `AdmissionG3UpdateLeaf`
  pg.c:796-1050, and `AdmissionGpuvaG3BuildPagingBuffer` pg.c:1971 (Flush)
  and 2197. The broker shadow tables are written in
  `AdmissionGpuvaG3BrokerTable` g3.c:589-630 and
  `AdmissionGpuvaG3MirrorTable` g3.c:646.

**Scene, context and manager mutations:** `AdmissionG3PrivateReleaseScene`
g3.c:755-765, `CacheScene` 788, `TrimCache` 800, `CachedScene` 815-817,
`ReuseScene` 834-841, `EvictCached` 850, `EvictPressure` 862-906 (it mutates
a **foreign** owner), `PrivateReap` 976-1000, `PrivateReset` 1005-1023
(**all** processes), `PrivateReported` 1037-1065, `PrivateRetireContext`
1083-1126, `PrivateDestroyStorage` 1147-1161, `AdmissionG3GrowPrivateHeap`
2309, `AdmissionG4PrivateUnqueue` 3592-3612, `PrivateContextBusy` 3622-3626
(through Reap), `AttachContext` 3280-3290 and `DetachContext` 3298-3307
(`Contexts` list), `EnsurePresentArena` 3251-3268, Submit scene commit
g3.c:6008-6036 and 6094-6096, `SubmitComputeLocked` g3.c:5497-5562,
`SubmitPresentComputeInner` g3.c:5668-5745, `ExecutePresentVirtual`
g3.c:4547-4605, `StoreManagerSnapshot` g3.c:4085-4092 (FirmwareManager).

**Walkers that write caches** (F1), so they need P even though they look
read-only: `CopyEscape` g3.c:1921-2226, `NativeQueryEscape` 1629-1742,
`FrameArmEscape` 1404-1442, `CaptureFaultMapping` g3.c:87-140,
`EncodeVirtualPaging` pg.c:1301-1310 and 1343-1366 (`ResolveLogicalVa`),
`BuildPagingBufferChecked` pg.c:1627-1631, `LiveUpdateReason`/`LiveTable`
pg.c:1650-1759, `PendingManager` g3.c:4113-4126, and the parse in Preflight,
Submit and BeginJob.

**State that can stay G-only** (no parse reads it): `NativeReady`. Its writers
are `AdmissionG3NativeInvalidate` g3.c:1246, which runs inside graph mutators
that hold P+G, and `InvalidateAllocation` g3.c:1263-1272. Its reader is
`NativeQueryEscape` 1696/1736. Also G-only: `FaultDrill` (3712-3777),
`DeviceRefs/ContextRefs`, `NativeMapRefs/NativeClosing`, the `Processes` list,
and all receipts. `paging_windows.c:379` only reads the
`JobInFlight/LeaseToken` scalars, so G is enough there.

## 3. Process lifetime and the broker client

- Destroy: `AdmissionDdiDestroyProcess` g3.c:3058-3152. Under G it refuses
  with `STATUS_DEVICE_BUSY` while any of these hold: `DeviceRefs`,
  `ContextRefs`, `NativeMapRefs`, a queued or quarantined scene, `Uncertain`,
  or a `GraphDestroy` failure. It then unlinks the process, sets `Magic=0`,
  and frees it after releasing G (g3.c:3141-3150). The precedent for a ref
  taken under G and used without G is `NativeMapRefs` (g3.c:3027-3053).
- How a parse keeps the process alive: take a per-process `EX_RUNDOWN_REF Rundown`
  with `ExAcquireRundownProtection` under G, right after `FindProcess`.
  `DestroyProcess` does: under G check the refuse conditions → release G →
  `ExWaitForRundownProtectionRelease` (bounded by the parse, at most ~8 ms
  measured) → take P then G and re-check everything → free. If it then fails
  with BUSY, call `ExReInitializeRundownProtection`.
  Hypothesis, not yet checked against the docs: dxgkrnl does not destroy a
  device during an escape on it, so `DeviceRefs` already pins the process.
  The rundown makes this explicit and costs nothing.
- Completion and Begin get the process from `state->ActiveProcess` or
  `context->GpuvaG3Process`. A process with a graph job in flight cannot be
  destroyed (`GraphDestroy` refuses while `LeaseToken/JobInFlight` are set,
  graph.c:991-995). They still take the rundown ref so the rule is uniform.
- Broker client: **not concurrency-safe** (F2). In the target design every
  broker call stays under G. A later step can move it under a dedicated
  innermost `BrokerLock`, together with the Registry, which F2 also makes
  global.

## 4. Lock protocol (recommended)

**Order: P lock → G lock → `runtime->PagingLock`/pool locks → spinlocks
(`SchedulerLock`, adapter `PagingLock`).** This is the reverse of the
candidate "global → process". Under "global → process", any G holder that
needs a P lock currently held by a parse (paging for GravityMark, GravityMark's
own Begin or Complete) would block **while holding G**. That brings the
DWM stall back transitively. Under P → G, nobody waits for a P lock while
holding G.

- P lock type: `KGUARDED_MUTEX` (or `EX_PUSH_LOCK` used only exclusively,
  inside `KeEnterCriticalRegion`), not FAST_MUTEX (F5). It needs its own
  `AdmissionG3ProcessLock/Unlock(p, site)` helpers and its own profile sites.
- Invariant: **P-owned state is written only with P and G both held.** A
  plain-scalar read may hold either lock. Any graph or shadow walk must hold P
  (F1). G-only readers of P scalars stay valid: `EvictPressure` filters,
  `SnapshotProcesses` g3.c:2565, `paging_windows.c:379`, `FaultDrill`.
- Getting a process (helper `AdmissionG3RefProcess`): take G → `FindProcess`
  or the pointer from the context → `ExAcquireRundownProtection` → release G.
  Then take P, and take G again only if needed. While holding P, re-check
  `Magic`/`Poisoned`/`Uncertain`.
- Sites that look at several processes and hold G (`EvictPressure` victim,
  `PrivateReset`): a blocking P acquire is forbidden there. Use
  `KeTryToAcquireGuardedMutex(&owner->Lock)` and skip a busy victim in
  `EvictPressure`. In `PrivateReset` (TDR, rare), use the
  ref → release G → P → G loop for each process.
- Every wait loop on `JobEvent` must drop **both** P and G before
  `KeWaitForSingleObject`, because completion needs P. The loops are at
  pg.c:1877-1900, pg.c:2048-2086, g3.c:1942 (CopyEscape) and g3.c:2653
  (PrivateEscape). paging_windows.c:401 holds only G.
- Completion: take G and snapshot `p=ActiveProcess`, `ActiveFence==fence`, the
  retry check, and ref p → release G → take P(p) → take G → re-check
  `ActiveProcess==p && ActiveFence==fence` (if not, handle it like the
  current retried/false path) → `PublishTimestamp`, `EndJob` (broker under G),
  scenes, `Active*` → release G → release P → `KeSetEvent`.

### Minimal first step (fixes the measured case)

1. Add `KGUARDED_MUTEX Lock` and `EX_RUNDOWN_REF Rundown` to
   `ADMISSION_G3_PROCESS` (gpuva_g3_private.h:253). Initialize both in
   `CreateProcess` before `InsertTailList`. Make DestroyProcess wait on the
   rundown as described in §3.
2. Preflight (g3.c:6206): G → `FindProcess` + rundown → release G → P → do
   the whole existing body (context lookup, `AdmissionG4PreflightLocked`)
   unchanged → release P → release rundown. The validation does not change.
3. Add P around every P-state writer and walker listed in §2. Most sites
   already take G, so they become `ref; P; G; … ; G release; P release`.
   Writers keep G, so every existing G-only reader stays correct.
   Submit, BeginJob and paging still hold G across their parse in this step,
   so they behave as today, plus an uncontended P acquire.

After step 1, the DWM completion needs P(DWM) and G. GravityMark's preflight
holds only P(GM). The 92 µs mean / 8 ms max wait from preflight goes away.
Not fixed by step 1: GravityMark's own Submit (149 µs), Begin and paging
(340 µs) still hold G across their parse or mutation.

- Step 2: Submit and BeginJob do their parse under P only, then take G for the
  commit: `PrivateCompletionFence`, `Active*`, JOB_BEGIN, scene flags. Move
  `G4ReparseV5` into the process (or a P-owned pool buffer) because it is
  G-shared scratch today. Holding P across parse and JOB_BEGIN keeps
  "validated graph == leased graph", which g3.c:3934-3936 requires.
- Step 3: an innermost `BrokerLock` for `Client` and `Registry`, so that paging
  holds P plus brief G/Broker sections instead of G for its whole 340 µs.
  This needs graph.c call-site locking or a lock hook in
  `APPLE_AGX_GPUVA_V5_IO`.

Rejected alternatives:

- Shrinking completion's critical section does not help. Completion waits to
  *acquire* G and holds it for only a short time.
- `EX_PUSH_LOCK` shared for parses fails twice. Parses write caches (F1).
  Completion and Begin are exclusive writers and would still wait for the
  shared parse to finish, so the measured stall stays.
- Dropping G in the middle of a parse is incompatible with an in-progress
  graph walk.

### Offline tests to update or add

- `tests/g3_vidmm_replay_shim.h:117-120` stubs `ExAcquireFastMutex` as
  "assert PASSIVE, set irql=1", which cannot nest. Replace it with a
  lock-rank checker: P has rank 1, G rank 2. Assert that every acquire's rank
  is greater than the highest rank held, that releases are LIFO, and that
  `KeWaitForSingleObject`/`KeDelayExecutionThread` run with no lock held (the
  shim already asserts PASSIVE at :134). Add stubs for
  `KeAcquireGuardedMutex`/`KeTryToAcquireGuardedMutex`/rundown. 43 tests use
  this shim or `g3_vidmm_replay.py`.
- Struct shims: 22 test files define their own `ADMISSION_G3_PROCESS` and
  `ADMISSION_G3_STATE` (for example `g4_submit_virtual_replay.c`,
  `test_g4_fault_drill.py`, `test_g3_poisoned_completion.py`,
  `test_d3d12_query_timestamp_publication.py`,
  `test_g3_private_global_pressure.py`,
  `test_g3_private_scene_cache_lru.py`). Each needs the new fields. Better:
  slice the real struct from gpuva_g3_private.h, as `g3_vidmm_replay.py:81-83`
  already does.
- Directly affected function slicers: `test_g4_submit_virtual_replay.py:42` and
  `test_g4_render_local_view_replay.py:35` (Preflight);
  `test_g3_job_event_wakes_escapes.py:29` (uses `rindex` on the G release, so
  it still holds if `KeSetEvent` stays after both releases);
  `test_d3d12_query_timestamp_publication.py`, `test_g3_copy_job_gate.py`,
  `test_g4_tdr_drill_set_b_rc98.py`, `test_g4_survivable_fault_recovery.py`,
  `test_g4_tdr_drill_poisoned_completion.py`,
  `test_render_job_depth2_kick_rc102.py`, `test_r153_manager_callbacks.py`
  (Complete/Abort).
- New RED→GREEN test. **WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?**
  "A foreign job completion never contends with a parse."
  In the submit replay, wrap the parse access callback so that its first call
  invokes `AdmissionGpuvaG3CompleteJobTimestamp` for a second process (DWM)
  with a non-blocking lock shim (an acquire of a held lock fails the
  assertion). Today this is RED because G is held. After step 1 it is GREEN.
  A same-process completion in the same hook must report contention. The
  rank checker catches G→P inversions and waits with a lock held.

## 5. Risks

- R1. A missed writer or walker is a silent data race or use-after-free. Gate
  with a debug assertion `AdmissionG3AssertProcessOwned(p)` (owner thread
  recorded on P) in `AdmissionG3CopyPte`, `LogicalEnvelopeAccess`,
  `GraphAccessTyped`, `PrivateGraphAccess`, and the graph `InvalidateNative`
  callback (`AdmissionG3NativeInvalidate` g3.c:1246, which runs on every graph
  mutation).
- R2. F5: using FAST_MUTEX for P in the profiled files corrupts memory. Use a
  guarded mutex and separate helpers.
- R3. A JobEvent wait that keeps P held deadlocks against completion, which now
  needs P. Four loops are listed in §4.
- R4. `EvictPressure` with a try-lock skips a busy victim. The private
  allocation then fails where it could have succeeded today. This changes
  behaviour only under pressure combined with a concurrent parse. Record it as
  a receipt (`PrivatePressure`).
- R5. Same-process stall remains. GravityMark's completion waits for
  GravityMark's own preflight (up to 8 ms), and while `ActiveProcess` is set
  no other process can Begin. The GPU still idles in that window. Fixing it
  needs either completion without P (hypothesis: `EndJob` touches only the
  lease fields, and the timestamp IPA would be resolved at BeginJob, where
  the leased graph pins it), or a shorter preflight. Measure first: in the
  EXP2157 profile, check whether the waits on the completion site
  (`OtherSite`=6206) are foreign or same-process.
- R6. `context->GpuvaG3Poisoned` is already written without the G3 lock at
  backend_platform_windows.c:4389 and scheduler_windows.c:606. The race
  exists today. It is acceptable only because the flag is advisory and F4
  applies.
- R7. DestroyProcess waiting on the rundown must not hold P or G, and the
  dxgkrnl calls in DestroyProcess are PASSIVE. A rundown that never drains is
  impossible, because each ref is bounded by one parse.
- R8. Hardware checkpoint: run the same GravityMark+DWM workload as EXP2157
  and read `Wom1G3LockProfile`. Expect the completion site
  ((1<<16)|4240-equivalent) to show no wait events with `OtherSite` = the
  preflight site, and preflight holds to show up on the new P sites. Failure
  criteria: unchanged completion WaitMax, or any deadlock (the guest-hang
  watchdog fires) → recover through the standard `recover.sh`.
