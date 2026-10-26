# RC32: GPU count predication for ExecuteIndirect

Offline implementation on `codex/rc32`, base `b31f3cf5` (RC31).
No hardware, guest, builder, package, firmware, capability or recovery changes.
The integration owner maintains EXPERIMENTS.md and CHANGES.csv under the task
handoff rules. Runtime correctness and throughput still require hardware.

## Evidence and the actual bottleneck claim

Saved input root:
`/Volumes/pwdev/public_windows/.local/experiments/EXP1963-counted-indirect/evidence/EXP1963`.
Reproduction, frame CSVs, input hashes and detailed receipt analysis are in
`/Volumes/pwdev/public_windows/.local/handoff/codex/rc32/`:
`receipts-analyze.py`, `receipts-timing.txt`, `frames-{a,b}.csv`,
`receipts-evidence.md`, `receipts-input-sha256.txt`.

QPC is 24 MHz; Execute/owner timing fields are microseconds. Use complete
present intervals after the first counted scene frame (39/35), ending >70 s
after the first UMD timestamp. There are 783/596 such intervals. Medians are
182.982042/184.687979 ms. The upper-median 10k frame differs by 0.000229 ms.

| Nested component, ms | 2k frame 683 | 10k frame 296 |
|---|---:|---:|
| Present interval | 182.982 | 184.688 |
| Execute replay | 176.885 | 179.416 |
| All draw_vbo | 118.388 | 118.172 |
| Indirect subset of draw_vbo | 87.334 | 99.332 |
| Other draw_vbo | 31.054 | 18.840 |
| Replay remainder | 58.438 | 61.136 |
| Final submit | 2.333 | 1.481 |
| Final flush, within submit | 2.301 | 1.398 |
| Final retire, within submit | 0.019 | 0.059 |

Evidence: `gm1963a-umd.log:179862` through line 180110 and
`gm1963b-umd.log:84090` through line 84351. Both frames have 13 outer draws,
seven counted draws, 149 dispatches, 19 copies and 27 fence increments.
The eight slow draws are across **all 13**, not the six other draws. Aggregate
checked CPU download is 1.125 MiB; this is not seven four-byte count reads.

The interpretation is **confirmed as a synchronization dependency**, not as
an exact 87/99 ms wait attribution. Baseline owner
`drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp:3599` calls ReadBuffer
for the count; `:827` maps PIPE_MAP_READ, then copies and unmaps it. Both this
operation and subsequent native draw encoding sit inside TimeDrawVboIndirect.
The removed helper can be inspected with `git show b31f3cf5:<path>`.

The relevant primary/source chain is:

- `.local/reference/mesa/src/gallium/drivers/asahi/agx_pipe.c:764` synchronizes
  writers before a direct CPU read; `:978` synchronizes a staging blit writer.
- `build-native-asahi-state.py:428` copies a GPU-only buffer into staging;
  `:444` selects staging for UNMAPPABLE buffers. Hence the important Windows
  reason is `Sync writer due to: GPU read staging blit`.
- Primary `agx_batch.c:449` flushes an active writer and optionally synchronizes
  a submitted writer. `native-asahi-batch-lifecycle.py:179` projects sync to
  BatchPoll and cleanup. `agx_win32_gpuva_batch.c:1268` retires/waits its fence.
- `agx_win32_asahi_bo.c:968` can also retire a holding fence before CPU mapping.

Existing receipts lack paired count-read/flush/wait/map timings and count BO
identity. A writer may already be complete. Seven count calls do not prove
seven nontrivial waits or exactly seven new submissions. The containing
87/99 ms also includes useful GPU work, CPU encoding, queries and residency.
Final-submit timings cannot account for internal waits during replay.

Perf reasons are aggregated counts and stop after about 4096 emitted rows
(owner `:305`). 2k stops at present 513, before frame 683; 10k stops at present
307. The 3318/1906 reported GPU staging-sync counts are incomplete process
totals. Absence of a later reason is not evidence that synchronization stopped.

## Windows and native contracts

WDK 26100 `.local/reference/wdk26100/d3d12umddi.h:2455` defines command
signature stride and `:2469` ExecuteIndirect's MaxCommandCount plus argument
and count placements. Gallium `pipe/p_state.h:780` defines packed draw records.
UMD validation now uses subtraction bounds and checks word alignment before
narrowing offsets to Gallium's 32-bit representation. It rejects oversized
count buffers/placements and arithmetic wrap instead of addressing another
word after truncation. This does not add a DDI, capability or layout exception.

Primary Honeykrisp `src/asahi/vulkan/hk_cmd_draw.c:3824` allocates packed scratch,
dispatches libagx_predicate_indirect with AGX_BARRIER_ALL | AGX_PREGFX, then
issues MaxCount indirect draws. `src/asahi/libagx/draws.cl:16` copies enabled
records and zeros every word of disabled records. Both indexed and non-indexed
specializations already exist in the generated shader bundle.

`build-asahi-runtime-closure.py:612` links libagx_shaders.c; the native state
projection supplies its matching generated header. This implementation uses
that existing kernel. No new NIR/compiler path, copied external implementation
or hand-encoded GPU program is introduced. External MIT notices remain in the
reference/prepared tree and temporary kernel test; the new header is original
Windows/Gallium integration glue.

## Implementation and ownership

The owner forwards the count buffer unchanged (`agx_d3d12_windows.cpp:3888`);
its bounds are at `:3830` and successful-path receipt at `:3928`.
The native GPUVA wrapper projection (`build-native-asahi-state.py:6796`)
intercepts counted non-patch draws before heap binding and graphics batch
entry. `agx_win32_counted_indirect.h` implements the lowering:

1. Validate source/count extents and output-size arithmetic before any GPU work.
2. Temporarily disable application heap callbacks while obtaining the compute
   batch and registering both source BOs through agx_batch_reads.
3. Allocate packed output through the existing batch slab pool. Take an
   explicit BO reference before dispatch, since CDM capacity can submit during
   launch. No per-record resource/BO is created.
4. Dispatch the existing predicate kernel. Gallium's agx_launch_internal emits
   a full CDM barrier after each launch (`agx_state.c:2998`). Its
   agx_launch_precomp ignores the barrier argument, so AGX_PREGFX alone would
   be insufficient: flush the compute batch asynchronously before drawing.
5. Restore callbacks, recursively issue native indirect draws of MaxCount
   records, and release the local BO reference. Consuming batches retain the
   BO, including across graphics batch rollover. The native fake meta-resource
   intentionally has no screen; batch.c:519 still retains its BO.

Input hazards use native agx_batch_reads (`agx_batch.c:519`) to submit previous
writers. GPUVA compute submission uses agx_cmd_header(true,0,0)
(`agx_win32_gpuva_batch.c:1153`); subsequent render headers wait on prior
compute (`:971`). These queue dependencies supply the cross-batch ordering.
No explicit CPU count map or completion wait is introduced. Ordinary batch
slot pressure/residency may still wait, as elsewhere in the driver.

Pool CPU shadows are published before the predicate kernel. Later render
batches reference the producer BO but do not own/re-publish its pool shadow
(`agx_win32_gpuva_batch.c:824`). This prevents stale shadow bytes from replacing
the GPU output. The explicit local pin covers producer completion and recursive
rollover; batch references cover GPU use after the helper returns.

Failures in batch acquisition, source hazards, initialization, scratch,
precompiled shader launch or submission prevent drawing and propagate the
existing context/backend fault. Callback restoration and the explicit reference
are unwound. Patch topology retains the native CPU fallback and its existing
map-failure handling, with a distinct receipt.

Review also found a WOW64 pool capacity overflow: `(offset + sz)` can wrap for
near-4GiB scratch and falsely fit into a 256KiB slab. The projection now tests
`offset >= capacity || sz >= capacity - offset` before addition. The helper's
output limit separately protects the 16KiB backing-size rounding on 32-bit
Windows. A real projected-decision regression reproduces the former failure.

Asahi/UMD own encoding, hazards and transient lifetime; KMD owns residency and
submission/completion. m1n1/Mu initialization, interrupts, DMA mappings, power
and recovery are unchanged. Live machine inspection is prohibited by this
explicitly offline task; the supplied EXP1963 launch is the comparison contract.

## MaxCount cost and expected gain

`agx_d3d12_ddi.cpp:5934` defines the ExecuteIndirect receipt. Every counted
call is indexed: 5480 calls with MaxCount=2400 in 2k, 5578 with MaxCount=12000
in 10k. Representative seven-call frames therefore encode **16800/84000**
records. Packed scratch is 48000/240000 bytes per call, 336000/1680000 per frame.
Actual count values are not logged, so the increase over RC31 cannot be measured.

Primary `agx_state.c:4393` recursively runs full native draw encoding for every
record. Zeroed records still cost CPU encoding and resource validation. Active
IA queries launch a libagx IA kernel per record (`:4933`, `:3874`), even when
that record ultimately draws zero vertices. Honeykrisp's different encoding
path is not evidence that Gallium's fixed-MaxCount cost is acceptable.

The expected mechanism is removal of seven CPU count-consumption boundaries.
There is **no defensible positive net-FPS prediction yet**. Even an impossible
saving of the entire 87.334/99.332 ms only budgets 5.20/1.18 us per MaxCount
record; actual available savings are smaller. Extra encoding/query work can
outweigh eliminated waits, especially at 10k. Reject this throughput candidate
if Score/FPS or medians regress. Do not hide this risk with a trace-specific
MaxCount cutoff or reinstate CPU readback for otherwise native-compatible draws.

## Regression evidence and hardware receipts

Tests execute the actual owner callsite and production predicate helper under
ASan/UBSan. The pinned libagx kernel runs in a temporary host harness, retaining
its MIT notice. Cases cover indexed/non-indexed, zero/one/clamped/UINT_MAX
counts, padded stride, nonzero offsets, 12000 records, zero MaxCount, invalid
alignment/extents, allocation/init/dispatch/submit faults, CDM auto-submit,
callback restoration and producer/consumer BO ownership. Projection tests check
placement before graphics batch entry, fallback and generated kernel presence.
Heap-hazard tests keep their existing resource/sampler rollover assertions.

New receipts:

- perf reason `GPU counted indirect`: kernel emitted and producer submission
  succeeded before native replay; `CPU counted indirect: patches`: fallback.
- `measure-d3d12-counted-path {path, MaxCount, total_call_us}` after a successful
  owner draw, independently of the perf-reason cap. Path 1 is native GPUVA
  predication, path 2 is CPU fallback (patches or legacy physical build).
  Time includes predicate preparation and native MaxCount encoding; it is not
  GPU execution time. Existing execute-indirect kind distinguishes indexing.

Require seven path-1 rows per representative GravityMark frame, matching the
original MaxCounts, no path-2 GravityMark draws, and no device/context faults.
Source plus executable regression proves the owner count ReadBuffer is absent;
perf-reason absence alone cannot prove that. Other CPU maps/waits remain legal.

Offline verification for the final code:

```
python3 -m unittest test_d3d12_count_scratch_pool test_d3d12_gpu_count_predicate test_d3d12_counted_indirect test_d3d12_heap_draw_hazards test_d3d12_indirect_heap_rollover test_d3d12_command_signature test_d3d12_native_owner test_d3d12_abi_gate
Ran 51 tests ... OK
cd tests && python3 run_against_baseline.py
ran 547 tests in 235 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

Logs and the independent review are in the RC32 handoff directory. Known
failures were not changed. No Windows toolchain/native package build occurred;
these host tests do not substitute for that compile gate.

## Smallest hardware checkpoint — integration owner only

Next run is this branch HEAD with the **EXP1963 launch**: expanded RAM, m1n1
075e3980 / macho 452ab29f, full 2688 MiB pool, Wom1GpuFaultInfoRead=1 and the
same GPU_WAIT/capture profile. Native ARM64/x64/x86 compile/link gates and exact
package hashes/preregistration/recovery remain integration responsibilities.

WHY THIS HYPOTHESIS:
1. Seven counted calls occupy 87/99 ms inside ordinary 183/185 ms frames.
2. The source necessarily consumes the GPU count synchronously on the CPU.
3. The linked native kernel can preserve argument semantics without that read;
   deterministic tests cover the translation and reference lifetime.

First run C14 counted correctness: zero/one/multiple/clamped counts, indexed and
non-indexed records, pixel and query/statistics checks, then patch fallback.
Then repeat GravityMark 2k and 10k with process exit between arms. Reference
Score/FPS: **784/4.7** and **798/4.8**; median **182.982/184.688 ms** under the
window above. Collect Score/FPS, median/tail, counted-path time, native phase
counts, replay, fences, downloads and trims. Check output, queries, DWM and
device health. Collect evidence before the established exact-package recovery.
No hardware checkpoint was run by RC32.

## Next independent sink and unknowns

Remeasure MaxCount encoding first. The next currently separate envelope is
58.438/61.136 ms of replay outside draw_vbo: 149 dispatches plus copies, queries
and translation. `owner:3490` creates samplers; `:3502` binds compute resources;
`:3523` launches; `:3542` unbinds/deletes. A follow-up should time these phases
and retain sampler/view state with exact invalidation only if binding dominates.
Keep batch-dependent heap rebinding and hazards. Dividing by 149 gives an upper
envelope of 392/410 us, not a measured per-dispatch cost. No implementation here.

Unknown: Windows native compilation, GPU correctness/barrier visibility,
zero-record GPU cost, net throughput, actual enabled counts, and how much of
87/99 ms is removable synchronization. No claims of hardware validation,
complete elimination of CPU waits, or expected score increase are made.
