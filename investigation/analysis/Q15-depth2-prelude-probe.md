# Q15: bounded prelude pairs and retained queue stamps

Offline base `66725b5428d30382ddc4fcbd9f9ed2e6823a1734`. No hardware,
guest, builder, package, m1n1/Mu change, or other worktree access. EXP2102 is
not evidence for this task. Paths below begin at `drivers/apple-agx/` unless
marked otherwise; Linux/Mesa sources are the local primary references.

## Conclusion

The Windows compute stamp unit differs from Asahi, but the available source
and EXP2100 evidence do **not** prove a regressing write, duplicate stamp, or
firmware assertion caused by that difference. Do not multiply fences by 256
as a speculative fix: it would change barriers, completion, wrap and recovery
contracts. `Wom1Depth2Prelude` remains default off. This change fixes missing
reset evidence and delivers a bounded nominal discriminator in both directions.
No GPU admission, stamp encoding or reset-success behavior changes.

## Source proof: the actual prelude trigger

- Windows `mesa/winsys/agx_d3d12_windows.cpp:2418` through `:2432` begins and
  ends all 11 Gallium pipeline-statistics counters for the D3D12 query. Its
  grouped allocation resets the same query BO before Begin.
- Mesa `.local/reference/mesa/src/gallium/drivers/asahi/agx_state.c:4934`
  tests the active IA counters; `:4949` uploads direct-draw counts.
  `:3826`/`:3831` creates the batch CDM encoder and `:3874` dispatches
  `libagx_increment_ia`. The Windows projection
  `mesa/scripts/native-asahi-query-ia.py:109` only suppresses the native update
  for the counted-indirect special paths; this direct draw uses neither.
- Mesa `agx_query.c:304`/`:333` ends statistics by detaching the query, without
  flushing. `agx_pipe.c:1616`/`:1631` flushes CDM and VDM together. This is a
  render with a compute prelude, rather than two app-submitted jobs.
- Native `.local/reference/mesa/src/poly/geometry.h:587` supplies exact IA,
  VS and clipper counts. Four triangle-strip vertices produce two primitives
  per instance; no GS/HS/DS/CS invocation is part of this render query.
  Pixel-shader invocation counts depend on rasterization and are deliberately
  not the IA oracle. All render pixels and all 64 compute words are checked.
- Pinned SDK `.local/reference/winsdk26100/um/d3d12.h:4491`/`:4527` specifies
  the heap type and 11-UINT64 result layout used by the probe. Query resolve
  writes an aligned (49152-byte) offset in a COPY_DEST readback buffer.

## Source proof: values and ordering

Let P/S denote original predecessor/successor submission fences; T(X)/D(X)
are their render stamps. These are **not** app ID3D12Fence values.

| Pair | Compute ring | TA ring | 3D ring |
|---|---|---|---|
| RP(P) → C(S) | write P; wait D(P), then write S | wait P, then write T(P) | wait T(P), then write D(P) |
| C(P) → RP(S) | write P; wait P, then write S | wait S, then write T(S) | wait T(S), then write D(S) |

Evidence for each edge/value:

- `render-admission/src/backend_platform_windows.c:3265` gives standalone
  compute `StampValue=fence`, `EventSequence=fence`, client low byte;
  `:3549` gives a native render prelude the identical raw-fence convention.
  `:3299`/`:3579` retain it as `ComputeExpectedStamp`.
- `shared/src/apple_agx_g13_compute_work.c:82` puts StampValue in RunCompute
  at +0x298; `:152` puts it unchanged in FinalizeCompute +0x30. There is no
  implicit shift hidden in the encoder. Render T/D stamps instead derive
  from base + sequence*step in `apple_agx_exp208_dynamic.c:87`, with step
  0x100, and are passed by `apple_agx_submission_coordinator.c:122`.
- Platform `:3344` constructs TA-on-own-compute, including exact value and
  event slot. `:3164` selects the predecessor terminal stamp: D(P) for any
  render, compute P for C. The caller supplies S as StampSelf when the
  successor starts on compute (`:3310`, `:3589`).
- `shared/src/apple_agx_g13_queue_runtime.c:313` and `:388` use the same
  Config.Compute binding for standalone and prelude work. Publication copies
  all rings before kicks; `:515` retains each queue's expected stamp separately.
  `render_job_window.c` chooses the successor's first queue, so C→RP is
  COMPUTE_ON_COMPUTE (4), not TA_ON_COMPUTE (1). It needs the separate
  `Wom1Depth2ComputeOnCompute` capability, read by platform `:7321`.
- Asahi `.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/event.rs:58`
  advances by 0x100 and `:84` compares signed deltas shifted by 8.
  `workqueue.rs:309` advances per command; `queue/compute.rs:215`/`:339`
  emits next() into compute finalize/work. `workqueue.rs:757`/`:788`
  intentionally retains one event/stamp across outstanding queue users.
  `queue/mod.rs:787` builds explicit barriers from prior queue events with
  the destination queue's next stamp as StampSelf (`:814`).

For EXP2100, Q13 `cdb-output-1.txt:1202` gives RP 0x182a → C 0x182b;
round 2 `:333`/`:338` gives wait D(P)=0x3d0b1400 and TA wait P=0x182a.
The corrected compute firmware word was 0x1827 (Q13 analysis). Raw P and S
are distinct and increasing; there is no demonstrated regressing write.
Applying **Linux's** comparator gives `(0x182b-0x182a)>>8 == 0` (and the
reverse negative delta is -1), which shows why adjacent raw values do not
represent Asahi event units. It does not establish proprietary firmware's
comparison operation. Windows host comparisons use unshifted modular deltas
(queue runtime `:884`, `:960`, `:977`). The real-runtime model accepts these
values; that model cannot prove proprietary firmware accepts them.

FIFO and the terminal dependencies above give an acyclic graph. The shared
compute queue is intentional; separate job words are reserved and retained
(`apple_agx_render_shared_memory.c:807`, `:815`, `:964`). Asahi also emits
explicit prior-compute barriers even when a render dependency is transitive;
Windows relies on FIFO plus the terminal dependency. A firmware requirement
for that extra barrier remains a separate hypothesis. No concrete source
contract identifies which difference caused the fatal RTKit notification.

## Delivered bounded probes

`windows/icd/agx_d3d12_probe.c:8418` scopes the query over the actual render;
`:8425` checks exact counters. `:8724` warms both exact endpoints and the same
query slot twice at depth one. Warm-up draws one extra instance, so a stale
warm-up result fails measurement. Readback statistics start at zero; target
pixels start red and compute words zero. Both measured lists close before
any submission; all submits/signals precede the terminal application CPU wait. Internal UMD
replay/residency can still delay admission; the PID/QPC trace join and KMD
overlap evidence are mandatory. Resolution
and copy/readback are at `:8972`, after measured completion.

- `agx_d3d12_probe.exe only Q3PC`: calibrated green 64x64 render with statistics, then compute
  producing 64 exact `3*i+7` words. Required pair kinds 2/3 and barrier 3.
- `agx_d3d12_probe.exe only Q3CP`: calibrated compute recurrence, then blue statistics render.
  Required kinds 3/2 and barrier 4. The existing q3_bad_compute recurrence
  oracle checks every word. It is only an overlap experiment with the
  COMPUTE_ON_COMPUTE capability explicitly enabled.

Both are named-only fresh-process steps (`:9037`). PID, app fences and pair
QPC bounds identify the trace interval. Predecessor calibration uses firmware
completion timestamps, not CPU Execute duration; WARP is explicitly only an
API/output control. Terminal wait is 3 seconds; the process watchdog starts
before device creation and terminates after 15 seconds (`:7308`). One measured
pair per process bounds the experiment. API PASS always says overlap=UNVERIFIED;
actual RP kind, publication and overlap must be established by KMD evidence.

## Receipt fix and invariant

Wom1RcDecisions v4 already retains typed kinds/barrier and first/last fences
(`render-admission/include/render_rc_progress.h:46`); changing its aggregation
is unnecessary. Wom1RcProgress retains expected terminal/first-queue stamps,
but its 128 records may fill before a later application fault.

PostResume v4 appends 48 bytes to the unchanged 264-byte v3 prefix:
`render_gpu_fault.h:211`, ExpectedStamps[6] then EventSlots[6]. Order is head
TA/3D/compute then successor TA/3D/compute. Platform `:7633` copies CPU-owned
pending metadata while reset owns the idle worker, including an already
latched fatal RTKit result. Unused queues are zeroed, even into dirty output
storage. This path is independent of software drill enablement and reads no
new SGX/ASC registers or firmware pointers. Decoder
`tools/decode_wom1_gpu_fault.py:594` accepts v1-v4 with strict length/version
pairs and exposes each named queue's expected value and event.

The real-runtime crash regression checks all nine R/RP/C pairs, one/two flights,
three repeated fatal drains, unchanged fence ownership and a later empty
snapshot. Producer→decoder tests pin all offsets and older versions. This
fixes missing diagnostic fields, not the GPU fault. Native WDK /W4 /WX remains
an integration build gate; host strict-warning and cross-target checks do not
substitute for MSVC.

## Ownership, remaining fork and smallest hardware checkpoint

KMD owns publication, DMA/job lifetime, interrupt/completion and reset; firmware
owns physical execution/retirement; Mesa owns prelude creation. WDK
`.local/reference/wdk26100/d3dkmddi.h:1009` still reports original submission
fences. m1n1/Mu initialization, ACPI, power and recovery are unchanged; their
absent submodules are not inferred from historical source. Supplied EXP2100
and Q13/Q14 evidence is the only machine-state reference. No external code was
copied. Full preregistration/recovery order is in handoff DESIGN.md.

WHY THIS HYPOTHESIS:
1. EXP2100's fatal pair had two compute users; accepted ordinary R/C lacks
   this topology. A tiny IA prelude distinguishes universal queue-sharing
   failure from GravityMark's much larger indirect/CDM workload.
2. Raw adjacent compute stamps differ from Asahi's event units, while the
   observed barriers and separate words weaken simple cycle/alias hypotheses.

First run the same-package Q3PC with Prelude=0: correct output and typed
reason 5. Separately preregister Prelude=1 with all other settings pinned.
Require genuine kinds 2/3, barrier 3, distinct published jobs, prelude timestamps,
correct expected stamps, original reports once in order, exact outputs, and
healthy subsequent ordinary work. A missing RP/overlap or unusable receipt is
INCONCLUSIVE. Q3CP follows separately with class 4 enabled, not bundled.

If raw adjacent stamps pass the nominal overlapped probe, a universal rejection
of any two raw compute users becomes less likely; focus next on application
prelude contents/lifetime. If it fails with a fatal RTKit notification and
v4 shows distinct monotonic P/S plus the intended slots, the remaining fork
is firmware stamp/explicit-barrier protocol versus prelude execution/data.
That receipt alone **cannot distinguish those two**. An exact firmware
assertion or a separately audited one-variable stamp/barrier candidate is then
required. Repeated/decreasing values or incorrect slots in the new receipt
instead identify a concrete publication/identity target for the next fix.
No third cdb round on EXP2100, no broad application opt-in, no fabricated
retirement, and no claim of fatal-firmware recovery.
