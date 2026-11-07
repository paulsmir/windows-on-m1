# Q18: EXP2105 paging waits are censored, not the dominant measured sink

Offline base `65bac4495aa63e79258968ff8f9190f59f173531`, branch
`codex/q18-paging-fence`. No hardware, guest, builder, network, package or
other-worktree activity. Handoff `DESIGN.md` records the implementation plan;
`REPORT.md` records commits, exact verification and remaining unknowns.

## Measurement method and evidence

Inputs are `gm2105a-umd.log` (HD, PID 11244) and `gm2105b-umd.log` (FHD, PID
1964) in the shared `.local/experiments/EXP2105-gm-minimal/evidence/EXP2105`.
SHA-256 respectively:

- `14454cc4bcf883009e86bddee3ea3a300f16e4909eff78ddada843d3bac7268a`
- `1da83ac519a310b66249ed559a669fcf229af10bb7a13f12fed10b1b1e165408`

Run `python3 tools/agx_paging_wait_analyze.py <log>` separately on each input.
The tool streams lines and keeps parsed records only; it reuses the established
GPU timestamp decoder. Default steady-state selection drops the first 30% of
Present records, then uses all remaining complete Present-to-Present intervals:
3943 HD frames and 3234 FHD frames. Complete replay/submit windows wholly inside
that selection contribute 3944/3235 Executes. CPU aggregates are assigned to
the interval containing submit-end. QPC is 24 MHz. Results and exact first/last
QPCs are saved as `gm2105{a,b}-analysis.json` in the task handoff.

`measure-d3d12-execute` supplies serial, replay and submit microseconds;
same-PID/thread `measure-d3d12-replay-window` supplies actual QPC endpoints.
`measure-d3d12-wait` is joined by PID/thread/serial. Completions join by sequence
and their embedded KMD Submit/Complete QPCs, never print time. Example first
retained Execute records: HD log:601777 serial 1736, replay start 3280780948;
FHD log:490943 serial 1418, replay start 8566444364. JSON preserves the complete
first three windows and wait records for audit.

Crucial limitation: `render-admission/umd/include/umd_wait_profile.h:76-103`
emits only **four exclusive buckets** plus their omitted aggregate. Of the
selected Executes, 3943/3944 HD and 3234/3235 FHD are truncated. Missing keys do not
mean zero. The separate wait-path receipt is inclusive and cannot be added to
these times. For a missing key its time is at most the fourth retained time;
for a set of missing keys, also cap by the shared omitted total. Bounds are
at integer-microsecond receipt precision (each emitted duration truncates
less than 1 us); they are not independent additive budgets.

## CPU cost per steady-state frame

All numbers below are milliseconds, mean unless a median is shown. The five
largest **named retained** wait buckets are ranked below. Unknown omitted time
is larger than some named buckets, so this is not a complete per-site ranking.

| CPU quantity / retained bucket | HD mean | FHD mean | HD median | FHD median |
|---|---:|---:|---:|---:|
| Frame wall time | 28.848 | 35.851 | 28.534 | 34.632 |
| Replay (inclusive) | 21.609 | 27.749 | 20.948 | 27.140 |
| Final submit (inclusive) | 3.008 | 3.659 | 2.547 | 3.079 |
| 1. 0x403 draw RenderFence | 9.682 | 10.657 | 9.586 | 11.183 |
| 2. 0x303 query-resolve RenderFence | 5.399 | 10.328 | 4.298 | 9.301 |
| 3. 0x603 submit RenderFence | 2.050 | 2.691 | 1.727 | 2.166 |
| 4. 0x30f query-resolve Flush, exclusive | 1.538 | 1.790 | 1.495 | 1.689 |
| 5. 0x40f draw Flush, exclusive | 0.147 | 0.062 | 0 | 0 |
| All exclusive profiled time, including omitted | 22.442 | 29.226 | 22.175 | 28.447 |
| Omitted exclusive total | 3.376 | 3.568 | 3.172 | 3.409 |
| 0x402 draw PagingFence, retained only | 0.00338 | 0.00448 | 0 | 0 |
| 0x002 other PagingFence, retained only | 0.00741 | 0.01137 | 0 | 0 |
| 0x41c draw PoolMapEscape, retained only | 0 | 0 | 0 | 0 |

All remaining named buckets and counts are in the JSON: the largest next is
draw NativeReadyEscape, 0.129/0.0187 ms. Submit PagingFence and PoolMapEscape
are never retained. That fact cannot be converted into zero true cost.

| Target's possible mean CPU time | HD lower..upper | FHD lower..upper |
|---|---:|---:|
| Draw PagingFence (0x402) | 0.00338..1.518 | 0.00448..1.708 |
| Draw PoolMapEscape (0x41c) | 0..1.516 | 0..1.708 |
| PagingFence, all phases | 0.01080..3.387 | 0.01585..3.584 |
| PoolMapEscape, all phases | 0..3.376 | 0..3.568 |

The supplied traces therefore do **not** identify an exact recurring paging
cost or prove that every observed paging callback was already complete. The
probe's 15.96/11.15 ms delays are not a measured per-frame GravityMark cost.
HD has one retained >=10 ms paging-wait diagnostic (log:1966967, 19.519 ms);
FHD has three (log:1610347,1610604,1610777: 10.763/41.433/10.438 ms). All have
site 1, the MapGpuVirtualAddress bind wait (`umd_gpuva_windows.c:933`). No
`measure-trim`/`measure-make-resident-trimmed` record occurs in either input.
This does not exclude OS placement activity; it supplies no evidence of a
recurring UMD trim/MakeResident thrash in these frames.

## GPU timeline and what the waits can explain

CPU waits overlap GPU execution; adding them to GPU time double-counts work.
The established firmware-end estimator (`tools/agx_gpu_frame_breakdown.py:159`)
attributes spans from max(KMD submit, previous own completion) to a locally
mapped firmware end, including launch overhead. Stale render Start timestamps
are deliberately ignored. These are estimates, not isolated engine timings.

Top five frame-time sinks by mean span/interval (ms):

| Rank | HD | FHD |
|---|---|---|
| 1 | Four standalone compute spans: **14.548** | Compute spans: **17.701** |
| 2 | Other >=1 ms render spans: **6.660** | Main render span: **9.137** |
| 3 | Main render span: **5.714** | Other >=1 ms render spans: **7.949** |
| 4 | Small render spans: **0.733** | Own completion-to-next-submit gaps: **0.370** |
| 5 | Own completion-to-next-submit gaps: **0.599** | Completion detection estimate: **0.321** |

Compute exact firmware execution accounts for 12.649/15.184 ms; its launch
portion is 1.899/2.518 ms. Main 3D is 4.732/8.008 ms. Other big renders include
3.216/3.260 ms after prelude through TA end and 1.791/2.832 ms in 3D.
Do not add these subcomponents again. Frame-bin boundary effects and foreign
jobs prevent exact equality between sums of per-job estimates and frame wall
time. The old analyzer's residual idle estimate is 0.768/0.768 ms, distinct
from the directly measured KMD gaps below.

| Direct QPC interval / overlap bound | HD mean ms | FHD mean ms |
|---|---:|---:|
| Own KMD Complete -> next own Submit | 0.59855 | 0.37045 |
| Portion inside recorded replay/submit windows | 0.36374 | 0.21750 |
| Gap portion with a skipped adapter completion sequence | 0.45739 | 0.31477 |
| All profiled waits possibly in those gaps, lower..upper | 0.05961..0.36084 | 0.02307..0.21750 |
| Paging waits possibly in those windows' gaps, upper | 0.25802 | 0.17668 |
| Pool-map waits possibly in those windows' gaps, upper | 0.25633 | 0.17009 |

Median own gap is only 0.12154/0.11771 ms. The table's mean upper paging
overlap is 43.1%/47.7% of own gaps, **not measured causal attribution**. There
is no positive per-site attribution lower bound. Foreign sequence gaps may
contain DWM GPU work, so even the whole interval is not proven global idle.
Wait receipts lack interval timestamps; no exact causal join can recover them.
The overlap bounds are aggregated by each Execute's submit-end frame, whereas
actual gap totals are clipped at Present boundaries. The table uses aggregate
means, which preserve those totals. JSON overlap median/p95 values describe
submit-binned aggregates, not exact per-Present causal overlap when an Execute
crosses a Present; they must not be interpreted as such.

For a window of length L with G ticks intersecting gaps and W exclusive waits,
the possible overlap is at most min(W,G). A lower bound uses only retained
nonzero-phase waits: max(0,Wknown-(L-G)). Other-phase and omitted waits cannot
support the lower bound because `d3d12/agx_d3d12_ddi.cpp:7195-7206` starts
profiling before OwnerLock/StageUploads and the replay-window start. The tool
rejects invalid profiler records, overlapping Execute windows and incomplete
joins instead of silently reporting a precise answer.

## Source contract and cause

Driver paths below start at `drivers/apple-agx/`.

| File:line | Contract / consequence |
|---|---|
| `render-admission/umd/src/umd_runtime_device.c:445,461,486` | Creates the per-device paging queue; stores its object and read-only CPU fence address. Q18 reads its switch once here. |
| `render-admission/umd/src/umd_gpuva_windows.c:505-532` | Fresh canonical maps first take a persistent residency reference; nonzero S_OK and E_PENDING paging fences must complete. Resident alone cannot excuse a failed wait. |
| `umd_gpuva_windows.c:541-585` | MapGpuVirtualAddress names the paging queue and returns a second paging dependency; the bind waits it. |
| `umd_gpuva_windows.c:709,728-837` | Submit retirement releases copy holds, not residency. Already-resident slots skip MakeResident. Only missing references are requested; accepted-but-uncertain work remains terminal/held. |
| `umd_gpuva_windows.c:307-449` | Budget refusal trims eligible idle slots under Transition and retries; successful eviction clears Resident. No change in Q18. |
| `render-admission/umd/include/umd_residency_trim.h:37-55` | Canonical maps, source holds and in-flight submission holds exclude trim; private staging lifetime is separate. |
| `umd_gpuva_windows.c:1993-2035,2230-2237` | CPU-map restoration waits paging under the map Transition reservation. |
| `umd_gpuva_windows.c:2243-2290` | Lock may rotate placement; touch/publication wait then live pool proof authenticate the new CPU pointer. A cached old pool result cannot replace this. |
| `mesa/winsys/agx_win32_gpuva.c:280-310` | Holds acquired, MakeResident accepted, paging complete, then validation/submission. Failed paging retains uncertain holds. |
| `umd_gpuva_windows.c:2690-2718` | CPU transfer, native readiness and preflight precede SubmitCommandCb. A GPU-only dependency cannot protect these CPU consumers. |
| `umd_gpuva_windows.c:852-896` | Common paging/render CPU wait leaf; the existing render-only completion shortcut now has a separate opt-in paging proof. |

The outer D3D12 `pfnMakeResident`/`pfnEvict` implementations in
`d3d12/agx_d3d12_ddi.cpp:1730-1740` only record object types and return success
(MakeResident sets WaitMask=0). Their `measure-d3d12-make-resident` receipts
are not counts of the internal VidMm `pfnMakeResidentCb` operations above.
Actual native allocation/BO use follows the internal residency/holds path.
The old C2a comment claiming lifetime-long residency predates idle-slot trim;
the executable trim/restore code defines current behavior. Q18 does not change
the application residency DDIs or claim to audit their complete semantics.

Q11/Q16 supply evidence of expensive fresh preparation, not permission to
delete dependencies. Steady-state paging can come from new/rebound internal BOs,
restoring trimmed allocations, CPU-map restoration and placement repair. This
code already avoids per-submit eviction and duplicate MakeResident for held
resident slots. The EXP2105 records do not resolve each allocation or prove
which source dominates the omitted paging time.

## Windows and Asahi primary sources

Inspected local pinned SDK26100 `shared/d3dukmdt.h:1475-1488`: MakeResident
returns a fence on the named paging queue's monitored object and a trim budget;
`:1596-1610`: MapGpuVirtualAddress returns a paging fence. Local
`um/d3dumddi.h:3922-3929` explicitly defines the paging fence's read-only CPU
mapping; `:3838-3850` defines a synchronous CPU wait; `:3859-3876` defines a
context GPU wait. `:3999-4043` is this driver's **D3DDDICB_SUBMITCOMMAND**:
there is no PagingFenceValue field to fill. Do not confuse it with a different
submission/HW-queue interface. A queued wait on the actual owner context is a
possible GPU-ordering mechanism, but cannot authorize current pre-submit CPU
copies or placement queries.

Microsoft references (URLs supplied for integration review; no network used):
[MakeResident callback](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/nc-d3dumddi-pfnd3dddi_makeresidentcb),
[MakeResident structure](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dukmdt/ns-d3dukmdt-d3dddi_makeresident),
[CreatePagingQueue](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dumddi/ns-d3dumddi-d3dddicb_createpagingqueue),
[residency overview](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/residency-overview).
The local Microsoft DirectX-Graphics-Samples residency sample
`dxgs-68907e31-more/Samples/Desktop/D3D12Residency/src/d3dx12Residency.h:1056-1077,1262`
orders EnqueueMakeResident via a queue fence before command execution. It does
not permit CPU use of pending allocations.

Primary Mesa `src/gallium/drivers/asahi/agx_pipe.c:651-675` excludes shared
resources from shadow renaming; `agx_batch.c:278-300` polls completed batch
sync objects before cleanup. Windows uses VidMm-owned placement and paging
fences, not those DRM interfaces. No external code copied. UMD owns references
and CPU waits; VidMm owns residency/placement; KMD owns mappings/native proof,
DMA execution/interrupts/recovery. m1n1, Mu, ACPI, power and initialization
remain unchanged and are not causal to this completed-fence optimization;
live measurement is prohibited by the task. EXP2105 is the saved reference
launch contract, with only Wom1GpuFaultInfoRead=1.

## Implemented behavior and expected performance

Set **APPLE_AGX_PAGING_WAIT_COMPLETED=1** before process/device creation.
Missing, zero or any other string leaves it off. `umd_internal.h:221` holds
the device-local setting. On x64/ARM64 only, an aligned, non-null, read-only
paging mapping is read with ReadAcquire64. The exact full-width target must be
covered, the device must not be terminal, and all-ones is not completion.
Otherwise the original callback runs and its error propagates. No polling,
allocation, lock or environment lookup is added to each wait. Paging proof
never updates RenderCompletedFence. x86 intentionally retains callbacks;
no potentially torn plain 64-bit load or RMW on the read-only page is used.

**This removes zero pending waits.** It implements the smallest proven-safe
avoidable callback in the requested paging scope, not a demonstrated top
overall frame-time fix. The much larger render-fence waits protect pending
GPU data/lifetime and already have their own completed-fence fast path.
Pool-map caching across Lock would need a new invalidation/current-placement
proof, unsupported by these receipts. No such cache or residency weakening
is introduced.

Expected steady-state result: near unchanged FPS, with any gain below 1% in
the measured pre-submit-gap model. Removing *all retained paging time* from
the score-derived frame periods would change HD 33.6 -> **33.612** and FHD
27.4 -> **27.412** FPS; this is a scenario, not a forecast or upper bound on
hidden time. An optimistic removal of the entire possible paging overlap
0.258/0.177 ms gives **33.894 / 27.533 FPS**. Those are model ceilings for the
recorded windows only; outside-window staging, startup and callback hit rates
are unknown. Zero gain is entirely consistent with the evidence. No claim
approaches macOS's HD 9739/FHD 7814 scores from this change.

## One integration experiment, not executed

WHY THIS HYPOTHESIS:
1. The common leaf currently always calls Windows for paging even when its
   own read-only mapping covers the exact target; real-function RED reproduces
   that redundant callback and GREEN elides only the completed case.
2. EXP2105 bounds recurring paging below the dominant render waits; most own-gap
   duration has skipped adapter sequences and may contain foreign work. This favors a small
   conservative callback experiment over speculative asynchronous residency.
3. The WDK documents the returned read-only paging completion object; pending
   CPU consumers still require completion, so a completion proof preserves
   the existing ordering and recovery contracts.

Integration owner first performs native MSVC/WDK /W4 /WX builds and linking
for x86/x64/ARM64, pins source/package/manifest/firmware and known-good recovery
hashes, and records the BEFORE entry. Run one EXP2105-style GravityMark sequence:
D3D12, HD then FHD, 100k asteroids, temporal=0, **only
Wom1GpuFaultInfoRead=1**, same trace settings, new process environment variable
APPLE_AGX_PAGING_WAIT_COMPLETED=1 as the single variable. Leave all depth-two,
completed-owner-wait and other experiment options at their baseline settings.
Compare to EXP2105; an absent/0 switch restores the old path.

Capture scores/FPS, full UMD/stdout, process/window QPC, completion/status and
fault/reset/WER evidence; run this analyzer on the same fixed fraction. Require
normal completion and unchanged visible output/no corruption, no reset,
device removal or unexplained fence/placement failure. Reduced top-four paging
without total/window reduction is not a performance proof; if the bucket is
still censored, say INCONCLUSIVE, not zero. Small score changes alone cannot
distinguish noise from a callback saving. No new receipt-only hardware series
is proposed. Native x86 is build/regression coverage, not an expected speedup.

On failure collect logs/receipts/dump first; integration performs exact-package
cleanup through its preregistered normal GPU-visible rec2105h-equivalent
recovery, preserving the immutable emergency-hidden fallback. No boot, guest
contact, install, package build or recovery action was performed here.

## WHAT IS STILL UNKNOWN

Exact omitted paging/pool-map cost and allocation identities; individual wait
intervals or completed-vs-pending callback hit rate; actual global GPU idle in
foreign-sequence gaps; native MSVC /W4 /WX/linking; on-device mapping visibility,
output correctness, first-frame savings and steady-state performance. Offline
tests prove the branch conditions and ABI syntax, not these hardware outcomes.
The existing top-four trace cannot answer exact causal savings. Integration
owns EXPERIMENTS.md and CHANGES.csv updates under the task README.
