# K1: GravityMark per-asteroid compute, offline analysis

The evidence supports dependent memory access and divergent hierarchy traversal as
97ae4b02's main suspects, not a shortage of registers or threadgroup memory.
It does **not** distinguish their runtime shares or prove that atomics alone
account for the 2.04 ms launch. There is one reproducible compiler omission:
the existing latency scheduler excludes shared-memory `local_load`. The supplied
default-off experiment fixes that omission without changing the dependency graph,
barriers, atomic operations, or explicit waits. It is not a demonstrated frame-time fix.

## Inputs, reproducibility, and limits

Repository base: `cd2055ef` on `codex/k1-asteroid-compute`. Only this worktree was
edited. Mesa reference: `9aa1215f878b504f66159dd2ead4c7973142126e`.
`tools/agx_shader_cost.py:115-184` compiles the same projected compiler units used
by Windows; `tests/dxil_agx_cost_fixture.c:1-9` explicitly describes the uniform
and binding stand-ins. All five captures were compiled with
`--root-views=e,3 --soft-fault-loads`. These masks are the task's approximation,
not verified root signatures for every shader. Offline 97ae4b02 is 1,334
instructions / 79 halfregs, versus the task's hardware 1,310 / 87. The ledger
also records the later rc123 small-shader reservation correction
(`investigation/EXPERIMENTS.md:67044-67046`). Do not substitute these static
counts for the exact hardware executable.

Hardware evidence was read only: EXP2076-2078
(`investigation/EXPERIMENTS.md:67083-67129`). The 391-group trio is **one combined
1.51 ms measurement**, not three individual timings. Indirect group counts,
active lanes, cache misses, atomic collisions, and dynamic loop trips are absent.
No hardware, guest, builder, package, or other worktree was accessed.

Evidence paths below are relative to the handoff directory
`.local/handoff/codex/k1-asteroid-compute/` unless prefixed with repository paths.
`evidence/<hash>-0.txt` and `-1.txt` contain the **selected main executable's**
disassembly and COST record; `-nir.txt` contains frontend/backend NIR. Compiler
`AGX_MESA_DEBUG=shaders` dumps rejected variants and preambles too; it is not
safe to take its last listing as the retained main function. The fixture's new
`--print-selected` disassembles `main_offset/main_size` after variant selection.
`evidence/metrics.json`, `target-sweep.json`, and `apple-inventory.json` contain
machine-readable results. `evidence/SHA256SUMS` pins the inputs and evidence.

Reproduce after building a persistent fixture with `tools/agx_shader_cost.py`
(the programmatic `build(Path('.local/k1-cost'))` entry):

```sh
APPLE_AGX_LOCAL_LOAD_SCHED=0 .local/k1-cost/dxil_agx_cost_fixture \
  /Users/pavel/public_windows/.local/experiments/EXP2070-shader-capture/evidence/EXP2070/capture/97ae4b027aa43c6c-s5-r0.bin \
  --root-views=e,3 --soft-fault-loads --print-selected
```

Repeat with the switch `1`; restart the process because switches are cached.
The cost tool CLI spells the option `--soft-fault loads`; its fixture spells it
`--soft-fault-loads`.

## 1. Traversal: 97ae4b027aa43c6c

### Occupancy and shared-memory capacity are not the demonstrated limit

Mesa's occupancy table is a best-case **register** limit:
`src/asahi/compiler/agx_performance.c:10-32` in the reference tree gives
104 halfregs -> 1,024 threads, 112 -> 896, 128 -> 832. Both 79 and 87 are below
104. Lowering 87 to 79 cannot cross an occupancy tier. No scratch, spills, or
fills occur. `agx_register_allocate.c:1488-1498` deliberately rounds available
registers to the occupancy step. `agx_state.c:1992-2003` reports this same
register-derived limit and SIMD width 32, not measured resident groups.

The group has 128 threads, not 1,024: 1,024 is the register-derived thread
ceiling in the stats. Eight 128-thread groups fit that arithmetic ceiling.
Shared memory is 548 bytes/group; USC allocation rounds in 256-byte units
(`src/asahi/genxml/cmdbuf.xml:696`), so 768 bytes/group, 6 KiB for eight groups.
Asahi advertises 32 KiB maximum local memory per group
(`src/gallium/drivers/asahi/agx_pipe.c:1983-1988`) and passes the shader's local
size to USC (`src/asahi/lib/agx_usc.h:41-65`). These sources do not establish
the actual resident-group count or a measured per-core shared-memory pool;
eight is a register-only upper-bound calculation, not an observation. There
is no evidence here of a shared-capacity occupancy cliff.

### The listing has several serial mechanisms

The selected baseline contains 202 `lload`, 79 `lstore`, 57 device `load`, six
`atomic`, 61 `wait`, and two `barrier` instructions. These are static sites,
not per-frame operation counts. The task's 41 SSBO loads and this listing's
57 loads are different lowering levels; the latter also includes constants
and other device accesses.

`evidence/97ae4b027aa43c6c-nir.txt:3775-3803` shows a loop whose child indices
are loop phis, then a shared base load, then device node loads. It subsequently
loads shared plane coefficients and tests dot products in nested branches
(:3804-3850). This is hierarchy traversal with bounding tests. Many of the 202
shared loads are constant-address coefficients, so calling all 202 "stack
loads" would be misleading. The loop depends on the prior node decision and
its lane mask: neither a scheduler nor more bandwidth can issue a dependent
next-node load before its address exists. Divergence and long traversal tails
are plausible costs even with maximum register occupancy.

Concrete selected-binary excerpts (`evidence/97ae4b027aa43c6c-0.txt:1162` and
:1235; addresses are function-relative):

```text
207c: atomic r1, r1, du52, r12.sx, add, coherent1, a
2084: wait a
...
2230: atomic r1, r1, du50, r3.sx, add, coherent1, a
2238: wait a
```

The six atomic sites include two return-value add sites and overflow paths
with OR flags and compensating decrements; they are not six independent
unconditional appends to one universal address. In a given append path the
counter is uniform, but the listing has two bases. All six static sites have
an immediately following wait. The returned reservation indexes output and
guards overflow, so the returning-atomic dependency cannot be eliminated by
moving a wait past its consumer. Unused-result atomics also end blocks.

Uniform atomic combining is already implemented:
`src/asahi/compiler/agx_compile.c:2903` invokes `nir_opt_uniform_atomics`;
`src/compiler/nir/nir_opt_uniform_atomics.c:206-255` reduces active-lane data,
elects one lane, and broadcasts the result plus the exclusive scan. The NIR
and coverage operations in the capture confirm this transformation. Combining
across iterations/groups would change reservation/overflow/visibility behavior
and needs a separate semantic proof; it is not a generic safe peephole.

### Shared latency versus explicit waits and bank conflicts

`src/asahi/compiler/agx_opcodes.py:279-296` defines local loads/stores without
SCOREBOARD. `agx_insert_waits.c:15-18` classifies asynchronous instructions by
that flag. Thus the 61 explicit waits do **not** measure shared-load latency.
They track device/texture/atomic destinations, barrier drains, slot capacity,
and block exits (`agx_insert_waits.c:53-145`, two slots, eight pending messages).
The pass deliberately drains non-exit blocks. Removing these waits without
cross-block hazard tracking would violate the current compiler contract.

The latency classifier at
`drivers/apple-agx/mesa/scripts/native-asahi-latency-schedule.py:88-100`
previously listed only device/texture/image loads. The regression's eight
independent shared reads had use distances `[2,1,1,1,1,1,1,1]` even with the
existing scheduler enabled. The opt-in includes local loads in the same
heuristic. In the traversal's selected binary, 82 scalar local loads have a
recognizable use in the next two instructions with the switch off, versus 24
with it on. This is a static instruction-spacing metric, not cycles saved.
No verified G13 bank mapping, broadcast cost, or per-lane dynamic address
trace is available. Many loads read the same constant address in all active
lanes, so a blanket "32-way bank conflict" diagnosis is unsupported. Do not
pad the layout or invent bank/latency numbers from the instruction count.

**Ranking:** dependent node/atomic latency plus divergent loop execution are
strong candidates; insufficient spacing of shared loads is a confirmed
compiler scheduling limitation and a bounded experiment; register/shared
capacity and bank conflicts are not supported as the primary cause. Exact
ranking of the first three requires timing/counter evidence not in this task.

## 2. The 391-group trio and a8260e42

Current selected offline results, switch off; explicit waits exclude preamble:

| Shader prefix | Group | Shared B | Instrs | Halfregs | Register thread ceiling | Device loads | Atomic sites | Wait sites |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| bd595520 | 256 | 0 | 318 | 31 | 1024 | 13 | 0 | 28 |
| f405bd5f | 256 | 0 | 38 | 11 | 1024 | 3 | 0 | 6 |
| a6e5a25c | 256 | 0 | 280 | 47 | 1024 | 21 | 1 | 25 |
| a8260e42 | 256 | 12 | 1108 | 127 | 832 | 106 | 3 | 90 |

The first three fit four whole 256-thread groups under the register ceiling;
they have no shared memory, so shared capacity or bank conflicts cannot explain
the combined 1.51 ms. 391 * 256 = 100,096 invocations per direct dispatch,
but static instruction count times that number ignores loops and lane masks.

- **bd595520** reads sorted keys, XORs neighbors, computes common-prefix lengths,
  expands an interval, then searches/splits it. Three loops are visible at
  `evidence/bd59552039e0399a-nir.txt:129,196,317`; :139-170 shows a key load and
  prefix computation determining termination. This strongly identifies binary
  radix-tree construction. Its reads are small and control dependent; bandwidth
  is a poor sole model. Divergent loop length and load-to-branch latency are
  stronger than occupancy. The 28 waits include stores/block drains.
- **f405bd5f** is the short linkage/reset stage: reads keys/links, writes a
  12-dword node's field 10, follows an optional second link, clears a counter
  (`evidence/f405bd5f93891b99-nir.txt:28-110`). It has no loop or atomics.
  The combined timing cannot justify assigning it 0.5 ms or calling it slow.
- **a6e5a25c** climbs a hierarchy and accumulates bounding minima/maxima. It
  starts with +/-1e24, atomically increments a node counter and continues only
  when the previous value was one (`evidence/a6e5a25c7bc49152-nir.txt:60-85`).
  The next parent is data dependent. This is synchronization/latency-sensitive
  tree refit, not a simple linear bandwidth pass. Registers are not limiting.
- **a8260e42** has 12 B shared metadata, two barriers, one loop, nested branches,
  106 device loads, and append/overflow atomic operations
  (`evidence/a8260e4297efffb3-nir.txt:60-142,1729,1911-1922`). Its 127 halfregs
  put it in the 832-thread tier: three complete 256-thread groups by arithmetic,
  possibly less effective occupancy than the trio. This is a lower register-derived
  capacity ceiling, but not proof that occupancy dominates 0.94 ms. The 90 waits
  and data-dependent work make device latency and divergence strong suspects.

A bounded offline sweep of existing `APPLE_AGX_LATENCY_SCHED=0,4,8,16,32,64`
(`evidence/target-sweep.json`) finds **no occupancy improvement** for either
heavy kernel. 97ae instruction counts span 1332-1337, always 1024 threads;
a826 spans 1108-1141, always 832. Lowering registers to 112 would yield only
896 threads, still three full groups of 256. Crossing to 104 is needed for
four full groups. No spill-free route to that threshold was demonstrated.

## 3. Apple comparison: partial structural matches, no traversal identity

All 274 supplied disassemblies were inventoried. There are 11 decoded atomic
instructions in four blobs, not zero. `device_load.TODO` exists in the decoder;
raw opcode counts have decoding limitations. Cache completeness and actual
Metal dispatch/binding identity are unknown. These listings alone cannot prove
that Metal lacks a matching kernel or uses one particular alternate algorithm.

Three strong structural counterparts to the trio **do** exist:

| DXIL | Metal candidate | Evidence and differences |
|---|---|---|
| bd595520 | `apple-dis/f_06d7c0.s` | :24-60 neighbor key loads, XOR/bit search, equal-key index tie-break; :61-90 expansion loop; three backwards jumps overall. Same radix-hierarchy construction shape; Metal 247 decoded instructions, 14 waits vs offline 318/28. Metal stages a four-word descriptor in shared memory; Windows reads uniforms. Counts include different setup and are not equal-work timing estimates. |
| f405bd5f | `apple-dis/f_067b80.s` | :20-50 48-byte node stride, update word 10 through an xyzw read/modify/store, optional second link, then :51-54 counter clear. Same input/output role and sentinel branch as the DXIL stage. Metal 53 decoded instructions / 4 waits vs 38/6; Metal's wider stores explain extra loads. |
| a6e5a25c | `apple-dis/f_06c680.s` | :30-44 +/-1e24 constants and atomic-add previous-value == 1 loop; :45-49 12-dword node/parent fields; :159-176 six atomic exchanges for bounds, parent loop. Metal 178 decoded instructions / 10 waits / seven atomic sites vs 280/25/one atomic site. Six exchanges publish the bounds; the counter traversal is structurally the same. Different synchronization lowering, not evidence for indiscriminate wait deletion. |

These are high-confidence functional-role matches, not verified binding-equivalent
binaries. In particular, finding the same trio argues against a claim that the
**entire** Metal per-asteroid algorithm is unrelated.

No blob matches the full 97ae4b02 combination of traversal, 202 shared loads,
79 shared stores, two barriers, and append/overflow atomics. Of the remaining
atomic blobs, `f_0a8ec0.s` has 12 shared loads, 14 stores and five barriers
(:69-89 demonstrates vector shared loads and geometry transforms), while
`f_06b800.s` and `f_05ca00.s` are much smaller. The largest compute-like blob
`f_080040.s` is a poor match: :14-40 starts with a 2D texture load and colour
math, 125 barriers and no atomics. Its size is not evidence of traversal.

The defensible inference is a different **decomposition or variant of traversal
and output compaction**, potentially staged work queues/scans instead of the
DXIL append-counter path, or an uncaptured specialization. A scan/queue variant
is a hypothesis, not an identified Apple algorithm. Exact Metal argument/dispatch
capture is needed to distinguish it from cache incompleteness. A826 also has no
verified one-to-one counterpart here.

## 4. Concrete changes and expected observable effects

| Candidate | Offline estimate / decision |
|---|---|
| **Implemented, default off:** shared-load latency classification | 97ae 1334 -> 1334 instructions, 61 -> 61 waits, 79 -> 87 halfregs, 1024 threads unchanged; 82 -> 24 shared-load uses within two instructions. a826 1108 -> 1106 instructions, 90 waits unchanged, 127 halfregs / 832 threads unchanged. Trio binaries unchanged. No milliseconds claim. |
| Existing distance target 16 -> 64 | 97ae saves only two static instructions; a826 no instruction/occupancy gain. Not the strongest causal test. |
| Reduce a826 register target | Need <=104 halfregs to change whole-group capacity from three to four. Existing sweep reaches only 119 with scheduler off, still 832 threads, while adding 33 instructions. No demonstrated gain; do not force spills. |
| Move atomic waits out of traversal | Two returning atomic sites require the result immediately. Four unused-result sites also drain at block exits; at most four static waits are candidates for a future CFG scoreboard analysis, **zero proven removable here**. No safe patch or timing estimate. |
| Further atomic batching | Wave combining is already active. No further reduction proven under overflow/ordering semantics: zero justified static atomic removals. Cross-iteration batching is an application/algorithm change. |
| Change shared layout / padding | No bank model or conflict measurement; zero justified wait/instruction reduction. Not implemented. |

The implemented switch changes only ready-node classification, within the
existing block DAG (`src/asahi/compiler/agx_pressure_schedule.c:46-113`). It
never moves instructions across control flow, memory dependencies, or barriers.
Post-RA acceptance remains the same occupancy/spill/fill/scratch/instruction
check (`native-asahi-latency-schedule.py`, `agx_win32_latency_keeps`). Exact `1`
enables it; unset, `0`, and invalid values preserve previous behavior;
`APPLE_AGX_LATENCY_SCHED=0` still selects the pinned compiler. The added function
is defined in the projected translation unit before use. Existing code already
uses the same environment-query convention; no new Windows ABI is introduced.
External source was inspected, not copied into the production implementation.

### One smallest hardware checkpoint, for the integration owner only

WHY THIS HYPOTHESIS:
1. EXP2078 localizes 2.04 ms to the traversal; it has 202 shared loads, and
   per-dispatch overhead was rejected by EXP2077.
2. The existing heuristic excluded this entire load class; the synthetic case
   and captured retained binary now show materially more independent spacing.
3. Registers stay in the same occupancy tier and explicit waits/atomics/access
   counts stay fixed, providing a narrow causal comparison.

Use the same verified package and benchmark settings in an ABA series with
`APPLE_AGX_LOCAL_LOAD_SCHED=0 / 1 / 0`, restarting the benchmark process each
arm; keep the existing EXP2078 split tags identical. Compare warmed medians
and distributions of the **third 97ae launch** (reference 2.04 ms), also report
all five occurrences, a826, and the unchanged trio. A candidate checkpoint:
at least 5% reduction in the third launch with the middle arm below both
controls, no image/output difference, no fault/TDR/WER, no regression in a826
or total compute. If controls vary by >=5%, timing is inconclusive. Stats must
confirm the intended binary changed and no spill/occupancy loss. Use the
owner's ordinary recovery and exact-package cleanup on failure; unset/0 is the
compiler rollback. Do not judge split-run benchmark score against unsplit runs.
Nothing in this report preregisters or executes hardware.

## Test contract and remaining unknowns

The new regression compiles real NIR through the projected backend, with all
shared addresses initialized by 64 lanes before a workgroup ACQ_REL barrier.
It consumes another lane's data, verifies eight stores and eight consumed loads,
checks store < barrier < load ordering in the **selected binary**, verifies
increased load-to-use distance, and checks occupancy/spill/fill/scratch plus
unset/off/invalid/master-off controls. It failed before the production change
with distance sum 9 vs 9, then passed. No shader hash or workload-specific shape
is admitted in production.

Unknown: true dynamic stall composition; indirect grids; active-lane masks;
local-load latency and banks; caches/counter contention; exact hardware bindings;
Metal cache/dispatch identity; runtime correctness and performance of the
opt-in on G13; MSVC ARM64/x64/x86 package builds. The offline test is a scheduling
invariant, not an execution emulator or hardware validation. Full baseline and
commit identities are recorded in REPORT.md.
