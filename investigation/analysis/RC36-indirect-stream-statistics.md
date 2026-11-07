# RC36: counted streams with pipeline statistics

Offline branch `codex/rc36`, base `c2cda9611f9868362e9eaff4028a64dd563151d6`.
The task authorizes source/test changes and small commits only. No hardware,
guest, network, builder, package or other-worktree operation was performed.
Integration owns EXPERIMENTS.md and CHANGES.csv updates.

## Observed boundary and cause

Saved evidence root: shared `.local/experiments/EXP1965-gpu-vdm-stream/evidence/`.
`EXP1965/gm1965a-umd.log:8100,14359,16530` creates three type-2 pipeline-statistics
heaps. WDK26100 `d3d12umddi.h:2272` defines type 2. All **2092** counted-path rows
in that 2k log have `path=1, MaxCount=0x960, reason=3`. The recovered 10k log,
`rec1965h/app-logs/gm1965b-umd.log:11403,11462`, has two successful path-1 calls,
MaxCount `0x2ee0`, reason 3, before it stops. Its query heap is at line 7629.
The handoff `workload-evidence.json` records the exact extraction.

At the base commit, `agx_win32_indirect_stream_emit.h:23-25` rejects every active
pipeline-statistics object. RC32 then predicates scratch on the GPU but leaves
MaxCount as the number of native indirect records to encode. Pinned Mesa
`src/gallium/drivers/asahi/agx_state.c:4388-4402` CPU-unrolls those records;
`:4931-4954` additionally launches IA-statistics work per record. GPU-zeroed
records still pay that encoding cost. This is the proven performance cause.

## Windows / Asahi counter contract

All Mesa paths below are relative to `.local/reference/mesa/`. The WDK is
`.local/reference/wdk26100/d3d12umddi.h:2455-2474` (signature stride and argument/
count placements). The owner maps a D3D12 statistics query to eleven Gallium
single-statistic queries, and resolves each as U64
(`drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp:1890-1938`).

| Counter | Pinned Asahi behavior | Stream behavior |
|---|---|---|
| IA vertices, VS invocations | `poly/geometry.h:587-593`: vertices × instances | Same per executed record, atomically added |
| IA primitives, clipper invocations/primitives | `poly/geometry.h:595-599`, `compiler/shader_enums.h:1408-1449`: decomposed topology primitives × instances | Same formula and primitive enum, atomically added |
| PS invocations | `asahi/lib/agx_nir_prolog_epilog.c:725-741`: non-helper fragment sample-mask population added with a shader atomic | Existing FS prolog, shader and uniform path retained |
| GS/HS/DS | Stage-specific lowering, e.g. `agx_state.c:4134-4157,4565,4596,4655` | Stages absent from admitted streams, so no contribution; their real draws use fallback |
| CS invocations | `agx_state.c:5370-5391`: application launch-grid accounting | Internal precompiled stream dispatch bypasses this accounting |
| Occlusion | `agx_query.c:23-29,132-142,547-552`: occlusion heap/hardware query index | Native fallback retained |

The IA helper is `asahi/libagx/geometry.cl:16-24`, not `query.cl`.
`query.cl:90-117` performs query copies and `:139-149` implements other statistic
increments. IA/VS/clipper values are software counts; PS is shader instrumentation,
not a newly introduced hardware counter. Asahi uses **32-bit updates to the low
word of a 64-bit query slot** (`poly/geometry.h:574-599`). RC36 preserves that
existing wrap behavior; it does not claim to solve native counters overflowing
2^32. Begin zeroes the 64-bit slot (`agx_query.c:290-300`).

Primitive restart requires scanning indices and separate segment decomposition
(`asahi/libagx/geometry.cl:27-84`). RC36 deliberately retains native fallback
for statistics plus restart. Occlusion, elapsed-time, render predicates,
primitives-generated, XFB/overflow, GS/tessellation, draw-ID and passthrough-GS
cases retain native behavior. Future statistic slots beyond CS are not admitted.
This is a semantic gate, not a trace-specific MaxCount threshold.

## Changes and ownership

1. `agx_win32_counted_indirect.h:24-64` validates extents, suspends heap callbacks,
   maps only the four-byte count, clamps it to MaxCount, unmaps, and invokes native
   replay on the original argument resource. Zero count emits nothing. Map or
   context failures stop the draw. The public wrapper routes rejected counted
   streams here before graphics entry (`build-native-asahi-state.py:6813-6817`).
   **No RC32 predicate/scratch execution route remains**, including small and
   oversized MaxCount. Path 2 and the original reason survive native recursion.
2. `agx_win32_indirect_stream.cl:20-46` excludes inactive/terminal lanes before
   reading records, then updates the five enabled counters with 32-bit atomics.
   Distinct records can run concurrently, unlike the old one-record IA helper.
   The shared ABI carries five optional addresses and the original Mesa primitive.
3. `agx_win32_indirect_stream_emit.h:23-26` admits ordinary statistics;
   `:47-51,173-180` obtains enabled query addresses through the native query helper,
   checks faults and supplies zero for disabled counters. Native query acquisition
   adds the BO and writer generation (`agx_query.c:531-565`). Resolve flushes that
   writer before its copy (`:480-503`); the existing query-source residency fix
   remains intact. The input resource references and output-pool references remain
   at emitter `:133-152`.
4. `native-asahi-indirect-stream.py:136-139` suppresses the native IA-template
   update during a stream, including count zero. Otherwise the first record would
   be counted twice. Ordinary draw accounting is unchanged. Native PS state and
   query uniforms (`agx_state.c:2290,5082-5087`) remain in the one CPU state encode.

Asahi/UMD owns query accounting, encoding, hazards and transient BO lifetime.
KMD/VidMm owns residency, submission and completion; no caps or interfaces change.
m1n1/Mu initialization, interrupts, DMA, power and recovery contracts are unchanged;
no alternate firmware contract is inferred from this offline change.

The existing full CDM barrier follows each precompiled dispatch
(`agx_state.c:2998-3049`). Windows submits graphics-batch COMPUTE before RENDER
with the existing dependency (`agx_win32_gpuva_batch.c:961-974`). Subsequent query
resolve flushes the tracked writer. This retains the established software ordering
contract; actual GPU coherence/results still need the hardware checkpoint.

Both G13G/G13X binaries were regenerated with local Mesa tools. Atomics cause the
compiler to emit distinct target binaries. Source, output, compiler and topology-
helper hashes are in `indirect-stream-generated/manifest.json`; an independent
regeneration was byte-identical. Existing MIT compatibility review applies:
new integration/kernel code is original; native MIT headers are included from
reference source, no external function body was copied into the repository.

## 10k fault: what the evidence distinguishes

`rec1965h/kmd-receipts-pre-disable.txt:187-191`, decoded in the handoff, records
PID 7248, active fence **0x3cbb**, an event **71 ms** after submission, and the same
raw fault as EXP1964: **0x11ac9a001303cc11**. It is an unmapped read of
**0x11ac9a0000**, VM 1, unit `gCDM_CS_KS0_SP(0)`. Mapping capture has no native leaf
and four invalid logical PTEs. Its only matching history record is a broad invalid
leaf update starting at 0x11ac040000, 8128 entries, allocation token zero; it does
not identify a previous owner. CPU graph metadata is not a hardware PTE read.

The recovered dump header is ARM64 PAGEDU64, 8 CPUs, stop **0x116**, parameters
`ffffd98ea01272e0`, `fffff802e3b71ec0`, `ffffffffc0000483`, `3`.
`dump-header-1965.json` records its SHA and raw header-derived fields. No cdb was
run and no instruction/function is inferred from P2. ResetFromTimeout again
returns c0000483 after a 1001 ms quiescence wait; TA 697/699, 3D 1060/1062 and
compute 739/740 remain unfinished. TDR recovery is unchanged.

**The captured failing job is combined render plus CDM, not the standalone
predicate producer.** Both retirement snapshots have phase 3 and mask 7.
`render_gpu_recovery_snapshot.h:26-28` derives that mask from ComputeTerminal and
ComputeInRender; `apple_agx_g13_queue_runtime.c:280-282` sets standalone compute
phase 1, whereas `:409-415` sets combined phase 3. RC32 used a separate compute
batch and flushed it before native draw recursion (base
`agx_win32_counted_indirect.h:48,83-99`). This materially favors a downstream
pre-raster consumer, such as native IA statistics reading RC32 scratch, over a
fault while the standalone predicate kernel produces scratch. It does not name
the CDM kernel, prove the faulting address is scratch, or exclude another
pre-raster read/shared residency defect.

A 12000-indexed-record scratch request is **240000 bytes**, below the **262144-
byte** slab. Base projection `build-native-asahi-state.py:6033-6044` already checks
remaining capacity by subtraction and allocates new backing if the current offset
cannot fit. Its WOW64 guard and RC32 helper match EXP1965 source.zip exactly
(`experiment-source-comparison.json`). The predicate's scratch is an output;
a read fault there would be in a consumer. Inputs were extent-validated and
referenced; source alone does not prove runtime mappings remained valid.
There is no saved per-dispatch argument/count/scratch VA association at the fault.
No simple slab-size overflow or exact root cause is established.

RC36 removes RC32's entire producer/scratch/native-consumer route. The new stream
uses existing GPU-only output pools and original argument BOs; its statistics
reads inherit the same bounded arguments and native references. This is the
requested conservative disposition of RC32, **not proof that the underlying
unmapped read is repaired or exclusive to RC32**. Another metadata-only experiment
is not proposed; the next candidate changes behavior and checks the failure.

## Verification and next owner checkpoint

RED/GREEN: fallback actual-count assertion; active-statistics eligibility; actual
kernel accumulation (runtime assertion after the ABI fields were added).
Tests cover count zero/clamp/large limits, disabled queries, heterogeneous draw
records, topology and instance rules, 32-bit wrap, query-address failure,
BO/callback boundaries, unchanged heap hazards, native IA suppression and complete
projected translation-unit compilation. A host ASan/UBSan kernel harness uses real
native packing and real atomic additions. GPU execution is not simulated by that
host harness. Independent review found no blocking issue.

72 affected tests pass. The prescribed full baseline is:

```
ran 556 tests in 240 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

An intermediate full run caught a missing `mode` field in the direct-draw hazard
test's reduced type. Its fixture was updated; no assertions or baseline exceptions
were removed. No new environment failure. Host compile does not replace the
Windows ARM64/x64/WOW64 ABI/link gate or validate GPU command fetch/coherence.

WHY THIS HYPOTHESIS:
- Every 2k counted call was rejected solely into reason 3/path 1; active pipeline
  statistics and the unconditional query gate explain the regression directly.
- Pinned Asahi's five IA/VS/clipper computations have deterministic equivalents in
  the stream kernel; tests show no inactive-record reads or template double count.
- The 10k stalled job includes render-batch CDM, the path containing the many native
  IA helpers; this candidate removes that per-MaxCount scratch-consumer sequence.

Hardware owner only: unchanged EXP1963/1965 launch (expanded RAM, m1n1 075e3980,
full 2688 MiB pool, Wom1GpuFaultInfoRead=1), exact candidate/package hashes and
established recovery. First counted indexed/nonindexed draws with statistics:
zero/one/clamped count, instances, triangles/strips without restart, query resolve,
then an ordinary draw. Verify exact IA/VS/clipper values and matching PS/pixels;
GS/HS/DS/CS counters must not be inflated by the generator. Check restart statistics
and simultaneous occlusion/predication on the native fallback. Require path **3,
reason 0** for eligible draws while statistics are active; path **2** with the
original reason for unsupported cases. Path **1** must never appear.

Then GravityMark 2k and 10k, separate processes: compare pixels/query output,
Score/FPS, total/native counted-call times and median/tail frames with EXP1963
(784/4.7 and 798/4.8). Capture fault/reset/UMD evidence before exact-package cleanup
using the existing recovery contract. No performance gain or hardware stability
is claimed offline. Remaining unknowns: precise faulting kernel/allocation,
GPU query/command coherence, native Windows builds, hardware query values,
new-path immunity to the old fault, and net performance.
