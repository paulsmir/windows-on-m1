# Q16: Q3PC's prelude was real; its successor arrived late

Offline base `89ef2ba6bcbc09e4a0bf5adc2a6d7afbdb60481e`, branch
`codex/q16-q3pc-topology`. Supplied EXP2100/2102/2104 evidence only. No
hardware, guest, builder, package or other-worktree operation. Paths below
start at `drivers/apple-agx/` unless explicitly rooted elsewhere.

## What EXP2104 actually established

**Yes, the measured render carried a compute prelude. No, the compute
successor did not reach KMD while that render ran.** A successful statistics
query is a real prelude trigger; Q15's mistake was treating its calibration
and exact-output PASS as sufficient to arrange the candidate pair.

Evidence folder: `.local/handoff/codex/q16-q3pc-topology` under the repository's
shared local root. `tool-q3pc-std.txt:2,46-49` identifies PID 5652 and measured
QPC window 1582355505..1589115974 at 24 MHz. The original run's exact counts
were only four vertices/two primitives: one fullscreen instance.

Join by completion sequence/fence, not by the nearest printed line. UMD
collects already completed jobs after submitting each Execute; consequently
the first Execute prints an older compute completion, the second prints the
measured render, and readback prints the measured compute.

| Measured work | Render | Compute successor |
|---|---:|---:|
| Execute serial | 14 | 15 |
| Replay start QPC | 1588467665 | 1588720132 |
| Replay end QPC | 1588702601 | 1588721073 |
| Submit-call end QPC | 1588719139 | 1588996435 |
| Replay / submit time | 9789 / 689 us | 39 / 11473 us |
| KMD fence / completion sequence | 7170 / 2792 | 7188 / 2801 |
| KMD submit QPC | 1588716634 | 1589067457 |
| KMD complete QPC | 1588735339 | 1589089068 |

Sources: `tool-q3pc-umd.log:652-654`, `:667`, `:673-676`, `:695`.
The decoded `kmd-receipts-after-q3pc.txt:184` has no reason-5 histogram
entry. The retained PID-5652 reason-66 aggregate spans multiple fences and
partially overlaps the filter; its count is not a per-measured-pair count.
`decisions-pair.json` preserves that decoder caveat. The completion timeline
above independently proves the measured successor was late.

Render KMD residence is **0.779375 ms**; successor KMD submission is
**13.838250 ms after** render completion. These are CPU lifecycle intervals,
not GPU execution durations. The pair's initial ~254.7 ms before UMD replay
is outside the reported replay interval and is not assigned to a specific
runtime operation by this trace.

`tool-q3pc-umd.log:669` associates prelude firmware timestamps
2377553361..2377553590 with sequence 2792. That is positive evidence of this
render's compute prelude, even though its 3D Start is stale. The producer
`render-admission/umd/src/umd_gpuva_windows.c:2874-2896` prints the
completion sequence/fence/QPCs, then that sequence's PreludeStart/End.
`render-admission/src/backend_platform_windows.c:817-855` captures them
from the pending render's ComputeTimestampBlock and timing slot.

## Which field means prelude, and who supplies it?

1. Primary Mesa `.local/reference/mesa/src/gallium/drivers/asahi/agx_state.c:4933`
   calls `agx_ia_update` for active IA/VS/clipper statistics. At `:3831` it
   allocates `batch->cdm`, then at `:3874` emits `libagx_increment_ia`.
   Windows `mesa/winsys/agx_d3d12_windows.cpp:2410-2435` enables the eleven
   Gallium statistics. `native-asahi-query-ia.py:109` suppresses the original
   update only when the GPU indirect stream or batched IA path already owns
   the counters; EXP2104 used an ordinary direct draw.
2. Primary Mesa `agx_pipe.c:1616-1632` submits both `compute` and `render`
   when both CDM and VDM exist. Windows
   `mesa/winsys/agx_win32_gpuva_batch.c:1052-1069` puts compute in a **v5**
   render packet with compute preemption storage. The encoder-side metadata
   is `Render.ComputeEncoderReference/ComputeEncoderBytes`
   (`agx_win32_asahi_batch.c:237-244`).
3. Parsed `View->HasCompute` becomes **`Image->G4WithCompute`** in
   `render-admission/src/render_backend_image.c:377-378`.
   `backend_platform_windows.c:5016-5026` maps a native render with that bit
   to **`ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE` (2)**. Standalone compute
   is kind 3. This field, not the presence of a D3D query object, controls
   the admission topology.

The compute prelude is in the same KMD render job; it is not an independently
submitted application Dispatch. KMD owns admission, queue publication, DMA
lifetime, interrupt/completion and recovery; firmware executes and retires.
UMD owns replay, preparation and the G4 payload. The application probe owns
legal API calls, bounded work and output checks. Firmware/m1n1/Mu initialization,
power, ACPI and mappings are unchanged. No live state is inferred or required
for this probe-only change, and no external implementation was copied.

## Why the successor was late

Both measured queue-wait receipts (`tool-q3pc-umd.log:643`, `:662`) have
`ordered=1`; owner fences are 31 then 32. Source
`mesa/winsys/agx_d3d12_windows.cpp:5082-5114` flushes the owner, obtains its
fence and inserts a GPU wait on the application queue; only an unordered
path calls CPU `ContextRetire`. The trace does not establish a CPU fence wait
between the measured endpoints. Deleting queue waits would violate ordering
and would not address this evidence.

Successor replay itself takes only 39 us. Owner flush takes 11431 us
(`tool-q3pc-umd.log:663`, hex 2ca7). The new BO creation spans 11195 us
(`:659`, 2bbb); the exclusive wait receipt attributes 11149 us to
**submit-phase PagingFence**, key 0x602 (`:678`, 2b8d).
`mesa/winsys/agx_win32_wait.h:6-23` defines site 2 and phase 6;
`render-admission/umd/include/umd_wait_profile.h:93` encodes phase*256+site.
This is preparation/residency cost despite warming, not evidence that an
outgoing application queue wait blocked native submission. KMD enqueue can
also occur after the UMD submit call returns; the completion record is the
correct boundary for whether a successor was pending.

Calibration made this worse. Stdout `:23-40` reports stale Start words,
substitutes the previous job's End, measures 64..454 ms including inter-job
CPU/DWM gaps, and shrinks 1024 instances to 1. The one-instance result is
explicitly `outside-target`. `q3_render_duration` and `q3_calibrate_render`
remain unchanged for existing ordinary Q3 probes; Q3PC now bypasses that
unsound duration estimate. This task does not claim to repair timestamp
ownership or all existing calibrators.

## GravityMark's prelude triggers and the corrected probe

The current default counted/multi-draw path is the GPU-generated indirect
VDM stream. `mesa/scripts/native-asahi-indirect-stream.py:193-216` selects
it for an indirect count buffer or multiple records. The eligibility checks
in `mesa/winsys/agx_win32_indirect_stream_emit.h:8-47` accept ordinary VS/PS
with pipeline statistics (no GS/tessellation, streamout, restart or occlusion
query); the explicit `GPU_INDIRECT_STREAM=0` override falls back.
At `:228-232` it allocates CDM; `:300-311` dispatches the stream producer
with `AGX_PREGFX`, marks path=3, and links the produced VDM.
The same file `:41-43` records GravityMark validation in EXP1993.

Pipeline statistics **are also a trigger** on the direct/native path, as
EXP2104 proves. For unrolled multi-draw fallback, the Windows RC67 helper
`mesa/winsys/agx_win32_query_ia.h:7-16,77-111` batches IA updates into one
CDM dispatch; its source records the GravityMark query cost. When the stream
path is selected, it owns those IA query addresses and counts instead of
emitting a second native per-draw IA dispatch. Geometry/tessellation and
other Asahi pre-raster kernels can create CDM generally, but the supplied
EXP2104 evidence does not identify those as GravityMark operations. Do not
claim a one-to-one assignment of every EXP2102 reason-5 sample to an app call
without its own trace.

Q3PC/Q3CP now use a pre-created DRAW-only signature, two legal 16-byte
records, **MaxCount=2 and count=1**, in the existing upload buffer at offsets
2048/2080. The second record draws one extra instance, so ignoring the count
fails exact IA/VS/clipper checks. This is a legal counted-indirect path, not
an admission exception based on fixture values. The SDK definitions are
`.local/reference/winsdk26100/um/d3d12.h:4527` (query result), `:6580`
(ExecuteIndirect); the existing C14/RC probes use this signature pattern.

`windows/icd/agx_d3d12_probe.c:8399-8442` shares bindings between direct and
indirect draws, updates only the argument region and records the counted
call under the query. `:8777-8801` warms exactly that query/resources/signature
twice serially with one extra instance; it restores measured arguments only
after retirement. Red pixels, zero compute words and zero query readback
remain sentinels. Query resolve and all readback remain after the pair.

Q3PC fixes render work at 262144 fullscreen instances (the existing Q3 count
upper bound) instead of estimating GPU time from stale timestamps. It emits
no new timing-duration claim. This is deliberately more work than EXP2104's
one instance to cover successor preparation; real duration, TVB pressure and
successful pending admission still require hardware evidence. Q3CP retains
one render instance after its calibrated compute predecessor.

`q3_prelude_pending` (`:8524`) requires one successful measured render and
one compute, a valid timed prelude, distinct fences, complete unambiguous
records, unchanged MissingTiming, no loss/reset, and the successor's KMD
SubmitQpc strictly between predecessor SubmitQpc and CompleteQpc. Missing
proof with exact outputs prints **INCONCLUSIVE**, returns nonzero and never
prints the former unconditional PASS. WARP can check outputs but cannot
qualify this Apple topology. This oracle proves a pending **candidate**, not
physical overlap or successful prebinding; typed KMD receipts remain required.

## Default-gate prediction and integration-only checkpoint

`render-admission/src/render_job_window.c:95-112` first requires a single
in-flight head, distinct prebound queued successor, stable mappings, same
process and valid G4 kinds. Under those conditions, General=1 and Prelude=0
with either endpoint kind 2 return **reason 5**. For RP/C, `:38-46` selects
compute-on-3D barrier 3. This source implication is exact; an offline source
change cannot prove the hardware satisfies its antecedents.

WHY THIS HYPOTHESIS:
- EXP2104 proves a real prelude but a 0.779 ms render and 13.838 ms-late C.
  More bounded render work directly targets the demonstrated missing overlap.
- EXP2102 has 32669 prelude refusals; the GPU stream path used in GravityMark
  supplies a source-proven counted-indirect prelude trigger to this small probe.
- EXP2104's stale-start calibration selected one instance. Bypassing it and
  checking KMD submit/complete intervals prevents that false qualification.

Integration owner: native-build all three ABIs and pin exact artifacts, package,
firmware, accepted General prerequisites and recovery. First run `only Q3PC`
with Prelude absent/0, drills off and GPU stream not opted out. Require:

- exact pixels, compute words and IA/VS/clipper counts;
- measured `measure-d3d12-counted-path` path=3/reason=0
  (`mesa/winsys/agx_d3d12_windows.cpp:4866-4875`; statistics alone can create a prelude);
- positive measured PreludeStart/End and `pending_successor=yes`;
- PID/QPC/fence-matched reason 5 with kinds 2/3 and barrier 3; no reset.

No reason 5 is INCONCLUSIVE regardless of API PASS. Only after the control
qualifies, separately change **only** Wom1Depth2Prelude=1, requiring typed
admission, two distinct published jobs, physical-progress evidence and each
original completion once in order. Q3CP is a later separate checkpoint with
ComputeOnCompute=1, kinds 3/2 and barrier 4. No application rollout follows
from a single synthetic PASS.

Collect decisions/progress/completions/PostResume v4/ETL and any dump before
exact experiment cleanup to ordinary GPU-visible Code28. Preserve the known
good recovery artifact; emergency-hidden recovery is the established fallback,
not a routine intermediate boot. Process deadlines do not recover firmware.
No hardware action was taken in this task.

## Verification and remaining unknowns

The real-helper regressions cover counted calls, preserved upload bytes,
retired warm-up updates, stale query counts, EXP2104's actual late timing,
missing/lost/reset/ambiguous records and ordinary probe compatibility. Exact
commands, RED/GREEN logs, independent review, three Windows-target COFF
results and final BASELINE line are in the handoff REPORT.md.

Still unknown: native MSVC/WDK /W4 /WX and EXE linking; revised probe's actual
render cost, stream-selection receipt, pending/prebound successor and reason-5
control on hardware; opt-in correctness and firmware assertion/recovery from
EXP2100. No hardware qualification, performance improvement or firmware fix
is claimed. Integration owns ledger updates; EXPERIMENTS.md, CHANGES.csv,
GPU_CURRENT_STATE.md and known_failures.txt remain untouched.
