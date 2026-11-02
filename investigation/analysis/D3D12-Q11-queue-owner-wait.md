# Q11 owner queue ordering — offline design

## Decision and implementation plan

The requested unconditional pending-owner wait removal is not justified by the
supplied evidence. Keep pending completion bridges. Implement only a default-off
`APPLE_AGX_D3D12_QUEUE_WAIT_COMPLETED=1` fast path: the owner's atomic, monotonically
recorded completed fence must cover the exact accepted fence after flush. Zero
and device-removal/all-ones values are not completion proofs. Use the existing
RenderCompletedFence cache, not a potentially torn x86 load or an interlocked
write into the read-only monitored-fence mapping. A stale cache only misses an
optimization. The existing GPU_WAIT option still controls use of the bridge.

Plan (native inline execution under the user's end-to-end authorization):
1. Compile the real submit body in the existing host test. RED: known-complete
   work still creates a queue wait. Cover default off, pending, fault, callback
   errors, zero and full-width values, explicit CPU consumers and non-GPUVA.
2. Add the completion predicate, sample the new switch at owner creation, and
   skip only the satisfied outgoing edge. Preserve all residency, download,
   retirement, signal, context and failure paths. Record skipped vs queued.
3. Document source/trace evidence, Windows/Asahi contracts, safe larger design,
   limitations and the integration owner's hardware checkpoint. Run targeted
   tests and the unmodified baseline runner; review and make small commits.

This narrower change cannot make an unfinished C predecessor's R successor
arrive earlier by removing a pending wait. It must not be described as the
requested full overlap solution. The distinction follows from the contracts
below, rather than from a missing implementation effort.

## Source and observed contract

Paths below are relative to the repository, or to `.local/reference` where
explicitly marked. Current line numbers include the Q11 change.

| Boundary | Primary implementation / evidence | Consequence |
|---|---|---|
| Application queue Q | `drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp:3783-3815` creates a context through the core callback with `hRTCommandQueue` | Runtime Signal/Wait belong to Q. |
| Device owner O | `drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp:779-821` creates one device-level context through the KT device callback | O differs from Q; there is no render-versus-compute owner context split here. |
| Render and compute | `drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c:1144-1152,1265-1273` both call AgxWin32GpuvaSubmit and update the same NewestFence | Batch kind changes the envelope, not the context or completion object. |
| Accepted work | `drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c:2701-2705,2716-2717` broadcasts every native submission on device->KernelContext | Both C and R submit on O. |
| Completion signal | `umd_gpuva_windows.c:2317-2329,2746-2757` signals RenderSyncObject on O after successful SubmitCommandCb | There is no distinct compute sync object to signal earlier or later. |
| Download completion | `umd_gpuva_windows.c:2401-2435` returns internal when no download is due; otherwise waits, downloads and signals internal+1 | Use the returned fence, never just a command ordinal or the pre-download internal value. This branch depends on written storage, not C/R kind. |
| Monitored object | `drivers/apple-agx/render-admission/umd/src/umd_runtime_device.c:483-514` creates D3DDDI_MONITORED_FENCE, initial zero, CPU mapping | This is a monitored-fence synchronization object; those terms are not competing object types. |
| Outgoing bridge | `agx_d3d12_ddi.cpp:7111-7116`; `agx_d3d12_windows.cpp:5082-5114`; `drivers/apple-agx/d3d12/agx_d3d12_queue_sync.h:7-17` | After flush, Q waits on O's exact accepted value. A wait on Q is not a wait on O. |
| Replay serialization | `agx_d3d12_ddi.cpp:7175-7204` | One owner lock covers staging/replay/submit, even across multiple app queues. CPU return alone does not prove kernel arrival. |
| Completion proof | `umd_gpuva_windows.c:840-882`; `umd_internal.h:223-225` | Cache is an atomic unsigned maximum recorded only after a successful completion wait or valid 64-bit acquire observation. A stale value can only retain an unnecessary wait. |
| CPU/present consumers | `agx_d3d12_windows.cpp:2481-2502,5149-5157` | Present flush and explicit SubmitAndWait are independent of Q11 and keep their existing contracts. |
| Private render setup | `drivers/apple-agx/render-admission/src/gpuva_g3_windows.c:2527-2570` | Uncached acquire/prepare waits for process quiescence; cached scene reuse has separate growth constraints. This can prevent arrival without a VidSch queue wait. |

UMD owns replay, private completion objects and resource tracking. VidSch owns
context waits/signals and scheduling; KMD owns validated DMA, typed publication,
interrupt completion and recovery; firmware executes typed barriers. The Asahi
reference `asahi-linux-asahi/drivers/gpu/drm/asahi/queue/mod.rs:787-820` selects
vertex or compute queues and inserts a barrier on the predecessor's firmware
stamp/value/slot. Windows policy is `render_job_window.c:33-46,74-82`: C→R selects
TA-on-compute and R→C selects compute-on-3D. This is ordering between admitted
jobs; it does not make an application Signal on another context depend on them.

No initialization, power, IRQ, DMA map, ADT, m1n1, Mu, ACPI, caps, signer or
recovery contract changes. Live state inspection is prohibited by this offline
task. Those layers are not causal to the demonstrated CPU replay interval;
no new register or inherited-machine-state assumptions are made. Source was
inspected, not copied. q6/q9 DESIGN.md and only EXP2093–2095 ledger entries were
read; the ledger is unchanged under the handoff README's integration ownership.

## Trace correction: no evidence of a C-specific completion bridge

`tool-q3cr-umd.log` in this handoff contains the measured pair (stdout
`tool-q3cr-std.txt:34-35` identifies its QPC interval). At 24 MHz:

| Evidence | QPC / duration | Meaning |
|---|---:|---|
| C owner context / returned fence, log:550 | context 0x1fdf1142a00, fence 25 | Same context used by R. |
| C outgoing bridge, log:551 | 1551673078, ordered=1 | Queued after C submission; does not prove O is blocked. |
| C Execute, log:560,563 | replay 26.447 ms; submit 0.334 ms | The “~50 us per Execute” interpretation does not describe this measured pair. |
| R replay window, log:586 | 1551674957 .. 1552483230 | 33.678 ms before the final flush/bridge. |
| C completion record, log:580 | start 1551672571, end 1551833053 | The reported completion interval is 6.687 ms; calibration in stdout was 5.964 ms. This is a CPU completion record, not a claim about fresh render hardware timestamps. |
| R first shown native batch, log:572 | 1552484910 | Already ~27.16 ms after C's reported completion. |
| R context / returned fence, log:574 | context 0x1fdf1142a00, fence 27 | Same context and timeline, no compute-versus-render fence asymmetry. |
| R wait attribution, log:588 | 0x402: 15.963 ms; 0x41c: 10.819 ms | Draw-phase PagingFence and PoolMapEscape, respectively. |
| R Execute, log:584 | replay 33.678 ms; submit 0.310 ms | Missing successor is compatible with CPU preparation delaying it. |

Wait IDs decode from `drivers/apple-agx/mesa/winsys/agx_win32_wait.h:5-28` and
`drivers/apple-agx/render-admission/umd/include/umd_wait_profile.h:89-95`.
The two wait buckets account for 26.782 ms, about 80% of R replay. The map-time
record at log:569 independently reports 15.964 ms. Do not label the 10.819 ms
PoolMapEscape as a scene-acquire wait; those are different IDs.

Thus EXP2095 reason 66 proves *no pending successor*, but does not prove the
outgoing wait caused it. This design is a proposed correction to the inference
at `investigation/EXPERIMENTS.md:67496-67503`, not an edit to historical evidence.
The supplied `tool-q3rcr-umd.log:671` ends at a mapping record just after the
measured lists were recorded; it does not contain the measured three-Execute
sequence. Its 14 earlier waits cannot establish that sequence's timing. The
ledger's R→C result remains evidence, but its contrast with C→R does not imply
separate completion semantics. The strongest remaining causal target is render
preparation/paging; the exact mapping/allocation identity is still unknown.

## Windows contract and the larger correct alternative

Pinned WDK 26100 sources inspected:
- `d3d12umddi.h:2562-2565`: the core create-context callback is associated with
  HRTCOMMANDQUEUE; `:2712-2737`: queue fence-operation and Execute DDIs.
- `d3dumddi.h:3839-3850`: a CPU fence wait with a null event blocks the call;
  `:3859-3876`: a GPU wait names the *waiting context*, sync objects and values;
  `:3895-3920`: GPU2 signal names broadcast contexts and monitored values.
- Cached project Windows contract analysis:
  `investigation/analysis/D3D12-C3-queue-fences.md:16-34` and
  `investigation/analysis/D3D12-C83-cross-context-wait.md:5-37`.

Microsoft reference pages for integration review (no network was used here):
[ExecuteCommandLists](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists),
[Context monitoring](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/context-monitoring),
[Multi-engine synchronization](https://learn.microsoft.com/en-us/windows/win32/direct3d12/user-mode-heap-synchronization).
Separate Execute calls on one D3D12 queue require earlier work to finish before
later work executes. This constrains GPU execution, not early admission to the
KMD or CPU return. Multiple lists in one Execute may be merged subject to their
dependencies. Signal must follow prior queue work; Wait must gate subsequent
work. CPU readback and Present must not observe unfinished writes.

A complete queue-owned design can admit the successor early without an O→Q
wait: register each execution context with its actual HRTCOMMANDQUEUE, submit
both C and R on that context, and let typed KMD barriers order their execution.
The runtime's Signal/Wait then belongs to the same stream as the work. Each
queue needs its own completion timeline, pending batches, deferred replay and
resource references; device allocations may remain shared with explicit
cross-queue dependencies and independent lifetime holds. Do not reuse one
monotonic owner completion fence across unordered queues: a later independent
signal can falsely cover an earlier unfinished job.

A correct implementation must cover these boundaries together:
1. Enqueue the successor without CPU-retiring the predecessor. KMD must either
   admit the typed dependency (TA waits for compute; compute waits for 3D) or
   serialize it; it must not start dependent reads early. Preserve fault-suffix
   abort, original WDDM fence order and preemption/TDR rules from q6.
2. Keep application Signal in the actual execution context after all accepted
   work and any required download. If work stays on O, keep the O→Q bridge at
   every possible externally visible boundary; the current runtime can Signal
   between any two Execute calls without invoking a UMD SignalFence callback.
   A UMD cannot silently defer this bridge until an unobservable later Signal.
3. Honor incoming cross-queue Wait before any GPU execution or CPU operation
   that consumes GPU-written data. The existing singleton replay does not do
   this (C83 explicitly documents the limitation). A CPU wait in Execute is not
   sufficient: `B.Wait(F,1); B.Execute(consumer); A.Execute(producer);
   A.Signal(F,1)` must enqueue from one thread without deadlocking. Deferred
   replay or fully GPU-native consuming commands, with retained lifetimes and
   a supported incoming-dependency interface, are required.
4. Preserve per-submission residency/SubmissionHolds, paging waits on the actual
   submission context, canonical VA lifetime, CPU mapping hazards, sparse
   remaps and Present's written-primary/completion ordering. Changing only
   Runtime.KernelContext during Execute leaves setup/transfer/replay outside
   that ownership and is not a correct migration.

This larger design is **not implemented or proven by this patch**. In particular
host typed-barrier tests alone cannot prove Windows callback association or
incoming Wait semantics. No safe pending-wait deletion exists in the current
separate-owner design without replacing those contracts. The narrower completed
edge is the only elision implemented here; it neither fixes nor worsens C83's
existing incoming-Wait limitation.

Cases that retain VidSch waits: every pending/unknown owner completion at the
separate application context, every external/cross-queue dependency, paging
requirements, and present ordering when the producer remains on another
context. Unsupported/failed callbacks retain CPU fallback. Zero-fence, fault,
all-ones cache and non-GPUVA paths cannot use the completion shortcut. A KMD
barrier does not replace an external context's synchronization contract.

## Implemented switch and estimate

Set `APPLE_AGX_D3D12_GPU_WAIT=1` as before and optionally
`APPLE_AGX_D3D12_QUEUE_WAIT_COMPLETED=1` before process creation. Only exact `1`
enables the new flag, sampled once per owner. Default remains off, including
x86. The code builds the same path on x86/x64/ARM64 with an atomic load of the
existing writable completion cache. It does not poll, CPU-wait, mutate a fence,
or retire resource holds merely because it omitted a callback.

`measure-d3d12-queue-completed {skipped, accepted_lo, accepted_hi,
completed_lo, completed_hi}` appears only with the switch enabled. Existing
`measure-d3d12-queue-wait` ordered=1 now means the outgoing edge is satisfied,
either by the queued bridge or by proven completion; join the new receipt to
distinguish them. Snapshot lag can miss completed work and is intentionally
conservative. The cache can remain behind the newest fence after asynchronous
submissions; there is no promised steady-state saving.

Counts from supplied traces: Q3CR has 11 Execute/queue-wait pairs (2 within its
measured C→R sequence); Q3RCR has 14 before its truncated measured sequence.
For an assumed one-job-per-Execute GravityMark-like frame C R C R C R C R,
there are eight outgoing bridge opportunities, four C→R and three intra-frame
R→C adjacencies (four R→C in a repeating stream). These traces do not contain a
GravityMark frame or completion-cache snapshots. This patch removes **zero
pending waits**, and between zero and eight already-satisfied waits in that
hypothetical frame; no nonzero removal count or millisecond saving is measured.
A full queue-owned design could eliminate all eight private O→Q bridges while
retaining explicit application/paging/external waits. The task's 0.5–0.6 ms gaps
must not be multiplied into a performance claim for the present patch.

## Hardware checkpoint for the integration owner — not executed

WHY THIS HYPOTHESIS:
1. Both kinds share O and its completion object; deleting a pending outgoing
   bridge would permit premature app Signal, whereas an already-complete value
   makes the edge redundant by construction.
2. The real submit-body regression fails on the original callback-only path
   and passes with exact completed-value elision; pending/fault/default paths
   retain ordering.
3. The Q3CR render replay outlasts the compute and spends 26.782 ms in paging
   and pool-map waits. Any claim of improved C→R arrival must measure replay
   and kernel admission, not just absence of an outgoing wait packet.

Before hardware: integration review, actual WDK/MSVC x86/x64/ARM64 syncheck of
`d3d12\\agx_d3d12_queue_sync.h` and `mesa\\winsys\\agx_d3d12_windows.cpp`, exact
candidate/package/source/manifest/firmware/recovery hashes and preregistration.
No package or builder was contacted here. Preserve EXP2095 prerequisites:
Wom1ManagerSlots, JobBuildAhead, JobDepth2, JobDepth2Caps, RcReceipt,
NotifierFlight, RcDepth2, Depth2General, GpuFaultInfoRead, all as recorded there;
set `Wom1Depth2TaOnCompute=1`, leave TaOn3d off after EXP2094's failure.

Compare new switch off/on with GPU_WAIT=1 in the same package; run `only Q3CR`
then `only Q3RCR`, collecting untruncated UMD/stdout, exact PID/QPC windows,
Wom1RcProgress/Decisions/JobWindow/JobTiming971, resets, G4 failures and recovery.
Completion-shortcut correctness PASS requires an actually skipped edge backed
by completed>=accepted, exact outputs, correct app fence ordering and no reset.
If none is skipped, the optimization is unexercised.

The requested **overlap PASS** remains stricter: typed C→R published-progress
with TA-on-compute while C is live, exact successful original fences, exact
output bytes, no reset/removal, and retained R→C behavior. Correct output with
reason66 and no typed interval is INCONCLUSIVE, never overlap PASS. Do not
expect this patch to remove a pending edge or cure render setup latency.
For the next overlap experiment, first make the exact successor's allocation,
paging and scene setup reusable before C begins (or fix the identified mapping
cost in its owning layer); keep the valid outgoing bridge while testing early
arrival. This is a proposed separate behavior change, not a reason for another
receipt-only rerun. Recovery stays the integration owner's preregistered
dump-first exact cleanup to ordinary GPU-visible Code28.

## Verification and review

Verification: 46 focused tests pass; the real two-flight runtime invariant
test passes; final baseline ran 806 tests in 323 modules, 6 known failures,
0 NEW, and printed BASELINE_OK. RED/GREEN evidence, commit identities and
remaining limitations are in REPORT.md. Native WDK package compilation and hardware correctness are unknown;
host tests validate callback arguments/decision branches, not VidSch execution.

Independent read-only review: no blocking correctness defect in the narrow
patch. Accepted findings: pending overlap remains unimplemented; the supplied
trace does not establish the outgoing bridge as cause; cache-hit count cannot
be inferred from completed GPU work. The analysis/estimate above includes all
three limitations. No additional wait deduplication was added without evidence.

## Tandem review disposition

The worktree has no `.local/tandem/REVIEW.md`; the canonical shared file at
`/Volumes/pwdev/public_windows/.local/tandem/REVIEW.md` was inspected read-only.
Its historical OPEN items are not permissions to operate hardware in this task.

REVIEW R113: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R111: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R110: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R109: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R108: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R107: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R106: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R105: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R104: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R103: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R102: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R100: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R99: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R98: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R97: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R96: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R95: ACCEPT — preserve fail-closed G4 validation and exact completion/lifetime contracts; this UMD change does not relax them.

REVIEW R94: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R91: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R90: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R88: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R86: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R85: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R74: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R71: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R69: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R65: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R64: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R63: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R57: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R55: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R54: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R49: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R48: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R47: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R45: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R40: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R37: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

REVIEW R64: DEFER — historical hardware/firmware/G3 bring-up item outside Q11 offline queue-completion scope; no related behavior changed.

## Q11b: warm the measured C→R successor (offline follow-up)

The probe now warms the actual blue render before `Q3RC init` in Q3CR and
Q3RCR. Setup list/allocator 3 draws one instance with the measured graphics
PSO/root, `vertices2` and `rtv2` (which names `texture2`). Two sequential
passes each use `run_list` to wait for the exact application fence before
resetting that allocator/list. Their fences are base-2 and base-1; compute
calibration uses at most base-8, init uses base, and measured work starts at
base+1. Failure stops setup; an uncertain submit terminates the probe without
releasing potentially outstanding resources. WARP follows the same setup.
The measured submission helpers and driver ordering are unchanged.

The first pass exercises first-use resource/state setup; the second exercises
reuse after retirement. This bounded second pass is a precaution justified
by the existing PagingFence and PoolMapEscape costs, not a claim that the
trace proves one warm-up insufficient or two sufficient. Reused render
resources can still incur per-batch mapping, eviction or scene preparation.
Q3CR's supplied trace has 33.678 ms replay, with 15.963 ms PagingFence and
10.819 ms PoolMapEscape (`tool-q3cr-umd.log:584,586,588`). Those buckets must
be measured again. No sleep, residency override or skipped wait is added.

The existing initialization remains **after both warm-ups**: it resets rtv2
to red and CPU-retires setup. Consequently, stale warm-up blue cannot pass
if the measured successor is omitted. All original exact compute/green/blue
output checks remain. The regression executes the real warm-up helper with
COM boundary fakes, injects submit/allocator/list failures in either pass,
and executes the real output oracle against red sentinel data.

This is application resource preparation. The probe owns setup/lifetimes
and the sentinel; UMD/VidMm own replay/residency, KMD/firmware own native
admission and ordering. Hardware initialization, IRQs, DMA, power, firmware
and recovery contracts are unchanged. Source references: probe
`run_list`, `q3_record_render`, `q3_calibrate_compute`, `q3_sequence`;
Asahi `agx_state.c:4862,5556` (`agx_draw_vbo` and its registration); pinned
SDK 26100 `d3d12.h:5297,7043` (allocator/list Reset). Microsoft requires GPU
completion before allocator reset, which the exact-fence wait supplies.
[Allocator Reset](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandallocator-reset),
[ExecuteCommandLists ordering](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/nf-d3d12-id3d12commandqueue-executecommandlists).

### Read the next measured interval

The probe prints `successor_execute=2` for Q3CR or `=3` for Q3RCR (1-based),
the successor application fence, and `successor_umd_replay_us=unavailable
source=umd-trace`. This is deliberate: D3D12 exposes no internal UMD replay
measurement. Read the following existing receipts for the same PID and the
selected Execute inside `pair_qpc_begin`/`pair_qpc_end`:

- `measure-d3d12-execute`: payload `{lists, commands, replay_us, submit_us,
  serial}`; payload integers in the trace are hexadecimal.
- `measure-d3d12-replay-window`: `{start, replayed, submitted, hz}`, each a
  low/high 32-bit pair. Replay duration is `(replayed-start)/hz` seconds;
  submit duration is `(submitted-replayed)/hz`. UMD source:
  `agx_d3d12_ddi.cpp:7122-7157`.
- `measure-d3d12-wait`: join the Execute serial; draw-phase `0x402` is
  PagingFence and `0x41c` is PoolMapEscape. Compare these costs with the cold
  trace above, including each warm-up's preceding receipts.
- Match actual successor KMD admission and its predecessor completion using
  PID, submitted work/fences and compatible timestamps. UMD replay-window
  end and application Execute return are not themselves KMD admission proof.

The new guidance prints before the pair QPC interval; nothing is added
between measured Execute calls. Retain untruncated logs. The first line of
`measure-d3d12-execute` is not necessarily the measured sequence: calibrations,
initialization and both warm-ups precede it.

Smallest integration checkpoint: same driver package/switches as the intended
Q3 test, `Wom1Depth2TaOnCompute=1`, TaOn3d off, run `only Q3CR` then `only
Q3RCR`. Change only this probe preparation. PASS requires exact outputs and
successful original fences, typed C→R publication while C is live, retained
R→C behavior and no reset/removal. Output PASS with reason66/no typed interval
remains INCONCLUSIVE for overlap. If paging/map costs remain, this falsifies
the sufficiency of this warm-up; use the new measured buckets to identify
the remaining preparation, not another unsupported wait deletion. Recovery
belongs to the integration owner's preregistered exact-package cleanup.
No hardware, guest, builder or package operations were performed for q11b;
actual native MSVC builds and hardware timing remain unknown.

Offline verification: 20 focused probe tests pass, including fresh-stage
full-probe Windows-target syntax checks on x86/x64/ARM64. Required full
baseline: `ran 810 tests in 326 modules; 6 failing, 6 known, 0 NEW, 0 FIXED`,
`BASELINE_OK`. Two new regressions were RED before the helper/call existed
and GREEN afterward. Independent review found no blockers. Native MSVC
build/link and hardware validation remain integration gates.
