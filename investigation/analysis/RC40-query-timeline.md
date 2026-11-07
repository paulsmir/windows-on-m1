# RC40 — GPU-timeline D3D12 query resolution

Offline candidate, 2026-10-26. No hardware verdict. This note supersedes the
C11 note's description of timestamp resource resolution as a CPU copy.

## What the EXP1968 evidence actually says

The task reports a 769-frame median of 180.6 ms wall, 174.7 ms replay,
59.4 ms query commands, 114.7 ms draws and 9.3 ms estimated GPU busy time.
The only query-heap creation receipts in
`.local/experiments/EXP1968-timing-recovery/evidence/EXP1968/gm1968a-umd.log`
are lines **9411, 15872 and 18058**, each `{Type=2, Count=0x400}`.
`d3d12umddi.h:2274` defines type 2 as PIPELINE_STATISTICS. Thus nine query
commands are consistent with three Begin/End/Resolve triples. The log does
**not** prove that occlusion or timestamp commands consume that 59.4 ms.

The provided CSV contains 809 rows; selecting its 774 rows with nine query
commands gives median query 59.7165 ms, draw 114.685 ms, replay 174.833 ms,
wall 180.6821 ms, submit 2.5985 ms and GPU busy estimate 9.271 ms. This is a
separate explicit filter, not a reconstruction of the task's 769-frame filter.
Neither series splits Begin, End and Resolve, or query allocation from waiting.
Do not describe all query time as a proven GPU wait or claim a measured saving.

## Primary contracts and cause

Paths below are repository-relative; external pinned sources live under
`.local/reference/mesa` or `.local/reference/wdk26100`.

- `drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp:4353` records the heap type;
  `:4387` records query commands; `:6682` replays them in order.
- `drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp:1901` lazily allocates
  each native query. Pipeline statistics use eleven separate native queries.
  `:1919` calls Begin/End; `:1932` resolves each with the Gallium GPU-resource
  API. The old owner already used this API; changing the owner call alone
  would not remove the hidden waits.
- Pinned `src/gallium/drivers/asahi/agx_query.c:290`: Begin synchronizes
  all outstanding writer generations before CPU zeroing. This forces CPU
  retirement when a query slot is reused. Fresh query allocation can also
  cost time; EXP1968 does not expose the query indices or per-operation time.
- The same source `:311–336`: occlusion/statistics End only detaches the
  active query. `:337–347`: timestamp End attaches pending batches and seeds
  a firmware-clock estimate. Neither End path explicitly waits.
- The same source `:475–500`: ordinary counters/predicates already resolve
  with `libagx_copy_query_gl` on a compute batch after flushing the writers.
  Flushing establishes submission order; it is distinct from waiting.
- The same source `:475–478, :444–445, :393`: timestamps decline that GPU
  path, enter `get_query_result(..., true)`, and synchronize writers.
  `:575–580` CPU-reduces batch timestamps into the query words at cleanup.
- `drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c:106` obtains firmware
  timestamps by escape after retirement. Merely GPU-copying those CPU query
  words would race cleanup and return incomplete results.
- Before RC40, `AgxWin32AsahiBatchPoll` discarded its timeout and called the
  retiring/waiting path even for zero. The projected `agx_cleanup_batches`
  calls Poll(0); this turned intended opportunistic cleanup into a wait.
  The native batch-slot limit and submission-depth pressure remain bounded
  waits; RC40 does not claim all memory allocation or submission is nonblocking.

WDK `d3d12umddi.h:2308–2321` and SDK `um/d3d12.h:4527–4540` define the
88-byte statistics result: IA vertices/primitives, VS invocations,
GS invocations/primitives, clipping invocations/primitives, PS, HS, DS, CS
invocations. Pinned `pipe/p_defines.h:582–592` uses this same index order.
Each result is UINT64 at an eight-byte-aligned destination offset.

D3D12 has no result-availability poll in this path: Resolve is ordered GPU
work. Gallium's `index=-1` availability and other GL result types retain
native CPU behavior. Never-created D3D12 slots retain the existing defensive
GPU zero fill. Active or never-ended query results are undefined by D3D12;
zero fill is not a newly advertised guarantee for invalid application use.

## Implementation and ownership

1. Poll(0) checks the monitored fence before entering retirement.
2. Query reuse versions busy storage. Old storage, including shared
   occlusion slots, is retired only when its writer **and resolve-reader**
   generations have completed. Begin/End contains no query flush or wait.
   Timestamp queries version on End because they have no Begin.
3. Each native batch owns an immutable GPU-only timestamp BO before
   submission. Every timestamp End snapshots active/submitted nonempty
   batch BOs with references and an immutable clock seed. Empty batches add
   no uninitialized output. Reusing a batch slot cannot change the snapshot.
4. G4 `APPLE_AGX_G4_TIMESTAMP_PUBLISH=2` in StartHandle, StartOffset=0,
   EndHandle/EndOffset=GPUVA low/high, requests one aligned eight-byte end
   timestamp. Only fragment output for render, or standalone compute,
   admits this mode. Compute preludes and vertex publication are rejected.
   Existing zero/request modes remain accepted. Old KMDs reject the new
   mode: **install a matching KMD and all UMD architectures, not a hot swap**.
5. KMD obtains the same existing firmware timestamp as the old escape,
   converts it to nanoseconds with the existing frequency contract, and
   caches it for completion retry. Under the G3 lock it rechecks the active
   process/context/fence, valid writable graph mapping, logical PTE/native
   translation agreement, allocation and local/pool extent. It publishes
   with memory barriers before GraphEndJob releases residency and before
   successful fence notification. Missing/invalid data fails completion.
6. The original `libagx_win32_query_timestamp` kernel computes the unsigned
   maximum of the immutable seed and referenced batch outputs, then stores
   one UINT64. Resolve flushes producers, holds every source BO in the
   consuming batch and uses `agx_batch_writes_range` for destination hazards.
   Address-upload/compute-acquisition failure cannot enter CPU fallback.
7. The owner rejects mismatched heap/query types before indexing or emitting
   work (the old binary-on-timestamp case could read past the heap), and
   propagates a fault raised during the void Gallium result callback.

UMD owns query objects/snapshots/encoders; Asahi owns GPU encoding and batch
hazards; KMD owns firmware completion observation and validates the new store;
VidMm retains residency ownership. m1n1/Mu initialization, DMA mapping,
interrupts, power and recovery contracts are unchanged. No new firmware
pointer, MMIO register or microsequence is introduced. The reference Linux
implementation (`queue/common.rs:14–42`, `queue/compute.rs:164–200`) instead
binds a firmware-visible timestamp object and brackets work with timestamp
operations; Windows continues using its existing KMD-owned timestamp storage.
The last validated C11 frequency is 1 GHz (EXP1375), and this candidate keeps
nanoseconds and the existing clock-calibration DDI unchanged.

Original new code is MIT; projected upstream notices remain intact. No
external implementation bodies were copied. Generated kernel hashes and
local compiler inputs are recorded in `query-timestamp-generated/manifest.json`.

## Remaining synchronization, ranked by available measurements

1. **Counted indirect draws:** EXP1963's representative 182.982 ms frame
   spent 87.334 ms in the seven counted calls (RC35 report); EXP1968 has
   seven path-2 calls and 114.7 ms in all thirteen draws, without the same
   separate measured subset. `agx_win32_counted_indirect.h:42–56` maps the
   four-byte GPU-written count with PIPE_MAP_READ. Pinned
   `agx_pipe.c:980` synchronizes the staging-copy writer. Native argument
   legalization/IA-counter work may add further draw cost. The GPU stream
   opt-in, guards, fence/link fixes and defaults are unchanged.
2. **Query bucket:** 59.4 ms supplied task median. The old explicit reuse
   synchronization, zero-timeout cleanup wait and timestamp CPU fallback
   are removed from the D3D12 path, but this workload's Begin/allocation/
   Resolve shares are unknown. Timestamp changes are correctness coverage
   for the requested query types, not evidence of this frame's bottleneck.
3. **Final submit:** 2.6 ms supplied median includes flush and queue-ordering
   work. Owner `AgxD3d12OwnerSubmit` uses `AgxD3d12QueueWaitForOwner`; without
   successful GPU ordering it calls `AgxWin32AsahiContextRetire`. Existing
   GPU_WAIT selection is unchanged.
4. **Other unseparated waits:** native submission depth (`retire_held`),
   batch-slot exhaustion, residency/paging, CPU readback/staging and native
   `agx_query_increment_cpu`. Copies are only ~0.02 ms in the measured replay
   bucket, dispatch ~1.1 ms; no separate time can be assigned to these waits.

## Expected result and unknowns

Removing the whole 59.4 ms bucket would be an optimistic upper bound,
180.6 -> 121.2 ms (~8.25 median frames/s). It does not by itself meet 18 FPS,
and it is not a prediction: allocations, scheduling, retirement work shifted
later and the new output-BO overhead can change the net result. Confirm with
`replay_query_ms`, frame time, Score/FPS, GPU completion coverage and pixels.

U64 transport, maximum reduction and destination arithmetic preserve all 64
bits. Existing shader statistics increments can wrap their low 32 bits
(`src/asahi/libagx/query.cl:119–136`); this candidate does not turn those
existing hardware counters into 64-bit counters. D3D11 GetData, GL elapsed
queries and non-U64 GL timestamp conversion retain their compatibility paths.
Batch-granularity timestamps and the existing overlapping-occlusion limitation
remain. Native Windows compilation/linking and actual CPU-to-GPU store
visibility/freshness require integration validation.

## Smallest hardware checkpoint (integration owner only)

WHY THIS HYPOTHESIS: three statistics heaps and nine query commands account
for ~59 ms; source contains forced reuse retirement and a nominal poll which
actually waits; U64 timestamp resolve additionally lacks a completion-ordered
source. The candidate removes those specific synchronization paths.

Build matching KMD/ARM64/x64/WOW64 UMDs and run native compile/link/stack gates.
Use the current proven m1n1/Mu/expanded-RAM/full-pool launch and its exact known
good recovery. Run C11 occlusion, binary, statistics and timestamp probes;
add ordered Resolve->GPU reader, multiple slots/destination offsets, and
same-slot Begin/End/Resolve reuse before completion. Timestamp re-End must
preserve a previously queued resolve; verify nonzero monotonic nanoseconds and
GetTimestampFrequency=1e9. Then repeat the EXP1968 2k workload with the same
capture/GPU_WAIT/indirect-stream settings. Require correct results, no device
loss and a measured query-time reduction before attributing a gain. Collect
evidence before integration's usual exact package cleanup. No hardware run was
performed in RC40.
