# RC42 — Execute CPU wait attribution (offline)

## Implementation plan and source contract

The assigned TASK.md is the execution specification. Implement inline in
codex/rc42; no network, hardware, other worktrees, package or ledger changes.

Inspected: RC35/RC40 reports and RC40-query-timeline.md; local WDK26100
`d3dumddi.h:3838,4323,4371,4376` (CPU wait, escape, MakeResident callbacks);
pinned Mesa `agx_batch.c:307,413,449,498,512,997`, `agx_pipe.c:912,980`,
`agx_query.c:146,585`; owner query replay, UMD residency and GPUVA retirement.
EXP1968/1969 supplied medians show the query bucket unchanged at about 60 ms.
No live observation is needed for additive offline instrumentation. UMD owns
CPU waits and replay, Gallium owns hazards and batches, VidMm owns residency,
KMD owns completion. m1n1/Mu initialize and expose the existing platform;
interrupts, DMA, power and recovery are outside this change and unchanged.
Windows uses callback/monitored-fence synchronization in place of Linux syncobj.

1. Add a bounded allocation-free per-thread Execute collector, enabled by
   AdmissionUmdMeasureEnabled. Nested scopes charge exclusive wall time to
   stable site IDs with replay phase encoded; no double counting. Query
   Begin/End/Resolve retain separate inclusive timers. Four largest entries
   plus omitted time and overflow flags fit one existing 16-word receipt.
   Verify deterministic clocks, nesting, disabled path, concurrency, reset,
   truncation, saturation and C/C++ linkage before wiring call sites.
2. Time UMD callback waits/residency/allocate/map/escapes, GPUVA pressure and
   blocking polls, Gallium synchronization, staging and query paths. Preserve
   returns, timeouts, ordering and failures. Compile projected translation
   units with their real declarations. Retain steady-state perf reasons once
   per 64 presents after the startup budget.
3. Extend the analyzer with phase/site frame medians and explicit missing or
   top-N-censored coverage; verify multi-Execute frames, missing old receipts,
   thread-interleaved IDs, failures and partial frames.
4. Run targeted modules and the complete prescribed baseline to BASELINE_OK.
   Review the diff, protected invariants and tandem items before each small
   commit. Write the handoff REPORT with final evidence and exact results.

No removable synchronization is established by the supplied timing. Do not
change hazards, capacity or GPU ordering speculatively. The only behavior fix
planned is keeping diagnostic sampling available after startup.

Smallest checkpoint for integration: EXP1970, GravityMark 2k default counted
path with the EXP1969 settings, tracing enabled (including refusals-only).
Compare begin/end/resolve, phase/site waits, omitted time and frame time;
retain correct queries/pixels and existing recovery. This task never runs it.

## Saved EXP1969a evidence

The provided `frames1969a.csv` contains 782 frames; selecting its 748 rows
with `replay_query_count=9` gives medians of 186.2902 ms wall, 60.7985 ms
query, 118.086 ms draw, 9.4346 ms estimated GPU busy, and 2.6145 ms final
submit. This explicit filter is not a claim to reconstruct another filter.
The exact saved log has three `measure-d3d12-query-heap` rows at lines
9716, 16254 and 18155, each type 2 / count 1024. It contains 4099 perf-reason
rows: the old cap is checked before reporting a group and can overshoot 4096.
The analyzer preserves missing RC42 fields as unknown in this old capture.
It also reports 3259 overwritten adapter completion records, so estimated GPU
busy is not a complete or calibrated GPU accounting result.

## Ranked query hypotheses and discriminating receipts

All local driver paths below are relative to `drivers/apple-agx`; native
reference paths are relative to `.local/reference/mesa`.

1. **Fresh native query allocation / residency during Begin.**
   `mesa/winsys/agx_d3d12_windows.cpp:1947` lazily creates a missing query;
   `:1967` does it eleven times for a statistics slot. Native
   `src/gallium/drivers/asahi/agx_query.c:146,166` allocates a dedicated BO
   and CPU maps it. Three 1024-slot heaps permit a long run of fresh indices;
   the existing receipts do not prove that indices are actually fresh each
   frame. Expect inclusive `query_begin.query_create` (site 18), creation
   count up to 33 per frame, and exclusive `allocate` (5), `make_resident`
   (4), `paging_fence` (2), `map_gpu_va` (37), `lock` (6) or `pool_map_escape`
   (28). If Begin is slow but query_create is zero, consider busy-version
   allocation at `mesa/winsys/agx_win32_query_lifecycle.h:32,40` instead.
2. **Resolve producer flush hitting finite submission depth.**
   Native `agx_query.c:480` flushes query writers before getting a compute
   batch and writing the result at `:484–498`. The helper at
   `agx_query.c:183` flushes each matching writer generation. Native `agx_batch.c:413,449` implements
   reader/writer hazards. Windows `agx_win32_gpuva_batch.c:486,930,1152`
   retires the oldest submission once all older slots are occupied.
   Expect a slow Resolve inclusive timer, `query_resolve.query_writers` (24),
   `flush` (15) or `inflight_slot` (10) in the path row, with the underlying
   exclusive `render_fence` (3), scene escape (27), or submit callback (9).
3. **Native batch-slot exhaustion, residency or scene work rather than the
   finite in-flight queue.** Native `agx_batch.c:307–363` searches free and
   completed slots, then synchronizes an evicted slot with reason "Too many
   batches". The projection instruments that exact call at
   `mesa/scripts/native-asahi-batch-lifecycle.py:1256`. Distinguish path
   `batch_slot` (12) from `inflight_slot`; `batch_poll` (11) preserves its
   nonzero timeout. A high exclusive `make_resident`, `paging_fence`,
   `native_ready_escape` (29), `scene_escape` or `submit_callback` instead
   locates the cost below Gallium submission. Escape IDs distinguish scene,
   copy, readiness, pool map, timestamps and diagnostic completion collection.
4. **Query reuse / CPU statistics hazards.** RC40 versions busy storage at
   `agx_win32_query_lifecycle.h:32`; it must preserve earlier resolves.
   Native `agx_query.c:585` still synchronizes writers before a CPU counter
   increment. Expect `query_increment` (20) / `query_writers` in the path row
   and a render-fence leaf; phase tells whether the cost belongs to draws or
   one of the three query operations. End itself detaches ordinary statistics
   queries and has no explicit native wait; a costly End is useful contrary
   evidence rather than a reason to assume another mandatory fence.

The first two are source-supported candidates, not a measured ranking of
Begin versus Resolve. Query allocation cannot simply be eagerly moved into
CreateQueryHeap: 3 * 1024 * 11 BOs would shift startup and memory costs, and
pool reuse must preserve results of all live indices and queued GPU readers.
Resolve flushes encode real producer/destination hazards. No unnecessary wait
or safe pooling lifetime is proven by these observations, so this change does
not remove synchronization or alter query storage.

## Ranked counted-draw hypotheses

1. **GPU-written count readback.**
   `mesa/winsys/agx_win32_counted_indirect.h:41–45` maps four bytes with
   PIPE_MAP_READ before native drawing. Projected GPU-only transfer can stage
   the read; native `agx_pipe.c:714,765,980` synchronizes writers, and a staged
   read synchronizes the copy's writer. Expect seven `draw.count_map` (26)
   path calls, often `transfer_map` (17), `sync_writer` (13) and a render-fence
   leaf. `staging_blit` (16), `copy_escape` (8), `lock_transfer` (35) and
   paging sites distinguish the actual transport.
2. **Native argument / IA-statistics work after reading the count.**
   The same helper `:59` re-enters native draw_vbo with the bounded count.
   `agx_query.c:585–594` CPU counter updates synchronize prior writers.
   `draw.query_increment` plus `query_writers` and fence time outside the
   count_map path supports this cause; count_map alone cannot account for it.
3. **Batch transitions/backpressure and residency.** The same finite queue
   and native slot paths as query Resolve can block during a draw. Their
   receipt phase is `draw`, separating them from query or final-submit work.
   A high `draw.inflight_slot` with `draw.render_fence` is different evidence
   from a slow `draw.make_resident` or `draw.scene_escape`.

Final submission is a separate phase. Owner `agx_d3d12_windows.cpp:4066`
retains the existing CPU-retire fallback when GPU queue ordering is unavailable;
its `submit.context_retire` path is explicit. Owner-lock admission is timed in
phase `other` before replay begins, so it is not silently charged to a query.

## Receipt ABI and interpretation

All fields use the existing unsigned hexadecimal UINT transport. The wait
header is `mesa/winsys/agx_win32_wait.h`; IDs are stable within ABI version 1.
Each valid Execute with tracing enabled emits:

- Existing `measure-d3d12-execute`: the first four words are unchanged;
  word 4 is the process-wide Execute serial. The collector is thread-local.
- `measure-d3d12-wait`, 16 words:
  `{version=1, serial, omitted_exclusive_us, flags, (key,count,exclusive_us)*4}`.
  Entries rank by total exclusive ticks, with deterministic tie breaking;
  unused triples are zero. `key=(phase<<8)|site`.
- `measure-d3d12-query-time`, 8 words:
  `{1, serial, Begin_count, Begin_us, End_count, End_us, Resolve_count, Resolve_us}`.
  These three times are inclusive wall time per replay operation, including
  allocations and all nested waits.
- `measure-d3d12-wait-path`, 16 words with the same layout as wait, but the
  selected caller scopes and omitted total are **inclusive**. These identify
  causal parents which would disappear when their nested fence is charged
  exclusively. Path times overlap; never sum them with each other or waits.

Phases: 0 other/lock, 1 Begin, 2 End, 3 Resolve, 4 draw, 5 dispatch/indirect
expansion, 6 final submit. Site names/IDs are mirrored in
`tools/agx_submission_analyze.py:52`. Flag 1 means malformed/overflowed scope
stack or invalid clock; flag 2 means saturated counters/microseconds; flag 4
means top-N omitted at least one site, even when its rounded time is zero.
Counters and clocks at inactive sites are untouched. The only enabled path
configuration is AdmissionUmdMeasureEnabled, so refusals-only tracing works.
No allocation, trace output, environment lookup or shared lock occurs at a
measured site. A fixed 64-deep TLS stack and fixed phase/site tables bound work.

Times are **CPU elapsed time in scopes**, including active kernel/UMD work,
scheduling and the small timer cost; they do not by themselves prove that the
thread slept or identify a kernel internal wait. Ordinary uninstrumented CPU
work remains in replay remainder. Collection is per Execute; creation,
GetData/present and application waits outside Execute are not attributed.

The analyzer joins all companions globally by PID and Execute serial, even
when a concurrent Present or delayed append separates them. Duplicate nonzero
Execute serials are rejected. Orphan companions are reported and make that
process's exact wait coverage unknown. Missing old rows remain unknown.
Top-N-censored sites expose lower bounds, not invented zeroes. JSON contains
`wait_site_medians`, `wait_path_medians` and `query_median_ms`; CSV contains
per-frame `wait_<key>_ms`, counts and lower bounds plus the corresponding
`wait_path_*` and `query_begin/end/resolve_*` columns. Medians exclude the first
partial frame of each process; process-level Present grouping is not a
swapchain-specific or queue-specific attribution model.

## Diagnostic correction and verification contract

The only runtime behavior correction is diagnostic: after the initial 4096
perf-reason rows, `owner_perf_report` emits at most one changed-table aggregate
(up to 48 rows) per 64 presents. Counts keep accumulating between reports.
Trace-disabled owners do not install a perf formatter. It no longer becomes
permanently blind after startup. Existing render, residency, timeout, package,
GPU stream, capability and recovery decisions remain intact.

Regressions execute the real collector/runtime glue under deterministic clocks,
C and C++ linkage, concurrent Execute threads, early scope exit, failure status,
disabled collection, bounded overflow, saturation and top-N omission. Additional
replays cover private staging's lock bypass, failed count maps, inherited batch
poll semantics, steady-state reason sampling, delayed receipts, duplicate IDs
and missing Execute receipts. Full projected query, batch and pipe TUs compile
against their real header closure with small host OS declaration shims. Reduced
pre-existing semantic replays use one inert diagnostic fixture; their substantive
assertions are preserved. Native Windows ARM64/x64/WOW64 package compile/link
and measurement overhead are integration gates, not claimed offline results.

## EXP1970 checkpoint for the integration owner

WHY THIS HYPOTHESIS:
1. EXP1969a still spends ~60.8 ms in nine statistics-query commands after rc40,
   so the remaining cause is below or beside the removed explicit waits.
2. Source allocates eleven BO-backed native queries per fresh statistics slot
   and flushes writers on Resolve; both enter the runtime residency/submission
   paths now measured independently.
3. Seven counted draws synchronously map a GPU-generated count, giving a
   concrete second CPU/GPU synchronization path to distinguish from query cost.

Build this UMD change for ARM64/x64/WOW64 with the ordinary package compile,
link and stack gates; preserve the same rc40-compatible KMD, m1n1/Mu, expanded
RAM, pool and default counted path as EXP1969a. Run GravityMark D3D12 at 2k
with the same trace settings and record Score/FPS/pixels. Require one wait,
query-time and path row per Execute serial; sample steady-state perf reasons
beyond the startup cap. Analyze the same post-loading/nine-query frame subset.
Check that query-operation totals reconcile with replay_query and that the
largest leaf/path pairs locate the ~60 ms query and ~118 ms draw costs. If the
omitted budget is substantial, report it before attributing a dominant cause.
Any flag 1/2, duplicate serial, missing parent or query-result/pixel regression
invalidates an exact timing conclusion. Compare trace overhead to EXP1969a;
no score improvement is predicted from instrumentation alone.

Run the existing query and counted-draw correctness probes. Preserve the known
recovery artifact and use integration's current exact-package cleanup after
collecting receipts/dumps. No hardware, package, builder, guest or recovery
operation was performed by RC42; no EXP1970 result is implied.
