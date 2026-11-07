# K3: C4 compiler costs after shared-load vectorization

The strongest **demonstrated and implementable** next omission is dense shared-store
vectorization. With K2 enabled, the default-off K3 option changes the traversal
from 1,201 to 1,163 instructions and from 79 to 37 shared stores, at the same
79 half-registers / 1,024-thread register ceiling. A826 loses one instruction.
The hierarchy trio, all six radix stages and lighting are byte-identical.
This is an offline compiler result, not a measured frame-time improvement.

A material limit: all 42 removed traversal stores are **before the first
workgroup barrier**, in initialization. K3 does not remove work from the hot
traversal loop. It is consequently a smaller and less certain runtime opportunity
than K2's repeated coefficient loads. A conditional instruction-proportional
estimate is about **0.09 ms/frame**, likely optimistic when loop work dominates;
zero measurable improvement remains plausible. Do not extrapolate EXP2110's
3% gain to this option merely by counting removed memory instructions.

## Evidence, scope and reproduction

Base `13430c4d899c0bbb6ca56e9b472c6748aa59a1b1`, branch `codex/k3-c4-kernels`.
Pinned Mesa `9aa1215f878b504f66159dd2ead4c7973142126e`. Reference paths below
start at `.local/reference/mesa`; `evidence/` and `apple-dis/` paths refer to the
K3 and K1 handoff folders respectively under
`/Volumes/pwdev/public_windows/.local/handoff/codex/`.

Read K1/K2 analyses, reports, retained NIR/disassembly and Apple inventory, and
only the referenced experiment entries. EXP2110 records HD 5949 / 35.6 FPS,
FHD 4803 / 28.7 FPS, correct images and no faults/WER with K2 enabled
(`investigation/EXPERIMENTS.md:67803`). EXP2080 rejected K1 shared-load spacing
(`:67166`); EXP2077/2078 supply the older split timings. No hardware, guest,
builder, network, package or other worktree was accessed. Firmware, ACPI and
live registers are outside this compiler-only contract; no new state-dependent
hardware conclusion is asserted.

The 12 requested kernels are the traversal, a826, three hierarchy kernels,
six radix stages and lighting. The older C4 budget also contains about 2.61 ms
of other work/residual; this analysis does not relabel that residual as one of
these kernels or claim complete dynamic coverage of C4.

`measure.py` builds on `tools/agx_shader_cost.py:115` and uses the real projected
compiler. `tests/dxil_agx_cost_fixture.c:1` documents its Gallium binding/sysval
stand-ins. Every capture uses `--root-views=e,3 --soft-fault-loads`, K2=1,
K3=0 or 1, all other APPLE_AGX variables cleared. These are the same approximate
bindings as K1/K2, not individually captured root signatures. The hardware
traversal's previous 1,310 instructions and this fixture's original 1,334 must
not be treated as identical executables. NIR is captured **before backend
optimization**; selected assembly is after variant selection. Preambles are
counted separately. Rejected compiler variants are not counted.

Reproduce from the assigned worktree (host Mesa and dxil-spirv already present):

```sh
mkdir -p .local/k3-cost
python3 - <<'PY'
from pathlib import Path
import importlib.util
s = importlib.util.spec_from_file_location('cost', 'tools/agx_shader_cost.py')
m = importlib.util.module_from_spec(s); s.loader.exec_module(m)
print(m.build(Path('.local/k3-cost')))
PY
python3 /Volumes/pwdev/public_windows/.local/handoff/codex/k3-c4-kernels/measure.py 0
python3 /Volumes/pwdev/public_windows/.local/handoff/codex/k3-c4-kernels/measure.py 1
python3 /Volumes/pwdev/public_windows/.local/handoff/codex/k3-c4-kernels/summarize.py
```

`metrics-{0,1}.json`, `static-metrics.json`, all selected listings/NIR,
`apple-metrics.json`, `change-regions.json` and `FINAL_SHA256.json` retain and
pin the evidence. Original-production versus candidate-with-K3-off results
were checked identical across all 12 captures.

## Instruction mix with K2 enabled, K3 disabled

These are static sites, not dynamically executed instructions or cycles.
SL/SS = shared load/store; DL/DS = device load/store; W = explicit wait;
A = arithmetic, move and immediate instructions; C = conversion subset of A;
CF = if/else/while, exec-mask stack and jumps; At = atomics; B = all barrier
instructions; T = texture/image instructions. Each row totals I when adding
SL+SS+DL+DS+W+A+CF+At+B+T+one `stop`; C is not added again.
AGX uses the device-load opcode for global, buffer and constant-memory reads.
It is not possible to split that opcode into global versus constant by its
name alone. The NIR table below supplies that semantic distinction.
The COST field named `alu` is a crude throughput estimate, **not an instruction
count** (`agx_compile.c:2697`, `agx_performance.c:128`); this table counts decoded
instructions instead.

| Kernel | I | SL | SS | DL | DS | W | A | C | CF | At | B | T |
|---|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|---:|
| 97ae4b027aa43c6c | 1201 | 70 | 79 | 57 | 14 | 61 | 762 | 0 | 149 | 6 | 2 | 0 |
| a8260e4297efffb3 | 1108 | 12 | 3 | 106 | 10 | 90 | 732 | 0 | 149 | 3 | 2 | 0 |
| bd59552039e0399a | 318 | 0 | 0 | 13 | 15 | 28 | 182 | 0 | 79 | 0 | 0 | 0 |
| f405bd5f93891b99 | 38 | 0 | 0 | 3 | 3 | 6 | 16 | 0 | 9 | 0 | 0 | 0 |
| a6e5a25c7bc49152 | 280 | 0 | 0 | 21 | 12 | 25 | 153 | 0 | 56 | 1 | 11 | 0 |
| d73f8477ba44473b | 221 | 12 | 11 | 2 | 12 | 14 | 115 | 0 | 45 | 0 | 9 | 0 |
| e242737cc78e81b6 | 103 | 18 | 8 | 1 | 2 | 3 | 45 | 0 | 21 | 0 | 4 | 0 |
| bbfcb4662ad80d8f | 113 | 2 | 2 | 5 | 6 | 9 | 59 | 0 | 27 | 0 | 2 | 0 |
| 7d355f20b5c046a1 | 108 | 2 | 2 | 5 | 5 | 8 | 58 | 0 | 25 | 0 | 2 | 0 |
| 65e8cdbe219c5f89 | 72 | 1 | 1 | 6 | 5 | 9 | 28 | 0 | 20 | 0 | 1 | 0 |
| 9dfc864e8b9ae450 | 108 | 10 | 6 | 4 | 4 | 8 | 57 | 0 | 17 | 0 | 1 | 0 |
| 806ee7b1bdb1944a | 3580 | 0 | 0 | 99 | 0 | 108 | 2977 | 29 | 344 | 0 | 0 | 51 |

The refit has 11 device/cache barrier opcodes from one NIR memory barrier; these are not 11 workgroup synchronizations. Lighting has 50 sample/fetch instructions and one image write. All C4 kernels have zero explicit conversion opcodes; NIR address-width casts mostly become addressing, moves or constants. Lighting retains 27 `f_to_u32` and two `s32_to_f` instructions, whose deletion needs a value/rounding proof.

### Registers and resource ceilings

| Kernel | Workgroup | Shared B | Halfregs | Register thread ceiling | Preamble I | Scratch B | Spills | Fills |
|---|---|---:|---:|---:|---:|---:|---:|---:|
| 97ae4b027aa43c6c | 128x1x1 | 548 | 79 | 1024 | 25 | 0 | 0 | 0 |
| a8260e4297efffb3 | 256x1x1 | 12 | 127 | 832 | 36 | 0 | 0 | 0 |
| bd59552039e0399a | 256x1x1 | 0 | 31 | 1024 | 30 | 0 | 0 | 0 |
| f405bd5f93891b99 | 256x1x1 | 0 | 11 | 1024 | 18 | 0 | 0 | 0 |
| a6e5a25c7bc49152 | 256x1x1 | 0 | 47 | 1024 | 27 | 0 | 0 | 0 |
| d73f8477ba44473b | 256x1x1 | 4096 | 51 | 1024 | 32 | 0 | 0 | 0 |
| e242737cc78e81b6 | 256x1x1 | 2112 | 23 | 1024 | 14 | 0 | 0 | 0 |
| bbfcb4662ad80d8f | 256x1x1 | 2048 | 31 | 1024 | 21 | 0 | 0 | 0 |
| 7d355f20b5c046a1 | 256x1x1 | 2048 | 23 | 1024 | 21 | 0 | 0 | 0 |
| 65e8cdbe219c5f89 | 256x1x1 | 4 | 23 | 1024 | 20 | 0 | 0 | 0 |
| 9dfc864e8b9ae450 | 256x1x1 | 4224 | 19 | 1024 | 19 | 0 | 0 | 0 |
| 806ee7b1bdb1944a | 8x8x1 | 0 | 207 | 512 | 207 | 0 | 0 | 0 |

`agx_performance.c:16` defines the ceilings: <=104 halfregs gives 1024 threads; <=128 gives 832; <=208 gives 512. The traversal fits eight whole 128-thread groups by register arithmetic; a826 fits three 256-thread groups; the trio/radix four; lighting eight 64-thread groups. These are register-only upper bounds, not measured residency or a bank/cache model. K3 changes none of them. A826 must fall from 127 to <=104 to gain a fourth whole group; reaching 112 alone is insufficient. Lighting is one half-register below the 208 tier boundary, but needs <=184 to gain the next tier. No spill-free reduction of that size is demonstrated.

### NIR memory sites before backend optimization

| Kernel | Global loads | Global-constant loads | Shared loads | Shared stores |
|---|---:|---:|---:|---:|
| 97ae4b027aa43c6c | 37 | 24 | 202 | 79 |
| a8260e4297efffb3 | 71 | 39 | 12 | 3 |
| bd59552039e0399a | 13 | 1 | 0 | 0 |
| f405bd5f93891b99 | 3 | 1 | 0 | 0 |
| a6e5a25c7bc49152 | 21 | 1 | 0 | 0 |
| d73f8477ba44473b | 2 | 4 | 12 | 11 |
| e242737cc78e81b6 | 1 | 3 | 21 | 8 |
| bbfcb4662ad80d8f | 5 | 1 | 2 | 2 |
| 7d355f20b5c046a1 | 5 | 1 | 2 | 2 |
| 65e8cdbe219c5f89 | 6 | 1 | 1 | 1 |
| 9dfc864e8b9ae450 | 4 | 5 | 10 | 6 |
| 806ee7b1bdb1944a | 88 | 147 | 0 | 0 |


These counts include constants later hoisted into the preamble and loads later merged. They must not be added to the assembly counts. Lighting has 147 NIR constant loads but only 99 total retained device loads and a 207-instruction preamble; a broad claim that all descriptor/uniform reads remain in its main function is false.

## Interpretation by kernel

- **97ae4b02:** 2.77 ms over five older split launches; the large launch was
  2.04 ms (`EXPERIMENTS.md:67125`). With K2, 70 shared-load sites remain alongside
  57 device loads, six atomics and 61 waits. Loop-dependent node addresses and
  lane divergence remain. K3 removes 42 stores plus changes ALU/moves by +4,
  net -38 instructions. Before the first barrier, stores become 58 -> 16 and
  instructions 434 -> 396. All 21 later store sites remain. The loop/control
  sites and waits are unchanged; constant uniforms are renumbered, so the tail
  is not byte-identical. See selected listing `97ae4b027aa43c6c-0.txt:2854` and
  `-1.txt:2854`, and `change-regions.json`.
- **a8260e42:** older split 0.94 ms. 106 device loads, 90 waits, only 12 shared
  loads and three stores, 127 halfregs. K3 combines two initialization stores;
  the third store, two barriers and loop stay. The 149 control-flow sites and
  load dependencies are stronger remaining suspects than shared capacity.
  Selected baseline starts `a8260e4297efffb3-0.txt:2076`.
- **bd595520 / f405bd5f / a6e5a25c:** 1.51 ms **combined**, not three timings.
  They construct the binary radix hierarchy, link/reset nodes and refit bounds.
  No shared memory in the Windows versions; K3 cannot change them. The first
  and third have data-dependent traversal loops. Their 28/6/25 waits, especially
  load-to-branch and atomic-result dependencies, need CFG/scoreboard analysis;
  a low register count does not remove those dependencies. Listings start at
  `bd59552039e0399a-0.txt:729`, `f405bd5f93891b99-0.txt:120`,
  `a6e5a25c7bc49152-0.txt:552`.
- **Six radix stages:** 48 dispatches over eight passes cost 0.79 ms combined.
  Ba44473b is a four-key/value bit-partition loop with subgroup prefix scans;
  c78e81b6 extracts 4-bit digits and records bucket boundaries;
  2ad80d8f/b5c046a1 are scan stages; 219c5f89 applies group offsets;
  8b9ae450 scatters key/value pairs through shared memory. Sources are retained
  NIR in each `*-0.txt:19`; dispatch-role labels are structural inferences.
  Their per-lane store addresses pass through 16-bit narrowing before late
  vectorization (`agx_compile.c:3659`), and same-lane adjacent addresses are not
  always proven adjacent by the existing vectorizer. K3 deliberately leaves
  this proof alone; all six binaries are identical, including barriers.
- **Lighting 806ee7b1bdb1944a:** 3,580 instructions, 207 halfregs, 108 waits,
  50 texture operations, 99 device loads and heavy reciprocal/trig/float work.
  No shared memory, hence no K3 change. Of 2,977 arithmetic/move instructions,
  only 29 are explicit numerical conversions. CSE/hoisting has already moved
  considerable descriptor work into its preamble. It belongs outside the C4
  fixed-cost budget; no new isolated lighting timing is available here.
  Baseline assembly begins `806ee7b1bdb1944a-0.txt:6691`.

## Apple comparison: same-role radix and hierarchy kernels

The Apple cache is the same 274 supplied listings inventoried by K1. Recounted
candidate listings are in `apple-metrics.json`. Counts include Apple's inline
threadgroup descriptor setup, whereas Windows often uses uniforms/preambles.
Functional-role correspondence is strong for the rows below; argument binding,
variant identity and actual dispatch frequency are not captured. No Apple code
was copied. Counts alone do not prove equivalent work or performance.

| Windows role / kernel | Apple candidate | I Windows / Apple | DL Windows / Apple | SL/SS Apple | W Windows / Apple | A Apple | CF Apple | Named halfreg footprint |
|---|---|---:|---:|---|---:|---:|---:|---:|
| Hierarchy construction bd595520 | f_06d7c0.s | 318 / 247 | 13 / 15 | 5 / 4 | 28 / 14 | 140 | 60 | 42 |
| Link/reset f405bd5f | f_067b80.s | 38 / 53 | 3 / 6 | 4 / 4 | 6 / 4 | 21 | 9 | 30 |
| Bounds refit a6e5a25c | f_06c680.s | 280 / 178 | 21 / 17 | 4 / 4 | 25 / 10 | 90 | 30 | 46 |
| Radix bit partition ba44473b | f_028740.s / f_026000.s | 221 / 179 | 2 / 2 | 12 / 11 | 14 / 2 | 122 | 19 | 46 |
| Four-word scan 2ad80d8f | f_01a740.s | 113 / 91 | 5 / 1 | 2 / 2 | 9 / 1 | 68 | 12 | 24 |
| Scan family b5c046a1 | f_018f80.s | 108 / 86 | 5 / 1 | 2 / 2 | 8 / 1 | 65 | 11 | 22 |

Named register footprint is the highest explicitly addressed register extent,
**not pipeline allocation metadata**, and cannot establish Apple occupancy.
The two scan matches distinguish the variant publishing a group total
(`f_01a740.s:86`) from the one without that output. B5's sign handling differs;
its row is a family comparison, not exact equality. No verified one-to-one
counterpart is established for c78 bucket boundaries, 219 group-offset add,
8b9 scatter, the full traversal, a826 or lighting; do not assign one by size.

Concrete structural checks:

- `f_06d7c0.s:24-90`: adjacent sorted keys, XOR/common-prefix and tie-breaking,
  expansion loop; `f_067b80.s:20-54`: 48-byte nodes, word-10 links, counter clear;
  `f_06c680.s:30-49,159-176`: +/-1e24 bounds, atomic previous-value==1 and parent
  traversal. Apple refit uses **seven** atomic sites versus Windows' one plus
  its barrier sequence. The wait-count gap is not automatically removable.
- `f_028740.s:16-18`: two xyzw reads of keys/values;
  `:40-75`: bit extraction and subgroup prefix scan;
  `:148-168`: barrier-separated scatter/gather of keys then values;
  `:169-178`: loop and two xyzw outputs. This matches ba44473b's role and
  four-item partition shape. `f_026000.s` is a same-sized close variant.
- `f_01a740.s:12-21`: four-value guarded read with tail selection;
  `:25-75`: two-level subgroup/group scan;
  `:84-90`: xyzw scan output plus group total. Windows also has a vector fast
  path but retains four scalar robust-tail alternatives in NIR
  (`bbfcb4662ad80d8f-0.txt:38-90`). Apple's single vector input load is not a
  license to remove D3D bounds semantics. B5 likewise has extra fallback work.

Apple uses ten shuffle-up operations for each two-level scan; Windows' selected
listing uses two hardware `simd` sites. Thus fewer total Apple instructions do
not imply that subgroup lowering is uniformly better there. Its radix barrier
count is eight versus Windows' nine; Apple's descriptor setup can add barriers
in other variants. No synchronization deletion is justified by this difference.

## Ranked next three compiler improvements and time estimates

No GPU run was made. **Measured K3 benefit is unknown.** The following estimates
are explicit prioritization scenarios, not predictions or additive savings.
They use old split milliseconds with current static instruction counts; exact
K2-on per-kernel timings, loop trips and active lanes are missing. The top row
has an implemented work reduction; the other rows are research candidates.

| Rank | Improvement | Conditional ms/frame estimate | Evidence / gate |
|---|---|---|---|
| 1 | Dense 32-bit shared-store vectorization | **0.089**: `2.77*38/1201 + 0.94*1/1108`; practical result may be zero | 42 traversal store issues and one a826 issue removed; occupancy fixed. All savings occur in initialization, so uniform instruction weighting is optimistic. Implemented default off. |
| 2 | Dependency-aware device wait placement across selected CFG boundaries | **0.074** illustration: `1.51*(59-28)/636` if the trio's entire 31-wait Apple gap were removable at average instruction cost | 59 Windows versus 28 Apple waits in the trio; AGX drains every non-exit block. This is **not** proof that any particular wait is redundant; atomic publication differs. Need an async-hazard oracle before a patch. True stall savings could exceed instruction weighting or be zero. |
| 3 | Preserve/prove shared-address adjacency before 16-bit narrowing | **0.0033** for the three directly visible scalar store pairs in c78/8b9: `0.79*3/725`, under equal instruction-weighted cost across the six stages | One pair of constant-distance dynamic stores in c78 and two pairs in 8b9 have narrowed addresses. Current proof conservatively refuses them. Need wrap/range oracle and no cross-barrier growth; other loads could add opportunities, not quantified here. |

The second row uses 28+6+25=59 Windows waits versus 14+4+10=28 Apple waits,
318+38+280=636 Windows instructions. The third row uses all six stage instruction
counts 221+103+113+108+72+108=725 as one equal-frequency pass. This states an allocation assumption, not a confidence interval. Neither scenario establishes
runtime ranking over K3; K3 is first because its legal reduction is demonstrated.

Other candidates lose priority: global/constant vectorization already runs
(`agx_compile.c:2884`); robust scalar-tail fallbacks cannot simply be widened;
K1 shared-load spacing was rejected by EXP2080; none of the C4 kernels has an
explicit conversion to delete; a826 and lighting have no demonstrated route
across the useful occupancy threshold. Hoisting shared loads across writes or
barriers would change semantics. These are not additional implemented switches.

## Production contract and implementation

`native-asahi-latency-schedule.py:632` defines exact-1, per-process cached
`APPLE_AGX_SHARED_STORE_VECTORIZE`, default off. `:648` admits compute shared
stores only when both operands have full write masks and 32-bit values with
32-bit proposed components. `:667` retains the original AGX check for alignment,
no positive hole and <=4 components. `:663` keeps K2 load admission independent.
`:688` enables shared mode for compute only when either option is enabled.

The dense-union proof matters: same-size full masks plus overlap/adjacency and
no gap give a full result mask. NIR `vectorize_stores` preserves last-writer
values on overlaps (`nir_opt_load_store_vectorize.c:1125-1175`), and these options
do not round up the result. No sparse emitter support is presumed. The existing
emitter uses `BITFIELD_MASK` rather than the original write mask
(`agx_compile.c:1045-1057`), which is why this admission stays narrow.

Existing NIR controls remain authoritative: alias checks `:1370`, same-access/
volatile rules `:1499`, size compatibility `:913`, barrier classification
`:1802`, release partitions `:1982-1996`. Stores cannot cross an interfering
load, atomic or barrier just because the environment option is enabled.
Asahi already emits vector local stores; Honeykrisp initializes shared memory
in vec4 units (`hk_shader.c:888-897`) and uses the same backend. No external
implementation was copied into this repository.

D3D TGSM lowering preserves explicit masks and stores
(`drivers/apple-agx/mesa/dxbc/dxbc_to_nir.c:2882`), and `:3131-3138` translates
sync flags into scoped memory barriers. No new Microsoft DDI, capability,
Windows interface or access permission is introduced. UMD/compiler owns the
change; KMD/VidMm retain DMA, residency, scheduling, IRQ and recovery, and
m1n1/Mu retain initialization/power. The two saved launch contracts EXP2108 and
EXP2110 differ in K2's compiler option, not those lower-layer contracts.

Every new injected function is defined before use in the same projected TU.
Host builds use `-Werror=implicit-function-declaration`; Windows architecture
build/link remains an integration gate. This follows the existing compiler
projection's environment-query convention, not the UMD vcxproj CRT wrapper.

## Per-kernel K3 deltas, always with K2=1

| Kernel | Instructions off -> on | Stores off -> on | Code bytes off -> on | GPR / register ceiling |
|---|---:|---:|---:|---|
| 97ae4b027aa43c6c | 1201 -> 1163 | 79 -> 37 | 8588 -> 8276 | 79 / 1024 unchanged |
| a8260e4297efffb3 | 1108 -> 1107 | 3 -> 2 | 7720 -> 7712 | 127 / 832 unchanged |
| bd59552039e0399a | 318 -> 318 | 0 -> 0 | 2202 -> 2202 | 31 / 1024 unchanged |
| f405bd5f93891b99 | 38 -> 38 | 0 -> 0 | 368 -> 368 | 11 / 1024 unchanged |
| a6e5a25c7bc49152 | 280 -> 280 | 0 -> 0 | 1990 -> 1990 | 47 / 1024 unchanged |
| d73f8477ba44473b | 221 -> 221 | 11 -> 11 | 1592 -> 1592 | 51 / 1024 unchanged |
| e242737cc78e81b6 | 103 -> 103 | 8 -> 8 | 848 -> 848 | 23 / 1024 unchanged |
| bbfcb4662ad80d8f | 113 -> 113 | 2 -> 2 | 898 -> 898 | 31 / 1024 unchanged |
| 7d355f20b5c046a1 | 108 -> 108 | 2 -> 2 | 868 -> 868 | 23 / 1024 unchanged |
| 65e8cdbe219c5f89 | 72 -> 72 | 1 -> 1 | 610 -> 610 | 23 / 1024 unchanged |
| 9dfc864e8b9ae450 | 108 -> 108 | 6 -> 6 | 872 -> 872 | 19 / 1024 unchanged |
| 806ee7b1bdb1944a | 3580 -> 3580 | 0 -> 0 | 24808 -> 24808 | 207 / 512 unchanged |

Traversal masks become 11 xyzw + 4 xyz + 1 xy + 21 scalar = **79 dwords**.
No component is dropped. Its crude ALU throughput estimate increases 780 -> 784
from vector assembly moves; the net static instruction saving is 38, not 42.
Preamble instruction counts, device/shared loads, waits, atomics, barriers,
scratch, spills and fills are unchanged for every captured kernel.
The ten unchanged shaders match both selected-main SHA-256 and whole-binary FNV.

## Regression and review evidence

`tests/dxil_agx_cost_fixture.c:621` constructs dense, barrier-split, hole,
overlap, masked, atomic, read-alias, 16-bit and 8-bit initialized-memory cases.
`:678` exposes **actual optimized NIR** addresses, masks and values; the Python
oracle applies them to a byte array with sentinels and compares independently
specified expected contents and barrier snapshots. Retained-binary assertions
check real vector stores, width totals, barrier placement and option behavior.
The existing multi-lane K2 fixture checks uncertain dynamic adjacency and
independent load/store switches. No GPU emulator is claimed.

RED: eight stores remained eight instead of two; barrier split remained eight
instead of three. The negative/semantic controls passed. GREEN: all four new
tests, and all 24 focused tests, pass. Original off/unset/invalid output remains
unchanged. Native projection helper declarations compile with the real fixture.
`store-red.log`, `store-green.log`, `targeted.log` and the 18 synthetic retained
listings preserve the evidence. An initial oracle read expectation was corrected
to compare narrow data by bytes: K2 legally widens adjacent 8/16-bit loads.
The final RED was rerun before production changed.

The unmodified full baseline printed:

```text
ran 844 tests in 335 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

The host used K1/K2's `CCC_OVERRIDE_OPTIONS='s/MacOSX27.0.sdk/MacOSX.sdk/'`
compatibility setting; no tests or known-failure entries were changed for it.
Independent read-only review found no substantive defect and separately passed
eight K2/K3 tests. Full results/known-failure names and commit hashes are in
REPORT.md; the proposed hardware comparison is in DESIGN.md in this handoff.

## WHAT IS STILL UNKNOWN

- On-device K3 correctness, native ARM64/x64/WOW64 compilation, and actual timing.
- Dynamic initialization versus loop cost after K2, lane masks, cache/bank behavior,
  counters, clocks and true resident groups. Static issue savings do not model these.
- Complete Metal pipeline bindings, dispatch frequencies, allocated registers and
  per-pass timings; several supplied stages have no verified counterpart.
- Full SSA-to-SSBO execution correctness: the new oracle checks shared bytes and
  memory observations, not the complete machine program; read multisets do not
  prove output-component placement, and legal forwarded reads are allowed.
- Exhaustive 16-bit address-wrap cases and every possible shader combination;
  the tested uncertain address case proves conservative refusal only.
- Whether the wait or early-address candidates can be implemented safely with
  worthwhile cost reduction. They are analysis targets, not shipped changes.
