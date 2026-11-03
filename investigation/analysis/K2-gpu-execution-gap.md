# K2: the remaining GravityMark GPU-execution gap

The largest measured resolution-independent region is the **8.6 ms C4
per-asteroid compute job**. A generic, default-off compiler lowering now reduces
its traversal kernel's shared loads from 202 to 70 and retained instructions
from 1334 to 1201 without changing registers, occupancy tier, stores, barriers,
atomics or waits. This is a demonstrated reduction in static work, **not a
measured frame-time gain**. It neither substitutes a different traversal
algorithm nor assumes that Metal does no traversal.

Deleting ExecuteIndirect preludes is not the largest arithmetic opportunity:
the saved EXP2075 timings put all render preludes together at **0.21 ms/frame**.
They also prevent most candidate pairs from using depth two, but native AGX
indirect drawing is not native GPU-counted multi-draw. Honeykrisp likewise
preprocesses counted indirect arguments. No source-supported, no-prelude
replacement for the counted path was found.

## Evidence scope and reproduction

Offline base `c15863e79b567a4443e44a34300470a84cbf28a3`, branch
`codex/k2-gpu-execution-gap`. Paths starting `mesa/` below mean the pinned
reference `.local/reference/mesa` (commit
`9aa1215f878b504f66159dd2ead4c7973142126e`). Driver paths are repository-relative.
Handoff artifacts are in
`.local/handoff/codex/k2-gpu-execution-gap/evidence/` under the shared repository
local directory. No guest, hardware, builder, package or other worktree was
accessed. The task's EXP2108 reference supersedes the older compact state for
this offline comparison; no compact hardware state or experiment ledger was
changed.

Inputs include K1 TASK/ANALYSIS/REPORT, selected binaries/NIR and all 274 Apple
listings; Q18 DESIGN and Q19 DESIGN/REPORT; the requested EXP2075-2108 entries.
EXP2080 (`investigation/EXPERIMENTS.md:67166`) rejected K1's shared-load
*spacing*: third traversal 2.06/2.02/1.98 ms in off/on/off. This change instead
reduces the number of shared-load instructions. No timing claim is inferred
from the old spacing experiment.

Reproduction uses the existing decoder, unchanged:

```sh
python3 tools/agx_gpu_frame_breakdown.py <EXP2075>/gm2075a-umd.log --jobs
python3 tools/agx_gpu_frame_breakdown.py <EXP2075>/gm2075b-umd.log --jobs
```

Here `a` is FHD, `b` HD, both 100k asteroids, temporal=0. The JSON files
`gm2075{a,b}-positions.json`, `-budget.json`, `-additive.json` and frame CSVs
retain the results. Additive means below use complete modal CRCRCRCR frames
after the same first-30% exclusion, 3052 FHD / 3515 HD. Raw UMD SHA-256:

- FHD: `72a549c36fc5298d2e4d68ead3af1b847214f9743a4ce5c9d74329789ef2f5e6`.
- HD: `a2e01798b3699e1cc6370e0df4664c1ec46fea3c8645f66d40329d13d0fad9d0`.

`tools/agx_gpu_frame_breakdown.py:179` maps firmware ends using the local
minimum-detection-delay offset; `:183` starts each span at the later of submit
and preceding own completion. Render starts are stale and are not used.
`:199` validates prelude timestamps; `:213` computes standalone compute time;
`:278` selects the modal job pattern. Launch spans include worker/firmware
latency **and foreign GPU jobs**, not just global idle. C6 has an intervening
adapter sequence in 70.4% HD / 93.1% FHD of these modal frames. Medians from
separate columns cannot be summed into an exact representative frame.
The single-in-flight decoder is appropriate for EXP2075; do not apply its
span assumptions to overlapping EXP2108 jobs as if they were serialized.

## D3D12 per-frame budget

The ordering and per-position medians reproduce the ledger
(`investigation/EXPERIMENTS.md:67062`). Compute columns are firmware start/end;
render work is 3D end minus TA end. TA is prelude-end to TA-end, including
barrier wake-up. A dash means the receipt does not isolate that phase.

| Position | Work | HD ms | FHD ms | Additional HD / FHD phase |
|---|---|---:|---:|---|
| C0 | standalone compute | 1.154 | 2.268 | launch estimate 0.291 / 0.317 |
| R1 | main 3D | 4.291 | 7.972 | prelude 0.102 / 0.102; TA 0.231 / 0.237 |
| C2 | standalone compute | 0.686 | 1.469 | launch 0.290 / 0.311 |
| R3 | small 3D | 0.345 | 0.579 | all work before 3D 0.462 / 0.488 |
| C4 | asteroid compute | **8.616** | **8.587** | launch 0.280 / 0.305 |
| R5 | asteroid 3D | 0.835 | 0.833 | prelude 0.074 / 0.074; **TA 1.957 / 1.957** |
| C6 | standalone compute | 1.992 | 2.020 | launch/foreign estimate 1.346 / 1.642 |
| R7 | final 3D | 0.713 | 1.072 | prelude 0.030 / 0.030; TA 0.769 / 0.765 |

All-frame median wall: **28.351 / 34.749 ms** HD/FHD. The following is an
additive accounting of *mean* modal-frame time; it does not attribute causes:

| Exclusive accounting category | HD mean ms | FHD mean ms |
|---|---:|---:|
| Standalone compute firmware intervals | 12.810 | 15.104 |
| Render 3D intervals | 6.535 | 10.898 |
| Render span before 3D (kick + prelude + TA) | 6.280 | 6.216 |
| Compute span minus firmware interval | 1.947 | 2.514 |
| Completion detection estimate | 0.479 | 0.319 |
| Frame-boundary / own-gap / foreign residual | 0.205 | 0.199 |
| **Total** | **28.256** | **35.250** |

The fourth category is signed algebraic residual under the decoder's clock
mapping, not an independent measurement of CPU launch work. The render
prelude subset is **0.206 / 0.206 ms**, already inside the third row. The
per-position launch medians total 3.832 / 4.282 ms for the seven positions
with identifiable starts; R3's unsplit pre-3D adds 0.462 / 0.488 ms, partly TA.
This explains the historical “4-5 ms launch gaps” description while bounding
what it actually measures. Neither all of that interval nor the ~17 ms CPU
RenderFence waits can safely be subtracted as driver overhead.

C4's disjoint measured kernel groups (HD split diagnostics, not benchmark
score comparisons) are:

| Group | ms/frame | Evidence |
|---|---:|---|
| `97ae4b027aa43c6c`, five launches | **2.77** | EXP2078: 0.05 + 0.15 + 2.04 + 0.53 + rounded 0.00 |
| `a8260e4297efffb3` | **0.94** | EXP2078 individual split |
| `bd59552039e0399a`, `f405bd5f93891b99`, `a6e5a25c7bc49152` | **1.51 combined** | EXP2077, 391 groups each |
| 48 radix dispatches (`ba44473b`, `c78e81b6`, `2ad80d8f`, `b5c046a1`, `219c5f89`, `8b9ae450` suffixes) | **0.79** | EXP2077 eight passes |
| Remaining C4 work / rounded split residual | **2.61** | 8.62 minus the four measured groups |

The ~20 tiny/indirect dispatch chain is only 0.12 ms, and `2308298b53aff18d`
only 0.05 ms (`EXPERIMENTS.md:67104`, `:67125`). They are inside the remainder,
not additional time. The original broad per-dispatch-overhead hypothesis was
rejected; the 391-group trio is not three separate 0.5 ms measurements.

## Best defensible Metal estimate

EXP2108 gives HD 5771 / 34.5 FPS and FHD 4665 / 27.9 FPS; Metal same-machine
reference is 9739 and 7814, FHD 46.8 FPS
(`EXPERIMENTS.md:67050`, `:67761`). Thus current D3D12 periods are about
28.99 / 35.84 ms and Metal about **17.1 / 21.4 ms**. HD Metal period uses the
score's approximately 167-second scoring interval, not a new timing capture.
The total deficits are roughly **11.9 / 14.5 ms/frame**. These score averages
include startup and are not the warmed EXP2075 medians above.

A two-point affine scenario `time = fixed + pixels * cost`, using HD 1280x720
and FHD 1920x1080, gives approximately:

| Fitted component | D3D12 EXP2108 | Metal reference |
|---|---:|---:|
| Fixed component | 23.5 ms | 13.7-13.8 ms |
| Pixel component at HD | 5.5 ms | 3.4 ms |
| Pixel component at FHD | 12.3 ms | 7.6 ms |

This is an underdetermined performance model, not a Metal pass measurement.
It suggests ~9.7 ms of resolution-independent deficit under those assumptions.
The similar 1.69x/1.67x ratios alone do not prove a fixed-cost cause. Shader
variants, scene evolution, cache behavior and startup can violate the model.

The cache supplies structural matches, **not per-pass Metal milliseconds**:

| D3D12 role | Apple listing | What is established |
|---|---|---|
| Binary radix hierarchy construction | `f_06d7c0.s:24`, `:61` | neighbor-key XOR/prefix tests, tie-break and expansion loops; 247 decoded instructions vs 318 offline AGX |
| Link/reset stage | `f_067b80.s:20`, `:51` | 48-byte nodes, word-10 update, optional second link and counter clear; 53 vs 38 instructions |
| Hierarchy refit | `f_06c680.s:30`, `:159` | +/-1e24 bounds, previous atomic value == 1, parent traversal and bound publication; 178 vs 280 instructions |
| Full traversal/append/overflow of `97ae4b02` | no verified match | cache incompleteness, alternate specialization or decomposition remain possible |
| `a8260e42` compaction/append | no verified match | cannot assign a Metal time or claim the work is absent |

These K1 role matches are not proven binding-equivalent binaries. The same
trio makes “Metal does no hierarchy work” untenable. All 274 supplied blobs
contain only 11 decoded atomic instructions, but a cache inventory is not a
complete dispatch trace. The large `f_080040` starts with 2D texture/color work,
125 barriers and no atomics; size alone does not identify traversal.

New metadata cross-check: cache objects `0x0de280`, `0x0e0040`, `0x0e1a00`
contain argument names `render_indirect_buffer`, `render_scalar_buffer`,
`base_indirect` (the first two also `base_counter`). Exact file offsets and
cache SHA-256 are in `metal-indirect-metadata.json`. For example
`apple-dis/f_0de280.s:1` loads arguments and its `:17` onwards fills uniform
state; these are compatible with an indirect setup role. **They do not prove
Metal ICB use, command generation, executed count, or the absence of a
prelude.** No host command-buffer/API capture exists in these inputs. No Apple
implementation code was copied or projected.

The best replacement hypothesis remains different traversal/compaction
specialization or decomposition (e.g. staged queues/scans), not an identified
algorithm that the translation layer can legally substitute. The Metal
sub-budgets are unknown within the measured whole-frame budget; assigning
2.77 ms of “work Metal does not do” would turn absence of a cache match into
unsupported evidence.

## Top three resolution-independent targets

Ranked by useful measured D3D12 cost exposure, not a claimed Metal-minus-Windows
difference. The rows are disjoint except explicitly identified subsets.

1. **C4 algorithm and lowering: 8.59-8.62 ms.** The traversal/append and a826
   subset is 3.71 ms; tree construction/refit plus radix 2.30 ms. This is the
   largest isolated fixed region. Metal counterparts demonstrate some common
   work but not equal cost. The safe translation lever implemented here is
   generic shared-load vectorization within the traversal, not replacing
   application algorithms. Its affected runtime envelope is the traversal's
   2.77 ms; recoverable time is unknown, possibly zero. A purely illustrative
   instruction-proportional scenario is 2.77 * 133/1334 = **0.276 ms/frame**,
   but loop execution, vector load latency and atomics invalidate that as a
   prediction. Even that scenario yields only ~1%, nowhere near 1.7x.
2. **Submission/firmware launch serialization: approximately 4-5 ms gross
   exposure.** The seven separately identified launches total 3.83/4.28 ms;
   R3 adds at most another 0.46/0.49. Some is foreign GPU work, and the best
   configuration already overlaps accepted non-prelude pairs. EXP2108's extra
   CPU lead adds only 1.7% HD / 0.2% FHD (`:67761`); Q18/Q19 own-gap bounds
   explain why deleting CPU waits cannot recover their full duration.
   Prelude-pair gate refusals were 32669/39522 = 82.66% in EXP2102
   (`:67636`), **not 83% of GPU time**. Removing *all prelude execution* saves
   at most the observed ~0.21 ms directly. Any larger gain requires proving
   new queue overlap; the failed EXP2100 topology must remain gated.
3. **Asteroid vertex/tiling/render work: R5 TA ~1.96 + 3D ~0.83 = 2.79 ms at
   both resolutions.** This excludes its prelude and launch, counted above.
   Repeated native VS/USC setup is a plausible translation contribution, but
   no receipt separates it from necessary vertex shading, index fetch or
   tiling. Metal's 17.1 ms total and indirect metadata do not quantify it.
   No TVB partial renders occur in these traces. The native-draw shape and
   per-record params must be preserved; 0..2.79 ms is only a gross exposure
   range, not a forecast that all asteroid geometry is removable.

## Windows, Asahi and translation contracts

Pinned `wdk26100/d3d12umddi.h:2468` passes command signature, MaxCommandCount,
argument placement and count placement. `winsdk26100/um/d3d12.h:4628` names
state-changing indirect arguments; `:6580` includes optional count-buffer use.
The DDI records pure draw/dispatch versus expanded state signatures at
`drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp:6165`. Winsys constructs Gallium
argument/count resources at `mesa/winsys/agx_d3d12_windows.cpp:4787`.

Native Mesa `agx_state.c:5224` emits a single indirect draw via
`libagx_dgc.h:273`. Its index-list packet has a pointer to one argument record
(`cmdbuf.xml:910`), no max/count-buffer operand. `agx_state.c:4877` unrolls
fixed multi-draws, `:4891` falls back for counted draws; Vulkan
`hk_cmd_draw.c:3820` explicitly preprocesses count predication with compute
then emits maxDrawCount indirect draws. Conditional VDM branching is only a
speculation in `cmdbuf.xml:915`, not a verified replacement interface.

Windows' GPU stream (`agx_win32_indirect_stream_emit.h:201`) supplies count
clamping, output bounds, per-record USC/params, query updates and links. Its
kernel (`agx_win32_indirect_stream.cl:45`, `:54`, `:70`, `:108`) copies native
state and draw parameters, produces bounded records and a terminator. RC69
comments document hardware failure when the complete per-record VS state was
not rebound. `agx_win32_stream_flush_inputs` (`emit.h:157`) flushes argument
writers because render-batch CDM runs before *all* draws. Simply moving that
CDM to a previous compute batch needs a new ownership/hazard proof, not a
changed flag. `agx_win32_gpuva_batch.c:693` uses the same compute-before-render
model as Asahi. No guaranteed ordering or parameter publication is removed.

UMD owns semantic lowering and pending resources; VidMm/KMD own placement,
DMA, interrupts, queues, completion and recovery; firmware/m1n1/Mu own their
existing initialization/power contracts. No changes to those lower layers,
caps, package identity or recovery are needed for this compiler candidate.
Saved EXP2108 is the launch contract for the later owner-run comparison.

## Implemented default-off candidate

`APPLE_AGX_SHARED_LOAD_VECTORIZE=1`, exact value only, sampled once per compiler
process. `native-asahi-latency-schedule.py:618` adds compute shared memory to
the existing NIR load/store vectorization modes and forwards to the existing
AGX callback, explicitly refusing shared-store combinations. Other modes keep
the same callback result. All definitions precede use in the projected TU;
the compiler closure and host cost fixture use this same projection.

Primary evidence:

- `mesa/src/asahi/compiler/agx_compile.c:2884`: existing modes exclude shared.
- `:2815`: callback refuses holes, >4 components, >32-bit and underaligned
  components; no extra bytes need to be fetched for dense merging.
- `:1028`: local-load emission handles a dense component mask and splits the
  result; `:1050` has the shared-store write-mask TODO, so stores stay excluded.
- `mesa/src/compiler/nir/nir_opt_load_store_vectorize.c:1370` checks interfering
  accesses, `:1499` refuses volatile/unvectorizable operations, `:1802` handles
  memory barriers, `:1982` respects acquire/release partitions. These existing
  alias/order proofs are reused, not reimplemented or weakened.
- `mesa/src/asahi/compiler/agx_performance.c:16`: 79 halfregs remains in the
  <=104 / 1024-thread register ceiling. This is not actual resident occupancy.
- `native-asahi-batch-lifecycle.py:860` removes native disk-cache initialization;
  in-memory shader reuse is per process. Every switch arm starts a fresh process.

Retained offline results (`vector-metrics.json`, full selected binaries):

| Kernel | Instrs off -> on | Shared loads | GPR / ceiling | Other effects |
|---|---:|---:|---|---|
| 97ae4b027aa43c6c | **1334 -> 1201** | **202 -> 70** | 79 / 1024 unchanged | 79 stores, 61 waits, 6 atomics, 2 barriers, no spill/fill/scratch |
| a8260e4297efffb3 | 1108 -> 1108 | 12 -> 12 | 127 / 832 unchanged | binary identical |
| bd59552039e0399a | 318 -> 318 | 0 | 31 / 1024 | binary identical |
| f405bd5f93891b99 | 38 -> 38 | 0 | 11 / 1024 | binary identical |
| a6e5a25c7bc49152 | 280 -> 280 | 0 | 47 / 1024 | binary identical |

The vectorized traversal has 30 xyzw, 16 xyz, 3 xy and 21 scalar shared loads:
195 statically read components versus 202 before (overlapping redundant reads
can also be merged). Code shrinks 9774 -> 8588 bytes. Dynamic component traffic
and cycle savings are unknown. Offline uses root-view masks e,3 and load-only
soft-fault approximation, as K1; it is not identical to the hardware 1310
instruction executable. No shader hash, constant offset, trace size or bind
combination appears in the production admission rule.

Host regressions use initialized shared data, real NIR and the real projected
backend's **selected** executable. Dense eight-word reads become two xyzw
loads, with all 16 producer stores scalar and before the barrier. Split
three/five-word groups stay on their own side of the barrier with exact
component totals. Holes and uncertain dynamic 16-bit adjacency retain scalar
loads and identical binaries. Unset/0/invalid switches preserve baseline.
The first dynamic fixture was an incorrect positive expectation: the vectorizer
cannot prove adjacency through its narrowed address expressions. It is retained
as a negative control; the constant-address case matches the actual traversal
transformation. Final regression fails against the original production
projection and passes after restoration (`vector-red-final.log`, `targeted.log`).

## Smallest hardware checkpoint for the integration owner

WHY THIS HYPOTHESIS:
1. EXP2078 isolates 2.77 ms/frame in this traversal, including a 2.04 ms launch;
   its shared coefficient reads are concrete repeated static work.
2. The changed retained binary removes 132 shared-load instructions and 133
   total instructions with the same registers, barriers, atomics and waits.
   This is new evidence, distinct from the rejected spacing-only EXP2080 arm.
3. a826 and the trio remain byte-identical, giving useful unchanged timing
   controls; no firmware, scheduling or recovery variable changes.

Integration first builds/links the consistent projected compiler for ARM64,
x64 and WOW64 with the native Windows toolchain, hashes source/package/manifest
and known-good recovery, and preregisters the run. Host tests do not establish
MSVC /W4 /WX, package linking or on-device correctness.

Use one package, fresh process each arm, exact EXP2108 configuration (depth two
non-prelude classes, UMD submission window 16, diagnostics mask unchanged),
**switch 0 / 1 / 0**, GravityMark HD then FHD, 100k asteroids, temporal=0.
Keep `APPLE_AGX_LOCAL_LOAD_SCHED=0`; keep the prelude gate closed. Reference
scores: **HD 5771 / 34.5 FPS, FHD 4665 / 27.9 FPS**. Compare against both
same-package controls, not only historical score. Capture shader stats,
correct image/output, scores/periods, faults/reset/WER and normal exit.

A convincing performance checkpoint is >=1% repeatable whole-frame improvement
below both controls, no correctness/regression failures, and confirmed intended
binary. A smaller change is inconclusive at the old run-to-run noise floor.
If score is noisy, the smallest causal follow-up is the identical EXP2078
split-tag diagnostic in all three arms: compare five traversal launches and
C4 sum, require >=5% traversal improvement below both controls; do not compare
split-run scores to unsplit EXP2108. Controls differing >=5% make that timing
inconclusive. Zero gain rejects the performance hypothesis and leaves the
switch off. No new receipt-only series is proposed.

Any wrong output, TDR, reset, removal or new smoke/query/indirect regression is
FAIL. Save evidence/dump before the owner's normal exact-package cleanup to
GPU-visible Code28 using the preregistered rec2108h-equivalent recovery.
Switch 0/absent restores the previous compiler behavior. No experiment was
run or preregistered in this task; integration owns both ledgers.

## WHAT IS STILL UNKNOWN

Metal per-pass times, complete shader variants/argument identity and actual
ICB use; how Metal implements the missing traversal/compaction stage; dynamic
loop trips, active lanes, bank behavior, memory/atomic stalls and GPU clocks;
true foreign GPU share in launch spans; current EXP2108 per-job budget;
correctness/performance of vector shared loads under real workload pressure;
MSVC builds and complete shader/graphics regression coverage. Static work
reduction does not establish any part of the missing 1.7x speedup as solved.

## Independent review and verification scope

Independent final review found no critical/important issue. Deferred minor:
new executable tests do not directly cover aliasing stores/atomics between
loads, 8/16-bit components or the 16-bit shared-address boundary. Those
legality properties were checked in the existing vectorizer/emitter source;
this is a coverage limitation, not a demonstrated defect or hardware proof.
The focused suite passed 20 tests. The full run reported 840 tests in 334
modules; 6 failing, 6 known, 0 NEW, 0 FIXED; BASELINE_OK. The native Windows
compiler and hardware checkpoints remain unperformed.
