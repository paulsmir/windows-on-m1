# RC35 — completion timing and compute replay costs

Offline implementation on codex/rc35, base c2cda961. No hardware, guest,
network, builder, package or other-worktree operation. Integration owns
EXPERIMENTS.md/CHANGES.csv and native compilation/hardware validation.

## Evidence and the changed performance premise

RC31/32's saved EXP1963 2k frame 683 is 182.982 ms: 176.885 ms replay,
118.388 ms draw_vbo, 58.438 ms replay remainder, 2.333 ms final submit;
149 dispatches, 19 copies, 27 fences. Source log:
`.local/experiments/EXP1963-counted-indirect/evidence/EXP1963/gm1963a-umd.log:179862`.
Those figures are nested CPU wall-time envelopes, not GPU durations.

EXP1965 arrived during RC35. The saved 2k arm completed with Score 300,
1.8 FPS (`gm1965a-out.txt`, final benchmark lines). The same RC31 method
(first counted scene, complete intervals ending >70 s from first receipt)
yields 299 intervals, median **481.895792 ms**. Representative frame 272,
`gm1965a-umd.log:79290–79545`: replay 476.370 ms, draw_vbo 262.105 ms,
indirect subset 197.665 ms, bind/unbind 0.060/0.313 ms, remainder **213.892 ms**,
submit 2.670 ms, 149 dispatches, 19 copies, 23 fences, no trims in that frame.
All 2092 `measure-d3d12-counted-path` rows are **path=1, reason=3**; first
at line 12183, representative seven at 79531–79537. Thus the GPU-generated
stream did not run. `agx_win32_indirect_stream_emit.h:20–29` assigns reason 3
to active queries/predication/statistics. It does not identify which specific
query in this receipt. The perf log names input-assembly counters, but its
budget makes totals incomplete. Do not remove that correctness guard or claim
RC33's intended saving from this run. No 10k workload log was present at review.

Handoff scripts reproduce both the previous and current analyses. RC35's
analyzer reports no completion records for these old logs, hence GPU timing
unknown; it never substitutes the CPU draw or wait time.

## Source contracts and ownership

Primary sources inspected locally:

- Asahi Linux `drivers/gpu/drm/asahi/queue/compute.rs:164–200,347–351`:
  optional timestamp microsequence operations bracket the idle wait; work
  has separate driver and user timestamp pointers. `queue/render.rs:525–556,
  923–927` does the equivalent for fragment work. Firmware writes its buffers;
  process memory is not invented as a new firmware mapping.
- Existing KMD `backend_platform_windows.c:426–469` captures compute sidecar
  job start/end (offsets 0/8, EXP1342 precedent), or fragment objects 28/29.
  TA object identifiers are already defined at lines 41–42. Existing completion
  at `:3335–3337` captures while PendingJob/storage still belongs to the worker.
- `gpuva_g3_windows.c:4736–4740` starts timing on SubmitCommandVirtual;
  `backend_platform_windows.c:3314` marks the first completion observation.
  These QPC endpoints include scheduling/queueing and completion detection.
- WDK26100 `d3dkmddi.h:5797–5807`: private escape buffer/size and context;
  `d3d12umddi.h:2284`: timestamp query type. No new public DDI/capability.
- `agx_d3d12_windows.cpp:1892–1976` implements query heaps/query/resolve;
  `native-asahi-batch-lifecycle.py:105–116,146–148,252–264,439–443` connects
  timestamp query completion to the existing KMD values. The unchanged v1
  escape in `umd_gpuva_windows.c` exposes those values only when queried and
  limits its old diagnostic receipt to 64 rows. Application timestamp queries
  already exist; they are not cheap per-batch telemetry and may introduce
  their own synchronization during query reuse/resolve.

KMD owns completion observation and firmware-buffer lifetime. UMD owns replay,
logging and immutable sampler objects; Asahi owns encoding, hazard tracking,
batch resources and sampler handles. VidMm still owns residency. No m1n1/Mu,
ACPI, DMA, interrupt, power or recovery contract changes. The saved EXP1963
launch contract remains the reference; live inspection was prohibited and is
not needed for these offline changes. No external implementation body copied.

## Completion stream

`render_job_timing.h` adds an independent 256-record ring and version-2 APTS
bulk query; version-1 bytes/layout are unchanged. Each record contains sequence,
command GPU VA, fence/PID/kind/status, submit/first-completion QPC and raw
compute-or-fragment plus TA timestamps. A record is about 80 bytes; query page
32 records is 2624 bytes, below the 16 KiB stack gate. All types are plain
32-bit unsigned int and 64-bit unsigned long long, with no user CRT includes.

`render_job_timing.c:147–197` implements allocation-free append, immediate
completion-retry deduplication, process-filtered pagination, explicit lost
sequence count and cursor-reset signaling. `backend_platform_windows.c:474`
captures in the completion path; aborted jobs have status 1 and no successful
firmware intervals. Missing overwritten submit slots increment MissingTiming
instead of silently disappearing. Reads/capture share JobTimingLock. The bulk
escape selects the PID from PsGetCurrentProcessId, zeros unused output records,
and returns no foreign process's command VA/timing record.

`umd_gpuva_windows.c:2089` drains at most four pages at each owner submit and
again after existing teardown retirement. It neither polls nor waits for GPU
completion. Collection/text export is gated by AdmissionUmdDiagnosticEnabled;
the ring is always cheap to fill. Text uses the existing buffered trace writer,
which may itself perform synchronous file I/O. This is no-added-GPU-wait
collection, not a promise of nonblocking OS I/O. Uncollected in-flight tail
jobs, overwritten records, missing timing slots and failed collection remain
limitations. Multiple owner cursors can repeat records; analysis deduplicates.

All receipt payload words are hexadecimal UINTs; adjacent pairs below are
low then high 32 bits. Frequencies/clocks retain all 64 bits.

| Receipt | Payload in word order |
|---|---|
| `measure-gpu-completion` | sequence(2), commandVA(2), fence, kind(1 render/2 compute), status(0 success/1 dropped), submitQPC(2), completeQPC(2), frequencyHz(2) |
| `measure-gpu-stages` | sequence(2), compute/fragment start(2), end(2), TA start(2), end(2) |
| `measure-gpu-coverage` | returned cursor(2), newest sequence(2), lost adapter sequences(2), reset, cumulative missing-timing attempts(2), original reader cursor(2) |
| `measure-gpu-collection-error` | HRESULT in the receipt header, no payload; no cursor advance |

Stage rows join on PID and sequence, not arrival time. Lost ranges are unioned
across independent cursors: [original cursor, original cursor + lost). The
analyzer rejects resets and conflicting sequence duplicates; use a single
adapter lifetime/capture. It does not guess epochs from late receipt ordering.

## Replay decomposition

New timers only sample when trace capture is enabled. Existing receipt layouts
stay unchanged. `agx_d3d12_ddi.cpp:6596` classifies mutually exclusive command
buckets; the loop records start/end QPC and aggregates once per Execute.
`agx_d3d12_windows.cpp:3526` partitions dispatch phases, including failure
cleanup and repeated heap/launch work across indirect records.

| Receipt | Payload |
|---|---|
| `measure-d3d12-replay-time` | six microsecond sums: draw, dispatch, copy/texture-copy/resolve, query, indirect expansion, other; then six corresponding command counts |
| `measure-d3d12-dispatch-time` | owner dispatch calls, bind microseconds, heap/reference microseconds, launch microseconds, unbind microseconds; reset per owner submit |
| `measure-d3d12-replay-window` | Execute startQPC(2), replay endQPC(2), submit endQPC(2), frequencyHz(2) |

Bind includes sampler lookup/creation, views/CBV/UAV binding and compute state;
heap includes batch sampler union, heap table upload, BO references and optional
validation; launch includes native encoding, allocations, any implicit
flush/residency/CPU wait; unbind includes cleanup/error checks. These are CPU
wall times. Direct DDI dispatch bucket minus owner phases approximates
translation/overhead; the analyzer suppresses this subtraction when indirect
expansion also contains owner dispatches. Query and copy buckets include their
real synchronization. Never sum nested buckets as disjoint frame costs.

## Ranked source hypotheses and the one optimization

1. **Native launch/state upload plus internal synchronization.** Primary
   `agx_state.c:5297–5349` uploads grid parameters, descriptors/uniforms,
   builds a pipeline and registers shader BOs for each launch. At
   `:2998–3049` the CDM barrier is required, and capacity can split a batch.
   `agx_batch.c:531–554` flushes writer/readers on real resource hazards.
   Current EXP1965 query fallback additionally keeps MaxCount CPU work alive.
   Neither 23 nor 27 fences proves that any split is redundant.
2. **Heap rebinding/reference work.** Owner `:3205–3260` allocates two temporary
   arrays, deduplicates sampler descriptors and obtains batch handles;
   `:3266–3311` uploads CB14, binds CB15 and traverses read/write references.
   Native batch rollover means identical CPU descriptors cannot justify reusing
   handles/references. The loop at `:3596–3605` still rebinds every indirect
   record. Optional reference validation is additional work when enabled.
3. **Ordinary stage bindings/view creation.** Owner `:3369–3474` rebuilds
   CBV/SRV/UAV state, including user-buffer upload and sampler-view allocation,
   and clears resource bindings afterward. DDI OwnerBindings at `:6068` and
   dispatch replay also translate descriptors. Their costs are unmeasured;
   reference/lifetime rules rule out a blanket unchanged-state shortcut.
4. **Immutable sampler object allocation/packing.** Primary `agx_state.c:504–566`
   copies the descriptor, packs it and frees it; it retains no batch/BO
   references. This is the clear safe optimization chosen here, even though it
   is unlikely by itself to explain the whole replay remainder.

`compute_sampler` at owner `:2954` retains one immutable object per slot,
using full descriptor bytes as its key. On a miss, create succeeds before old
state is replaced; failed creation returns failure without using stale state.
Invalid slots bind NULL while their cache can remain for later reuse. Every
successful dispatch still binds/unbinds, and all batch-dependent work remains.
`release_compute_samplers` at `:389` releases state before context destruction.
The owner's existing lock serializes access; at most 16 cached objects live.

Expected gain is removal of repeated allocation/free/descriptor packing for
unchanged direct/static compute samplers. The executable test proves 149 calls
with two unchanged samplers use 2 allocations instead of 298, retaining 298
bind/unbind calls. If GravityMark uses only heap samplers on the measured path,
this cache may save nothing there. No millisecond/FPS gain is claimed. The
change is independently revertible and the new timers establish the next target.

## Analyzer and validation gate

Run `python3 tools/agx_submission_analyze.py LOG --csv frames.csv`.
For EXP1963/1965 old logs, add `--qpc-hz 24000000`. Handoff `analyze.py` is a
thin entry point to this tracked tool. JSON reports coverage and invalid stages;
CSV reports frame/replay components and interval intersections.

`outstanding_ms` is the union of submit→completion-observation intervals clipped
to each present interval. `cpu_outstanding_overlap_ms` intersects that with
CPU replay windows. These are QPC-based outstanding-work metrics, not GPU busy.

`gpu_busy_estimate_ms` unions raw firmware stage intervals after aligning each
job's final firmware end to its first completion QPC. `gpu_cpu_overlap_estimate_ms`
intersects that estimate with CPU replay. This mapping includes observation
latency/hypervisor offset error. Zero/inverted ranges, stages beyond final end,
or mapped starts before submission are excluded and counted. Old timestamps
can still fit a long queued envelope: freshness needs hardware validation.
The analyzer deliberately sets timing_complete=false and gives no total-GPU-busy
claim. Missing data is NULL, not zero. Tail in-flight jobs require later drain.

Tests run production ring/capture functions and owner dispatch/cache code with
ASan/UBSan, check phase partition/capture-off operation, failure and rollover,
real sampler descriptor bytes, v1 query projection, and analyzer late-arrival,
union/overlap, unknown ranges, duplicates/loss/reset handling. Native Windows
ABI/link and firmware validity are not established by these host tests.

Smallest next checkpoint belongs to integration: native compile ARM64/x64/x86
and KMD with stack gates, existing query and compute/indirect regression probes,
then same saved EXP1965 2k profile with trace capture and no other change.
WHY THIS HYPOTHESIS: 149 dispatches recur in the representative frame, native
launch and heap work execute on each, while existing receipts cannot split
213.892 ms remainder or observe GPU completion. Require usable coverage, sane
raw stages/QPC latency and unchanged pixels/device health; compare frame median,
phase sums, Score/FPS and trace overhead. Collect through application teardown
before the established exact-package recovery. Query-active GPU-stream support
is a separate correctness task, not a guard to disable here.
