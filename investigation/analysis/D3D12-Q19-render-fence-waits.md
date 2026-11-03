# Q19: RenderFence waits are mostly submission pressure, not CPU query resolve

Offline base b4c65f6af2f2f1ba2f675ee1a62297c9ab2bdd3d. Implementation is
opt-in and unvalidated on hardware. Task handoff DESIGN.md contains the plan
and integration experiment; REPORT.md records commits and final verification.
No hardware, guest, builder, package, firmware or other worktree was touched.

## Finding and measured attribution

The largest identifiable removable mechanism is the **four-submission UMD
tracking window**. It is distinct from the depth-one KMD execution window.
`retire_held` CPU-waits for the oldest tracked submission before a fifth can
be accepted. Draw hazards, query writer flushes and final owner flush all
reach it. Increasing tracking capacity permits earlier CPU enqueue while
keeping the existing GPU ordering and exact fence ownership.

Query resolve is already a GPU operation on this branch. The saved workload
creates **three pipeline-statistics heaps**, not timestamp heaps. Its three
Begin/End/Resolve commands per frame resolve eleven counters each. The
hypothesis “ResolveQueryData synchronously reads timestamps on the CPU” does
not describe this source or these captures.

Inputs are the shared `.local/experiments/EXP2105-gm-minimal/evidence/EXP2105/`
`gm2105a-umd.log` (HD) and `gm2105b-umd.log` (FHD). SHA-256:

- HD: `14454cc4bcf883009e86bddee3ea3a300f16e4909eff78ddada843d3bac7268a`
- FHD: `1da83ac519a310b66249ed559a669fcf229af10bb7a13f12fed10b1b1e165408`

Reproduce with `python3 tools/agx_render_wait_analyze.py <log>`; it runs Q18's
steady-state selection, then joins exclusive waits, inclusive paths and query
commands by PID/thread/Execute serial. First 30% of Presents excluded; 3943 HD
and 3234 FHD complete frames, 3944/3235 complete Execute windows, QPC 24 MHz.
JSON results are `gm2105{a,b}-render.json` in the Q19 handoff, with sample source
line numbers and exact receipt words. Q18's analysis documents the GPU-span
estimator and gap limitations; those conclusions are preserved.

| Retained exclusive RenderFence | HD ms/frame | HD calls (total / frame) | FHD ms/frame | FHD calls (total / frame) |
|---|---:|---:|---:|---:|
| Draw, 0x403 | 9.68246 | 7815 / 1.98199 | 10.65690 | 9161 / 2.83271 |
| Resolve, 0x303 | 5.39932 | 8375 / 2.12402 | 10.32757 | 9305 / 2.87724 |
| Final submit, 0x603 | 2.04961 | 3588 / 0.90997 | 2.69137 | 3103 / 0.95949 |
| Sum of these phases | **17.13138** | 19778 / 5.01598 | **23.67584** | 21569 / 6.66945 |

Counts are calls into the profiled CPU wait callback, not total polling calls
or number of GPU jobs. Already-completed fast paths do not enter that scope.
The profiler retains only four exclusive and four inclusive buckets per
Execute: omitted keys and counts are unknown, not zero.

| Retained inclusive InflightSlot | HD ms/frame | HD calls | FHD ms/frame | FHD calls |
|---|---:|---:|---:|---:|
| Draw, 0x40a | 8.90764 | 7129 | 5.37846 | 4682 |
| Resolve, 0x30a | 0.89713 | 907 | 7.47836 | 4863 |
| Submit, 0x60a | 0.06291 | 34 | 0.02447 | 11 |

These inclusive values overlap the RenderFence rows and Flush scopes. They
must not be added to them. Inclusive QueryWriters is 7.9678/13.3963 ms/frame,
with 129096/106458 retained calls; those are mostly the eleven per-statistic
**flush** requests, not proof of CPU query reads. Resolve command counts are
11829/9702; begin and end have the same counts (three per frame). The full
resolve commands cost 8.29790/13.76302 ms/frame. All three observed heap
creations in each whole log have type 2 (pipeline statistics).

A conservative source-independent intersection bound sharpens attribution.
For each Execute and phase, let R be RenderFence exclusive time, I the
InflightSlot inclusive time and P an upper bound on all exclusive time in
that phase. Then RenderFence inside InflightSlot is at least max(0,R+I-P)
and at most min(R,I). P includes all omitted exclusive time. Missing R or I
gets the top-four/omitted upper bound; truncation of each emitted integer
microsecond bucket is conservatively allowed. No per-wait timestamp or call
stack is reconstructed.

| RenderFence time inside submission-slot pressure, lower..upper ms/frame | HD | FHD |
|---|---:|---:|
| Draw | 5.65138..9.68808 | 3.65750..10.66616 |
| Resolve | 0.49375..5.42823 | 4.74390..10.33859 |
| Submit | 0.03147..2.18776 | 0.01169..2.75801 |
| Sum across disjoint phases | **6.17660..17.30407** | **8.41309..23.76276** |

The upper endpoint can exceed retained RenderFence slightly because omitted
RenderFence time and quantization are included. At least 6.18/8.41 ms of CPU
blocking is attributable to the chosen capacity mechanism. The traces cannot
partition the remainder exactly among native CPU maps, staging publication,
completion downloads and other nested waits. Giving exact per-caller counts
for those would invent information. The table gives observed callback and
path counts instead.

Perf-reason records are batched **delta counts in word 1**, not word 3's
Present count. Reported-in-window deltas include:

| Reason | HD | FHD |
|---|---:|---:|
| Flush writer: Read from another batch | 3968 | 3200 |
| Flush reader: Write from other batch | 3968 | 3200 |
| Flush writer: Indirect stream arguments | 7936 | 6400 |
| Windows runtime flush | 3968 | 3200 |
| Flushing due to `<invalid>` | 130944 | 105600 |
| GPU stream scratch reuse | 55552 | 44800 |

These encompass 3968/3200 reported Present deltas, not exactly the 3943/3234
selected frames, because reporting is batched. `<invalid>` is the generic
query-name string for the single-statistic query type, not a failed query.
Scratch reuse selects an idle spare; its receipt does not mean CPU waiting.
No retained SyncWriter, SyncReaders, CountMap, TransferMap, QueryIncrement or
BatchSlot *inclusive top-four* bucket occurs in the selected windows. That
limits evidence for those alternatives without assigning them zero cost.
All observed queue-wait receipts in the window are ordered=1 (3943/3234);
the outgoing bridge succeeds, so those calls do not enter the CPU fallback.

## Exact source paths and contracts

Paths in this section start at `drivers/apple-agx/`, unless marked reference.
Line numbers are for the Q19 implementation.

**Common leaf.** `render-admission/umd/src/umd_gpuva_windows.c:852-898`
`wait_object` first trusts a valid completed RenderFence cache/acquire proof,
otherwise calls `pfnWaitForSynchronizationObjectFromCpuCb` with no event and
profiles AgxWaitRenderFence. The pinned WDK `d3dumddi.h:3838-3850` explicitly
says the null-event call does not return until the value is satisfied.
`wait_render:2784` additionally handles pending CPU completion/download work.
This is different from the GPU queue wait at the end of Execute.

**(a) Draws.** `d3d12/agx_d3d12_ddi.cpp:6978,7044` sets draw phase and replays
through the owner. Native Asahi reference `agx_batch.c:519-588` registers BO
reads/writes and flushes conflicting active writers/readers with `sync=false`.
Thus a texture/indirect-argument hazard submits the producer without requiring
its CPU completion. The projection
`mesa/scripts/native-asahi-batch-lifecycle.py:468-585` finishes compute/render
batches through the Windows backend. Both
`mesa/winsys/agx_win32_gpuva_batch.c:978,1206` call `retire_held:533-544`, which
blocks through BatchPoll:1339 when the tracked window is full. BatchPoll uses
`agx_win32_gpuva.c:337-372` FIFO retirement, which calls WaitRender before
releasing each set. This is the measured InflightSlot chain.

Additional possible draw waits are real but not individually timed here:

- Native `agx_batch.c:305-362` has 128 batch slots. If none is free or completed,
  it synchronizes an LRU slot (“Too many batches”). Q19 changes no batch-slot
  limit. Ordinary encoder/sampler/batch fullness causes a **flush**, reaching
  the submission-capacity path, rather than inherently demanding CPU completion.
- Reference `agx_pipe.c:708-792` synchronizes writers before CPU maps and readers
  before CPU writes unless unsynchronized/discard/renaming rules apply.
  `agx_win32_asahi_bo.c:971-995` retires the newest holder before a first native
  CPU map. `agx_query.c:585-595` CPU query increments synchronize query writers;
  indirect count/DrawAuto fallbacks may map GPU-produced input. GPU stream IA
  accumulation is already implemented in `agx_win32_query_ia.h:86-130`.
- `umd_gpuva_windows.c:1455-1470,1677-1700` waits LastUseFence before a changed
  CPU staging upload overwrites canonical bytes still held by an older job.
  Clean staging skips this; CPU-visible does not by itself imply a wait.
- `agx_d3d12_windows.cpp:3050` whole-level ring and `agx_d3d12_level_ring.h:34`
  handle wrap by retiring before reuse; the persistent upload ring instead
  reserves fence-tracked slices (`umd_gpuva_windows.c:1510-1626`) and falls
  back when full. No busy slice may be overwritten. These are distinct rings.
- Resource barriers/feedback legalization can trigger flushes or decompression.
  They do not universally call CPU sync. `agx_d3d12_ddi.cpp:6232` tracks normal
  transition barriers; the actual resource uses enforce producer ordering.

**(b) ResolveQueryData.** `agx_d3d12_ddi.cpp:4544-4551` records a command; replay
at `:7051-7063` calls `agx_d3d12_windows.cpp:2461-2495`. Each counter uses
`get_query_result_resource(WAIT,U64,0,...)`. Reference `agx_query.c:464-501`
flushes query writers, gets a compute batch, registers destination writes and
emits a query copy kernel. The native lifecycle projection at `:1076-1125`
adds GPU timestamps: reduce the immutable per-batch timestamp BOs with the
captured seed, writing the maximum into the destination. Occlusion uses the
native counter/boolean conversion; pipeline statistics copy each full U64.
`PIPE_QUERY_WAIT` means the result must be valid when consumed, not a mandate
for a synchronous CPU wait. Windows-supported GPU resolve failure faults the
context instead of silently entering CPU fallback (projection `:983-985`).

Query Begin/rebegin already versions busy storage
(`agx_win32_query_lifecycle.h:31-51`), while resolve readers and producers keep
the generation busy. Retired generations are destroyed only when no batch
names them (`:18-28`); RC67 groups eleven counters in one generation/BO.
Therefore no second “GPU resolve” implementation is needed. Resolves still
**flush producers**, and those submissions hit the same four-entry capacity
wait. CPU GetData for non-D3D12 consumers has different wait/poll semantics
(reference `agx_query.c:386`, projected `native-asahi-batch-lifecycle.py:277`).

**(c) Submit.** `agx_d3d12_windows.cpp:5087-5130` flushes remaining owner work,
then orders the runtime queue after the owner's exact accepted fence using
`agx_d3d12_queue_sync.h`. This may reach InflightSlot inside the flush. If the
queue bridge cannot be used, ContextRetire performs CPU retirement; explicit
SubmitAndWait and presentation also retain their completion requirements.
`umd_gpuva_windows.c:2414-2443` additionally waits synchronously if completion
requires downloading GPU-written CPU-visible private staging; native/direct
storage or cpu_quiet storage excludes that path (`download_due:1854-1858`).
The branch otherwise returns after scheduling a monitored-fence signal, with
holds intact. Q11 explains why an unsatisfied cross-context bridge cannot be
deleted: it protects application Signal/Wait/Present ordering.

## Windows contract and replacement choices

Pinned WDK `d3d12umddi.h:2354-2363` defines ResolveQueryData as a command-list
operation naming heap/type/range/destination/offset; SDK `d3d12.h:6555` exposes
the corresponding command. Microsoft's local DirectX-Graphics-Samples
`dxgs-68907e31-more/MiniEngine/Core/CommandContext.h:791-794` records GPU query
resolve. `GpuTimeManager.cpp:98-105,118-129` queues resolve and keeps its fence,
then waits for that fence at **CPU readback**, before mapping. That primary
sample separates GPU timeline resolution from CPU result consumption.

Microsoft reference URLs for integration review, not network-fetched here:
[ResolveQueryData](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12graphicscommandlist-resolvequerydata),
[Queries](https://learn.microsoft.com/en-us/windows/win32/direct3d12/queries),
[Readback buffers](https://learn.microsoft.com/en-us/windows/win32/direct3d12/readback-data-using-heaps),
[Resource barriers](https://learn.microsoft.com/en-us/windows/win32/direct3d12/using-resource-barriers-to-synchronize-resource-states-in-direct3d-12).

| Mechanism | Required contract / replacement |
|---|---|
| Four-entry submission pressure | Translation-layer capacity. Add bounded fence-tracked slots and keep FIFO retirement; implemented behind opt-in. Still wait at the new bound. |
| Query result availability | GPU producer-before-copy and copy-before-reader ordering required. Existing GPU copy/reduction + tracked source/destination + generations already supply it. |
| Query overwrite/free | Storage must survive prior GPU readers/writers. Existing fresh generations/deferred destruction avoid CPU wait. Keep this unchanged. |
| GPU BO read/write transition | Preserve ordering, flush active producers and use the existing ordered owner timeline; a CPU wait is not inherently required. |
| CPU map/readback, CPU counter/count consumption | Finished data required before CPU access. Replace algorithm with GPU count/compute/resolve when feasible; never just delete the wait. Unsupported native fallbacks remain. |
| CPU staging overwrite / ring wrap | No overwrite while GPU reads. Use independent ring slices/renamed BOs with fence retirement and GPU copy; existing rings partially implement this. Further growth costs memory. |
| Private-staging completion download | Coherent CPU-visible result required. Native directly mapped backing or fence-tracked deferred copy-out can avoid submit-time waits only if external fence/map/present publication still covers copy-out. Not changed. |
| Owner-to-runtime queue bridge | GPU ordering required for external signals. Keep pending bridge, or migrate complete execution ownership per Q11; increased tracking does not replace it. |

UMD owns replay, BO/query/set lifetime and CPU publication; VidMm owns
residency/placement; VidSch/KMD own GPU scheduling, DMA, interrupts and recovery.
Asahi's DRM dependency protocol is already translated into the existing
Windows owner context/fence timeline. m1n1/Mu/ACPI initialization, power and
DMA-map contracts do not change. This task relies on saved EXP2105/2106
hardware evidence, not a newly guessed machine state. No external code copied.

## Implementation and limits

`APPLE_AGX_D3D12_SUBMISSION_WINDOW=1` is exact, default off, sampled once per
D3D12 owner. The owner configures winsys and runtime after initialization and
before replay (`agx_d3d12_windows.cpp:480-492,889`). One header
`agx_win32_submission_window.h` bounds the supported values to four/default
and sixteen/opt-in. `agx_win32_gpuva.h` reserves fifteen older-set entries;
submit and `retire_held` use the selected limit. `make_resident` uses the same
limit for per-allocation SubmissionHolds. Zero-initialized D3D11/GL owners
stay at four. Invalid internal values fall back to four. All ABI variants
use unsigned fields and existing full-width fence types, without new atomics.

The seventeenth live submission must wait for the oldest or refuse; failure
cannot reuse its slot. Shared allocations retain one hold per accepted set.
CPU consumers, staging overwrite waits, generation reuse, outgoing bridges,
failed/uncertain ownership and recovery remain intact. The KMD window is not
changed: it may execute at depth one while the UMD tracks sixteen queued jobs.
More pending work retains more BOs/private scenes; memory pressure or other
synchronous preparation may erase the saving. All modules using the changed
internal structs must rebuild consistently; mixing old/new archives is invalid.

The CPU opportunity is meaningful, but **17 ms is not a frame-time gain**.
Q18 measured only 0.59855/0.37045 ms/frame own completion-to-next-submit gaps,
of which 0.36374/0.21750 ms falls inside replay/submit windows. Most own-gap
time has skipped adapter sequences and can contain foreign GPU work.
Removing every inside-window gap, optimistically, changes EXP2106's 33.5/27.7
FPS to about **33.91/27.87** (1.2%/0.6%). Removing all own gaps gives about
34.19/27.99 (2.0%/1.0%). These are fixed-GPU-work scenarios, not guarantees or
universal bounds on changed batching. Zero gain is plausible; a larger gain
needs measured evidence of a changed submission/launch schedule. Do not forecast
closing the Metal score gap from this CPU-capacity change.

## Verification / remaining unknowns

The regression compiles real winsys submit/retire, real batch backpressure,
real runtime MakeResident/release and the exact option parser. RED reproduces
the fifth-set refusal; GREEN retains sixteen live sets, protects full capacity,
wait failure, shared holds, full-width FIFO results, uncertain ownership and
unbind. Existing query tests exercise pending timestamp/occlusion/statistics
results and generation reuse. Analyzer tests cover nested-time bounds, missing
buckets, bad flags and PID/thread/serial joins. Final counts and build manifests
are in REPORT.md. No baseline exemptions are added.

Unknown: native MSVC /W4 /WX/link success, on-device output equivalence,
performance, peak retained memory/scene pressure, exact caller attribution of
censored waits, and global GPU idleness. Host modeled results and Windows-target
objects do not establish those outcomes. The integration owner runs the
DESIGN.md checkpoint: full D3D12 regression and existing smoke gates, then the
EXP2106 mask at depth one, HD followed by FHD, switch off/on, exact hashes and
dump-first rec2106h-equivalent recovery. No hardware run occurred here.
