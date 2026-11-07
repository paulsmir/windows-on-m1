# Q5: retain the completed render's timestamp storage

The source confirms an ownership defect when an actual depth-two render build
rebinds `QueueObjects` before the older job completes. **It does not confirm
that depth-one rc90 prebind causes the EXP2087 stale-start symptom.** Saved
EXP2073–EXP2082 telemetry independently shows stale render starts even on the
serial path. The address-retention correction below fixes the first defect;
it does not establish a repair for the second.

## Source evidence and ownership

Paths below are relative to `drivers/apple-agx/` unless otherwise noted.
Line references identify the corrected tree; the removed readers are visible
in its implementation commit's parent.

| Evidence | Contract / consequence |
| --- | --- |
| `shared/src/apple_agx_render_shared_memory.c:591` and `:602` | Objects 13–19 and 28–35 have two copies; `((TaExpectedStamp >> 8) ^ 1) & 1` selects the job's parity. |
| Same file `:660` and `:697` | Binding sets each `ActiveObjects[index].Data` to that parity's address; it is a mutable build table, not a completion record. |
| `render-admission/src/backend_platform_windows.c:3418` | The platform render builder passes `runtime->QueueObjects` as `ActiveObjects`. |
| `render-admission/src/gpuva_g3_windows.c:4944` and `:4973` | rc90 prebind calls `AdmissionBackendImageBindG4Submission` / the compute binders on `adapter->BackendImage`. It does not run the shared active-job builder or change `runtime->QueueObjects`. |
| `render-admission/src/backend_platform_windows.c:5111` and `:6019` | Prebind is distinct from `AdmissionJobWindowKickWindows`; only the latter can build/submit a successor while the older job runs. |
| `shared/src/apple_agx_backend_runtime.c:250` and `:283` | Successor relocation fills a distinct `SuccessorJob`; promotion copies the entire image into `PendingJob`. |
| Same file `:405` and `:414` | The completion callback runs on the old pending job before successor promotion. Batched reporting still visits jobs in that order. |
| `shared/src/apple_agx_render_shared_memory.c:1114` and `:558` | q2 flights reject another live render with the same parity and retain the slot until explicit retirement. |
| `render-admission/src/backend_platform_windows.c:4205` and `:4414` | Captures occur while the completing job is pending; flight retirement occurs after reporting succeeds. |

Depth-one sequence: build N → run N → restore/rebind **BackendImage** for N+1
→ capture/report N → build N+1 and rebind QueueObjects. Depth-one build-ahead
alone therefore does **not** expose N+1's parity through the old readers.
Receipt-only depth two has the same distinction: noting eligibility is not a
successor build.

Actual depth-two render-successor sequence: build/run N → prebind N+1 →
`SubmitSuccessor` relocates/builds N+1, rebinding QueueObjects → capture N.
For R→R / render-with-prelude successors this can return the successor's 3D
and TA timestamps under N's fence. C→R also changes the table, but compute N
already uses its own `ComputeTimestampBlock`. R→C does not itself rebind the
render timestamp objects: the compute builder uses its sidecar. The q2 flight
records prevent byte reuse, but the old capture readers did not select the
held flight's storage. A rejected render successor may also leave the table
rebound; the retained old job is still the correct lookup owner.

In particular, EXP2087's `Wom1RcDepth2=1` restricts admission to R→C
(`render-admission/include/render_rc_progress.h:123`,
`render-admission/src/backend_platform_windows.c:7162`). It does not admit the
render-successor class that rebinds this table. The calibration failed before
the intended pair, so the ledger's attribution to build-ahead slot switching
is refuted for that boundary; the ledger itself is left unchanged for the
integrating session to append a correction.

The shared owner owns allocation, mapping and parity selection. The backend
owns the pending/successor job images; the platform worker owns completion and
reads the timestamp values before reporting/retiring that job. Firmware owns
writes to those values. There is no new mapping, allocation, register, IRQ,
power, initialization, queue protocol, synchronization or recovery contract.

## Correction and invariant

`shared/include/apple_agx_backend_runtime.h:119` adds eight private CPU
pointers to the job image. `shared/src/apple_agx_render_shared_memory.c:1172`
validates the eight-byte objects 28–35 and retains all eight addresses from
one active build. Every successful render builder goes through this function.
The existing job copies and zeroing preserve/initialize the new fields.

`render-admission/src/backend_platform_windows.c:662` reads a complete pair
from that job's retained addresses or reads neither value. It has no fallback
to QueueObjects. Consumers now use it for:

- End-nanosecond publication (`:727`), preserving the completion retry cache;
- APTS v1 timestamp capture (`:760`) and APTS v3 completion capture (`:802`);
- TA timing fields (`:856`) and the eight timestamp values in FwTiming (`:674`).

The pointer is captured at build time; the firmware value is read at completion
time. This avoids an early value snapshot. Compute and render-prelude sidecar
reads retain their original behavior. Compute completions still record their
independent pstate observation. A compute completion no longer labels the
previous render's TA timestamps as its own.

`FwTiming.TaTail`, shared TA statistics and the standalone queue/progress/debug
snapshots still describe their existing diagnostic sources. They are not
converted into per-job timestamp proofs by this change. Buffer-manager stats
also keep their existing ownership path. The APTS wire layouts, version,
process filtering, cursor, ring sequencing, failure records and retry
deduplication are unchanged.

## Primary implementation / ABI check

The pinned local Asahi implementation allocates a fresh zero-defaulted
`RenderTimestamps` per render in
`.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/queue/render.rs:375`,
retains it in the fragment job (`fw/fragment.rs:286`), and makes the work
command point to its own fragment start/end (`queue/render.rs:923`). The
start/end microsequence operations name the command's timestamp pointers
(`queue/render.rs:525`, `:549`). This supports job-owned storage rather than a
mutable current-build lookup. No external implementation code was copied.

The local WDK `d3dkmddi.h:5797` defines the private escape data/size contract;
`:8805` declares the calibration DDI. The repository's private APTS structures
are in `render-admission/include/render_job_timing.h:397` and `:427`.
There is no Windows ABI change. The m1n1 and Mu submodules are uninitialized in
this offline worktree. Their pinned commits are unchanged; this task makes no
claims about new firmware behavior and does not access hardware to substitute
for those missing sources.

The added KMD index is `ULONG`; timestamp values and output pointers are
`ULONGLONG`. Shared builder indices are `APPLE_AGX_U32`; timestamp sizes are
compared with `sizeof(APPLE_AGX_U64)` without assignment/narrowing. No new
64→32 conversion, integer-to-pointer conversion, pointer-base-type mismatch,
CRT include or uninitialized output is introduced. This is a source/type
audit and host compilation, **not** a native WDK `/W4 /WX` build result.

## Consumers and historical results

`render-admission/src/backend_platform_windows.c:4806` copies the completion
ring into the read-only, process-filtered APTS v3 escape. The UMD drain in
`render-admission/umd/src/umd_gpuva_windows.c:2850` emits submit/complete QPC in
`measure-gpu-completion`, and raw Start/End/TaStart/TaEnd in
`measure-gpu-stages` (`:2886`). The escape consumer receives exactly the values
captured by the KMD; neither layer reconstructs the missing job identity.

`tools/agx_gpu_frame_breakdown.py:170` bounds its reconstructed timeline with
QPC submit/complete observations. Its `d3` estimate (`:193`) is
`min((End - TaEnd) / frequency, span)`; it does **not** use render `Start`.
A contaminated End or TaEnd at depth two can therefore corrupt `d3` and the
rolling clock-offset estimate, even if QPC clamping makes the result appear
plausible. End-only consumers (APTS v1 calibration and RC40 query publication)
are likewise susceptible to wrong-job End selection. No estimate substitutes
for a completed job's own End in the correction.

Read-only analysis of all 24 saved `gm2073a` through `gm2082b` UMD logs, using
the existing parser/timeline, found 475,401 successful render records for the dominant process in each log:

- Every run had exactly two distinct render Start values across its whole
  sampled interval. Every `End - Start` exceeded `CompleteQpc - SubmitQpc`.
- Every record had nonzero, ordered TaEnd/End values. Every raw `End - TaEnd` fit inside the corresponding QPC interval; the
  `d3` clamp clipped zero records. Render Ends never decreased within a run.
- Thus raw per-render durations derived from Start were already invalid.
  The historical `d3` results pass this consistency check and are not
  invalidated by stale Start alone. Bounds cannot prove exact firmware
  timing or exclude every small error.

Representative medians, milliseconds (all successful renders, including
small passes; these are not main-pass-only medians):

| Run | Records | Raw End−Start | Submit→complete | Raw End−TaEnd / tool d3 |
| --- | ---: | ---: | ---: | ---: |
| EXP2073a | 17,600 | 150,415.680 | 8.165 | 0.876 / 0.876 |
| EXP2075b | 23,078 | 366,750.257 | 4.977 | 0.786 / 0.786 |
| EXP2078a | 19,313 | 153,404.538 | 3.781 | 0.796 / 0.796 |
| EXP2080c | 19,779 | 580,999.383 | 3.782 | 0.795 / 0.795 |
| EXP2082a | 18,519 | 148,332.344 | 8.068 | 0.880 / 0.880 |
| EXP2082b | 23,010 | 364,005.342 | 5.027 | 0.788 / 0.788 |

Reproducible script and all 24 rows:
`/Volumes/pwdev/public_windows/.local/handoff/codex/q5-render-timestamp/analyze_history.py`
and `history.jsonl`. Input paths and byte lengths are recorded per row; files
are under `.local/experiments/EXP2073-*` through `EXP2082-*`, not another
worktree. No guest or hardware access was used.

## Verification and remaining unknowns

`tests/test_render_timestamp_ownership.py` compiles the real shared builder
and the actual Windows capture/publication/timing functions. The original
readers fail because N receives N+1's Start. The fixed readers pass for both
notifier modes, two live jobs, a third build reusing the first retired parity,
TA and 3D pairs, missing end storage, cached retries, record deduplication,
compute values and independent compute pstate. Existing completion/publication
fixtures now use the real shared job-image type. ASan, UBSan and patterned
automatic-variable initialization are enabled for the new replay.
Final combined targeted run: 71 tests in 19 modules PASS (14.487 s).
The required `cd tests && python3 run_against_baseline.py` completed with:

```text
ran 792 tests in 319 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

`tests/known_failures.txt` is unchanged. The task's REPORT.md records the
commands, logs, known failure names and the implementation commit.

**Still unknown:** why firmware render Start stays stale at depth one; the
EXP2087 calibration cannot be declared fixed by address retention. Source
provides one next discriminator: Asahi allocates zero-defaulted timestamp
storage per job, whereas `per_submission_object` (`shared/src/apple_agx_render_shared_memory.c:570`)
does not refresh objects 28–35 on ordinary builds. That difference and the
two fixed Start values warrant checking per-job timestamp initialization and
the firmware's start-write semantics. No extra firmware-visible write or
experimental workaround is bundled here.

The integrating session should first compile with pinned WDK `/W4 /WX`, then
run a hash-verified serial render calibration under the existing Q3RC profile.
That checks compatibility and preserves the separate stale-start discriminator.
A later, explicitly preregistered render-successor checkpoint is needed for
hardware coverage of the changed ownership path; the current restricted R→C
profile cannot exercise it. Do not broaden admission just to claim coverage. Match command/fence identity, require both pairs
to remain from that job while the successor table is current, and compare
End/TaEnd against QPC bounds. Reproduce serial stale Start separately; do not
claim this fix restores it. Record each run and preserve/use the existing
exact GPU-visible recovery artifact/package cleanup contract. This worktree
has built or installed no package and performed no hardware run.
