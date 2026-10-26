# RC31: GravityMark frame cost and counted indirect replay

Offline source candidate on base `d8a8e212`; saved EXP1961 evidence only.
Implementation: `29780667d1676f5a195483d01778534fcc51e3a2`.
No hardware, guest, builder, package operation, or other-worktree access.
The handoff README assigns EXPERIMENTS/CHANGES bookkeeping to integration.

Evidence root: `/Volumes/pwdev/public_windows/.local/experiments/EXP1961-guest-ram/evidence/EXP1961`.
Reproduction and all per-present rows:
`/Volumes/pwdev/public_windows/.local/handoff/codex/rc31/{analyze.py,frames-a.csv,frames-b.csv,timing.txt}`.
Logs are `gm1961a-umd.log` (2k) and `gm1961b-umd.log` (10k).
The source references below use repository-relative paths; `owner` abbreviates
`drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp` and `ddi` abbreviates
`drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp`.

## Measurement and ranking

QPC is 24 MHz. Hexadecimal Execute/owner duration fields are **microseconds**,
not QPC ticks. `ddi:6600,6719` times replay and final submit; `owner:3936,3977`
times nested bind, draw_vbo, unbind, flush and queue-wait/retire. Never add
nested measurements to their parent. The fence receipt at `owner:3981` counts
submissions, not completion latency or GPU busy time. `measure-render-fence`
is limited to the first 16 submissions (`umd_gpuva_windows.c:2020`) and records
identifiers, not elapsed execution time. These logs cannot isolate GPU busy
from CPU encode, GPU dependency waits, or paging waits inside draw_vbo.

The first counted scene draw reaches present 37/35. Exclude that loading
interval, then select complete intervals ending >70 seconds after the first
UMD receipt: 351/249 intervals. Medians are 217.485/241.858 ms. Independently,
application `*-times.txt` rows with elapsed >70 s have median 217.691/241.715 ms.
The application and UMD clocks lack an exact shared start anchor; these are
separately defined windows. All-present counts (388/290) include loading UI.

Representative frames at the exact UMD medians:

| Component | 2k, present 272, log lines 80689–80953 | 10k, present 122, lines 39106–39354 |
|---|---:|---:|
| Whole interval | 217.485 ms | 241.858 ms |
| Execute replay | 210.536 ms | 233.222 ms |
| draw_vbo, nested in replay | 141.631 ms | 159.770 ms |
| Counted indirect subset, nested in draw_vbo | 115.455 ms | 134.788 ms |
| Other draw_vbo | 26.176 ms | 24.982 ms |
| Owner draw bind + unbind | 0.119 ms | 0.125 ms |
| Replay remainder (dispatch/copy/query/translation/internal waits) | 68.786 ms | 73.327 ms |
| Final submit | 3.554 ms | 3.579 ms |
| Final flush, nested in submit | 3.429 ms | 3.507 ms |
| Queue-wait/retire, nested in submit | 0.090 ms | 0.048 ms |
| Outside Execute (recording/application/present upper envelope) | 3.395 ms | 5.057 ms |
| Commands / outer draws / counted draws | 195 / 13 / 7 | 195 / 13 / 7 |
| Owner dispatches / copies / submission fence increments | 149 / 19 / 41 | 149 / 19 / 41 |
| CopyTextureRegion / staged-upload / texture-copy receipts | 0 / 0 / 0 | 0 / 0 / 0 |
| Checked CPU upload / download | 1.102 / 1.563 MiB | 1.164 / 2.875 MiB |
| Trim calls | 1 | 0 |

**Rank 1: counted indirect replay and its internal synchronization.** Seven
outer draws account for 115.5/134.8 ms, 53.1%/55.7% of these intervals. Primary
Asahi `agx_state.c:4891` sends any counted indirect to `util_draw_indirect`.
`util/u_draw.c:212–265` synchronously maps the count, then the argument buffer,
loads each record on the CPU, and recursively draws direct calls. The trace's
`multi-draw indirect` perf reason confirms that fallback (2456/1784 whole-log
counts). They are sampled deltas, not precisely attributed per-frame calls.
This is the strongest bounded optimization target, not proof that the entire
115 ms is avoidable or pure CPU work.

**Rank 2: remaining replay, 68.8/73.3 ms.** It includes 149 dispatches and 19
copies plus query/translation work, uninstrumented internal submissions and
waits. `owner:3475–3549` binds/launches compute and `ddi:6613–6698` replays
all command types. A 0.462/0.492 ms-per-dispatch division is only an upper
envelope with unrelated work included, not measured dispatch CPU cost.
No heap-walk receipts occur in either log (`ddi:6210,6724`): optimizing that
walk cannot be justified as this workload's measured sink. Per-draw owner
bind/unbind averages only 9.15/9.62 microseconds. DDI rows have exit timestamps,
not paired entry/exit timing, so per-call recording cost is not available.

**Rank 3: other draw_vbo work, 26.2/25.0 ms.** Six ordinary outer draws remain;
4.36/4.16 ms per outer call still includes native work and any blocking waits.
No phase timing distinguishes encoding from GPU waits. Do not suppress resource
hazards or reorder query/statistics updates based on that aggregate.

All 394/300 queue-wait receipts have ordered=1. C83 is already effective:
`owner:3971` skips CPU retirement on successful GPU queue ordering. Changing
its opt-in default (`ddi:3679`) would produce no benefit in this comparison.
The 41 submission increments per frame include internal dependencies, not
41 application Execute calls. `agx_win32_gpuva_batch.c:515–524` drains batches
already observed complete, so the perf reason "Begin drain" is not evidence
of a costly blocking wait. No reason to remove that cleanup is established.

Buffer copies already use Gallium resource_copy_region (`owner:852`), whose
Asahi buffer path uses GPU compute copies (`agx_blit.c:623–654`). The ordinary
frames have no texture-copy calls, so changing CopyTextureRegion is not this
steady-state fix. Loading does have bursts: whole logs each contain 5863
CopyTextureRegion, 2033 staged uploads, 3734 texture-copy receipts. Counts are
not individual durations. CPU staging byte receipts do not identify every
copy's purpose or physical paging traffic.

## Remaining stalls beyond pool capacity

Two complete scene intervals in 2k exceed 10 s: presents 45/387 take
22.085/15.986 s. They contain 37.419 s total draw_vbo, of which 37.301 s is
indirect. Three late 10k stalls total 56.535 s, 55.516 s inside indirect draws,
and have **zero texture-copy receipts**. Paging interrupts the indirect path:

- 2k log lines 21236–21237: 21.509 s after pending trimmed MakeResident.
- 2k lines 110945–110946: 15.594 s after pending trimmed MakeResident.
- 10k lines 43564–43565: 15.277 s after pending trimmed MakeResident.

A silent gap is not exact paging latency, but repeated alignment with
E_PENDING and the source's synchronous paging wait makes residency pressure
the strongest explanation. `umd_gpuva_windows.c:398–453` trims on OOM then
retries; `:736–794` preserves the accepted paging fence and `:797–810` waits
on it. Increasing GPU pool size alone can move pressure to Windows RAM/pagefile,
as the EXP1962 preregistration explicitly notes (`EXPERIMENTS.md:64087`).

Post-loading selected windows drop 43041/29807 MiB of residency **references**
for 36247/24202 MiB requested (1.187x/1.232x), with 653/332 trim calls. Already
implemented RC17 limits terminal-victim overshoot but cannot eliminate it when
only larger eligible heaps remain. Selection is allocation-granular; internal
submissions repeatedly need residency and CPU maps serialize producers. We
have no per-allocation reuse history or physical VidMm copied-byte evidence.
EvictOnlyIfNecessary remains set (`umd_gpuva_windows.c:362`): these numbers
must not be described as physical page-out bytes. The complete supplied 2k
file contains 2228 trim rows, versus the task headline's 2167; 1572 are in the
first scene/loading interval. No old ledger result is edited.

## Fix and contracts

`owner:3599–3608` reads only the optional 32-bit count through the existing
synchronized `AgxD3d12OwnerReadBuffer` (`:814–836`), clamps to MaxCount, and
clears Gallium's count-buffer pointer. `owner:3900–3912` does this before
installing application heap callbacks, skips zero draws and preserves cleanup
on error. Count waiting stays inside the original TimeDrawVbo measurement.
Plain non-counted draws are unchanged; all patch topology retains the old
fallback because Asahi indexed tessellation still needs CPU argument lowering.

Windows primary ABI: `.local/reference/wdk26100/d3d12umddi.h:2455–2474`.
Gallium argument layout: `.local/reference/mesa/src/gallium/include/pipe/p_state.h:780–813`.
Asahi's existing native `util_draw_multi_unroll_indirect` (`agx_state.c:4388`)
preserves the argument BO, stride and draw ID. `agx_indirect_buffer_ptr` (`:4032`)
registers BO reads before GPU consumption. Native indirect IA/query handling
is already present (`:4931`, passthrough GS at `:4251`). The native Windows
wrapper (`native-asahi-batch-lifecycle.py:339–367`) still rebinds heaps after
hazard legalization on recursive draws. GPUVA admission supports indirect
arguments (`agx_win32_gpuva_batch.c:567–587`).

The UMD owner owns this normalization; Asahi owns draw encoding/resource hazards;
VidMm/KMD own residency, paging and GPU completion. No initialization, DMA,
interrupt, power, firmware or recovery ownership changes. m1n1/Mu source is
not needed to alter this CPU normalization; the saved launch contract is
EXP1961 ACTUAL (`EXPERIMENTS.md:64075–64085`). No external implementation body
is copied into Git. The test compiles a pinned MIT Asahi helper in temporary
storage with its copyright notice retained (Mesa `docs/license.rst`).

A review caveat: the new count read precedes native predication evaluation.
D3D12 SetPredication currently refuses (`ddi:7548`, AGX_LIST_STATE); no admitted
D3D12 predication path is changed. If predication is implemented later, avoid
mapping a count for a draw rejected by the predicate.

Expected gain: up to seven argument-buffer CPU map operations per typical
frame are removed (zero counts already avoided argument maps), plus CPU
argument decoding and direct-parameter uploads. The count
read, native recursive per-record draw, query counters and GPU rendering remain.
115/135 ms is the containing cost, **not a promised saving**. A 10 ms actual
saving would move these medians to 207.5/231.9 ms (~4.8%/4.3% higher rate);
30 ms would give 187.5/211.9 ms (~16%/14%). These are sensitivity examples,
not predictions. Long stalls can dominate overall Score/FPS despite a better
median. A fully GPU-consumed count needs separate native lowering and validation.

## Offline verification and next checkpoint

RED: test_owner_count_only_replay fails on original CPU fallback. GREEN:
actual owner callsite/reader/helper and pinned native unroll execute under
ASan/UBSan, covering zero/one/multiple/over-max/UINT_MAX counts, padded stride,
offset/draw ID preservation, NULL map/transfer cleanup, no-count/direct and
patch fallback. Existing heap-hazard fixture executes the new helper for its
direct-only cases with an unreachable read boundary.
Targeted: 46 tests pass. Full prescribed baseline:

```
ran 542 tests in 233 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

Independent read-only review found no blocking issue. Protected invariants
and git diff --check pass. No Windows compile or hardware performance claim.
Known failures and exact commands are in the handoff REPORT.md.

Integration checkpoint: base d8a8e212 plus RC31 commits; pinned native builds,
including WOW64, exact package/manifest hashes and known-good recovery before
installation. Retain expanded 6.88 GB guest, m1n1 075e3980/macho 452ab29f,
`AGX_GUEST_RAM_PROFILE=expanded`, `run_uefi --guest-ram-profile expanded` and
the corresponding m1n1 proxyclient override. Select pool from EXP1962's verdict:
2688 MiB if full pool succeeds without RAM-floor regression; otherwise retain
2016 MiB/Wom1PoolFraction=3. At this branch base EXP1962 has BEFORE only, so
its outcome remains unknown. Keep Wom1GpuFaultInfoRead=1 and GPU_WAIT=1.

WHY THIS HYPOTHESIS: seven counted draws occupy >half of ordinary frame time;
source plus perf receipts proves unnecessary CPU argument decoding; counts-only
normalization has a deterministic RED/GREEN and retains native hazard tracking.
First require counted indirect C14 probes (0,1,multiple,clamped count, indexed
and non-indexed, query/statistics, output content), then 2k followed by 10k,
1280x720 TAA. Compare Score/FPS and post-loading median/tail to EXP1961
352/1.8 and 256/1.4, also record replay/indirect time, CPU downloads, trim
references/bytes and fence increments. Keep capture overhead consistent.
Collect evidence before ordinary GPU-visible exact-package rollback; stop on
content, query, device-removal or desktop failure. This task runs none of it.

WHAT IS STILL UNKNOWN: actual CPU/GPU/paging time split, per-resource physical
paging/reuse, saved byte traffic attributable specifically to argument maps,
Windows compile and native indirect shader/query correctness on this workload,
EXP1962 pool verdict, and net performance gain. New perf data must decide
whether remaining native indirect lowering or compute/query replay deserves
the next optimization; no speculative caps, barrier or trim-policy change here.
