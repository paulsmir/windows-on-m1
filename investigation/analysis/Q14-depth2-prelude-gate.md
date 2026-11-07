# Q14: gate unproven prelude pairs at depth two

## Result and scope

General depth-two admission now refuses either render-prelude endpoint unless
`Wom1Depth2Prelude=1`. The typed reason is
`ADMISSION_JOB_WINDOW_REFUSAL_PRELUDE_UNPROVEN` (5, previously unused).
`Wom1RcDecisions` counts it and the decoder labels it `prelude unproven`.
Its v4 layout, 72 histogram bins, old reason numbers and unknown bin 71 remain
unchanged. Normal serial dispatch still runs these jobs. Explicit opt-in keeps
all existing class, sidecar, manager, idle, process and scheduler checks.
Legacy non-general policy is unchanged; the switch alone cannot enable general
mode. This prevents the unqualified topology from entering general depth two;
it does not repair a fatal firmware crash or establish why firmware asserted.

All paths below are relative to `drivers/apple-agx/` unless marked otherwise.
Primary Linux sources are under `.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/`;
Mesa sources under `.local/reference/mesa/src/gallium/drivers/asahi/`.

## Evidence and ownership

- `investigation/EXPERIMENTS.md:67519`, `:67545`, `:67569`, `:67593` record the
  no-prelude R/R, C/R, R/C and synthetic two-flight recovery results. Q6 DESIGN
  explicitly says prelude-bearing instances need their own bounded evidence.
- EXP2100 `investigation/EXPERIMENTS.md:67614` failed after about 30 seconds of
  FHD GravityMark with 0x116 and reset status c0000483. Q13's supplied
  `cdb-output-1.txt:1202` identifies RP 0x182a -> C 0x182b, compute-on-3D;
  `:1140` records RTKit FirmwareCrashed. This is a failing topology, not proof
  of the instruction that caused the firmware assertion.
- Q13 `cdb-output-2.txt:150`, `:197` shows different command, preemption,
  microsequence and timestamp storage for the two flights; `:333` and `:338`
  give the intended 3D and compute barrier waits. Ring-tail expressions failed
  (`:346` onward); their zero outputs are not valid ring evidence. No new dump
  round, guest access or hardware measurement was performed.

KMD owns admission, publication, queue/scene/DMA lifetime, interrupt/completion
accounting and reset. Firmware owns execution and physical retirement. Mesa owns
which render batch includes compute work. m1n1/Mu power, ACPI, mappings and launch
contracts are unchanged. Their submodule implementations are absent here; the
supplied experiment/q6/q13 contracts are the reference, with no new assumption
about inherited machine state. Pinned WDK `d3dkmddi.h:1009` reports original
submission fences and `:8332` declares PASSIVE_LEVEL ResetFromTimeout. This
patch neither changes that ABI nor invents completion/physical retirement.
No external implementation was copied.

## How the prelude and successor are encoded

1. Mesa `agx_state.c:3826` allocates a CDM encoder for IA-statistics updates;
   `:3874` emits `libagx_increment_ia`; `:4934` selects it for an active pipeline
   statistics query. `agx_pipe.c:1616` flushes CDM when present and `:1631`
   submits compute and render together. This supplies a simple prelude trigger
   without needing a tessellation or geometry pipeline. The Windows RC67
   helper `mesa/winsys/agx_win32_query_ia.h:2` documents the same prelude use.
2. KMD `render-admission/src/backend_platform_windows.c:3524` builds that
   render's compute work, using Config.Compute.EventNumber, the original fence
   as StampValue/EventSequence, and a selected job-word set. At `:3578` it saves
   the compute endpoint; `:3581` constructs a TA barrier on the prelude compute
   stamp. `AdmissionBuildTaComputeBarrier` at `:3344` supplies wait address,
   value and event slot. TA/3D are not held back for a CPU compute completion.
3. Shared `apple_agx_g13_queue_runtime.c:388` prepares all three rings using
   the same `Config.Compute` binding as standalone compute (`:313`). At `:470`
   it writes the rings, publishes their write pointers, and at `:498` sends
   compute, 3D and TA run messages. Each queue has its own event and endpoints.
4. A successor compute gets a leading barrier before RunCompute
   (`apple_agx_g13_queue_provider.c:621`). `AdmissionBuildLeadBarrier` at
   platform `:3164` selects the predecessor's **terminal** queue: for a
   render/prelude it waits on the 3D firmware stamp, `D3ExpectedStamp`, D3Event.
   It does not wait on the compute stamp that the prelude writes.

The resulting dependency graph is:

    compute ring: prelude(P) -> wait 3D(P) -> compute(S)
    TA ring:      wait compute(P) -> TA(P)
    3D ring:      wait TA(P) -> 3D(P)

This graph has no cycle if queues preserve command order and the stamp protocol
is correct. The CPU can have two outstanding users of the **compute queue**;
they are not meant to own the same **ring entry** or job-word set.

## Does Asahi do this?

Yes, the source permits this topology, though it is not independent hardware
proof for this Windows encoding. Mesa `agx_batch.c:820` emits compute with
barriers on prior render/compute; `:859` then emits render waiting on this
submission's compute. The next compute submission again barriers on previous
render and compute. `agx_pipe.c:1487` explicitly describes sequential UAPI jobs
with compute+render barriers on the previous job.

Linux `queue/mod.rs:650` snapshots previous fragment/compute event info;
`:787` selects the destination queue by command type and builds barriers from
that event's firmware stamp, value and slot. Its events table is updated after
each compute/render command (`:836`, `:858`). Compute in both submissions uses
`q_comp` (`:735`), not a freshly allocated queue for every prelude. Submission
is committed across all queues before run messages (`:349` through `:384`).

Shared state is intentional in Asahi:

- `workqueue.rs:743` and `:757` retain one event slot/stamp pair while jobs are
  outstanding; `:788` gives new jobs that same slot and queue pointer.
  `:373` validates the sequence and commits the new event value; `:433` checks
  command submission ordering. Sharing an event slot is not itself a defect.
- `queue/mod.rs:284` explains why the shared notifier threshold increases only
  at run time; `:325` does so. The count is `2*render + compute` at `:746`.
- `queue/compute.rs:94` allocates a preemption buffer per compute command and
  `:111` separate timestamp storage; `:337` carries its queue stamp and slot.
- `buffer.rs:684` retains the manager slot with active scenes; `:701` counts
  scenes and `:716` separately increments manager use. Compute does not acquire
  a render scene or buffer manager. An RP/C pair has only one render user.

Windows similarly has one compute queue/event 2
(`shared/src/apple_agx_render_shared_memory.c:1776`), queue-owned firmware stamp
at sidecar+0x8780 and driver stamp at compute-pointers+0x80. Its builder (`:807`)
reserves the per-flight word set; `:815` clears only those command words;
`:822`, `:853` deliberately share queue statistics/stamps/notifier.
`shared/include/apple_agx_render_shared_memory.h:58` separates sets A/B by 0x2000.
The two published flights retain separate words/preemption/timestamps, consistent
with Q13's dump. No same-word overwrite or manager rebind is demonstrated.

One difference: Asahi emits an explicit compute-on-prior-compute barrier as
well as the render barrier. Windows uses ring FIFO plus only the predecessor's
terminal barrier. The dependency is transitively sufficient for RP/C, but an
additional firmware protocol requirement has not been excluded. Another
difference is kick ordering (Windows compute/3D/TA, Linux fragment/vertex/compute);
Windows already uses its order for serial prelude jobs. Neither difference is
changed without a falsifiable checkpoint.

## Ranked hypotheses, with limits

1. **Compute stamp sequencing with two outstanding users** is the strongest
   concrete source difference to audit. Asahi `event.rs:58` increments by 0x100;
   `:84` computes deltas with a right shift of 8. `workqueue.rs:309` advances
   that value and `queue/compute.rs:339` emits its next value. Windows platform
   `:3265` and `:3549` instead use raw submission fences, copied unchanged into
   RunCompute (`shared/src/apple_agx_g13_compute_work.c:82`) and FinalizeCompute
   (`:152`). EXP2100 has adjacent 0x182a/0x182b users on event 2. Applying the
   Asahi delta arithmetic to those values gives zero; that is a source-derived
   warning, **not proof that proprietary firmware uses that comparison**.
   Prior serial compute and R/C tests do not qualify two outstanding compute
   commands. Do not silently change stamp units: every barrier, completion,
   recovery and wrap comparison would need a separate audited change.
2. **Prelude-specific command/data or queue protocol defect** remains possible:
   same queue now has a pending prelude plus a barriered successor, unlike the
   accepted no-prelude pairs. Candidate distinctions include explicit same-queue
   barrier semantics and shared queue metadata. The correctly directed barriers,
   separate command words and Asahi's supported mixed topology reduce the case
   for a simple self-deadlock or unconditional queue-slot collision. Missing
   crashlog/ring-tail evidence prevents selecting a particular faulty word.
3. **Premature shared notifier/event or job-storage reuse** is lower ranked:
   NotifierFlight publishes at commit, the builder reserves distinct sets,
   and Q13 shows both flights published with separate addresses. A stale cache
   publication or missed lifetime path is not disproven, but no overwrite is
   observed. It needs an exact failing field, not another generic sidecar split.
4. **Manager/scene ownership** is lower still for RP/C: the successor has no
   manager, and no-prelude R/R is hardware-proven. A workload-specific CDM/VA
   fault remains possible, but EXP2100 supplied no decoded firmware assertion
   that establishes one. No generic manager, memory or recovery rewrite here.

The confirmed software defect is overbroad admission of an unqualified topology.
The firmware assertion's root cause remains unknown. The gate is containment
of that specific admission defect, not a claim that the stamp hypothesis is fixed.

## Bounded later prelude probe

WHY THIS HYPOTHESIS:
1. The only supplied fatal pair has two compute-queue users (RP/C), while
   accepted R/C, R/R and C/R pairs lack preludes.
2. The two users carry adjacent raw compute stamp values, unlike Asahi's 0x100
   event sequencing. Queue-event reuse therefore has a concrete protocol
   difference to investigate before broader GravityMark opt-in.
3. Q13 shows distinct job words and a directed dependency chain, so a bounded
   nominal RP/C probe is more discriminating than speculative storage rewrites.

Propose a separate `Q3RPC` variant of the existing Q3RC workload (not implemented
or run here). Wrap its calibrated 5-10 ms ordinary render in a pipeline-statistics
query. The IA update source above supplies CDM prelude work; retain the green
pixel output and compute's 64 words `3*i+7`. End the query before submitting the
compute list; defer query resolve/readback until **after** both measured lists
are submitted and the bounded terminal wait. Check IAVertices/IAPrimitives
against the exact draw/instance counts as well as every output byte.

Warm the exact render/query/compute resources twice at depth one. Close measured
lists first; reset output sentinels; no CPU wait between Execute/Signal pairs.
Keep a 3 s final wait and a 15 s process watchdog. Bound the run to a small fixed
number of warmed pairs, record PID/QPC and KMD fence/stamp pairs, and stop on the
first fault. Do not force particular fence values or admit by observed sizes.

Run the same probe with Prelude=0 first: require typed reason 5 and correct
serial output. Its own later preregistered run changes only Prelude=1 on the
same reviewed package/mask. Require RP/C kinds 2/3, barrier 3, two published
flights/distinct sets, actual prelude timestamps/endpoint, valid 3D wait, exact
original reports once in order, exact query/pixel/compute outputs, no reset,
and later ordinary work/DWM. Missing RP kind, no overlap or saturated/foreign
receipts is INCONCLUSIVE. A correct ordinary R/C output is not a prelude proof.

This establishes or rejects the topology under a small nominal workload. A crash
cannot uniquely establish stamp encoding as cause without firmware assertion
or a separately audited one-variable stamp-sequence candidate. If the small
probe passes, the universal queue-sharing explanation loses probability; next
inspect the application prelude's contents/lifetime. If it fails, preserve v3
RTKit and exact command/event evidence, then audit the stamp contract before
another run. No third cdb round on the old EXP2100 dump.

## Default-gate GravityMark rerun order (integration owner only)

After review/native WDK /W4 /WX build, pin package/manifest/source/firmware and
accepted GPU-visible recovery hashes. Keep General=1 and the EXP2100 three-class
mask, ComputeOnCompute off, Prelude absent/0, and all drill arms off. Preserve
other accepted settings. Run bounded ordinary Q3 controls first.

GravityMark D3D12: HD 1280x720, 100000 asteroids, temporal 0, windowed,
ValidateRefs=1, Timeout=300 FIRST. Compare same-package depth-one control and
gated depth two. Require correct completion, reason-5 refusals for encountered
preludes, no admitted prelude decisions, ordinary admitted pairs, no unexpected
reset/removal, and healthy later work/DWM. Histogram counts are decision
observations, not unique jobs or saved milliseconds.

Only after HD passes run FHD 1920x1080 with the same settings. A bugcheck,
unexpected reset, incorrect output or missing gating evidence stops escalation.
Collect RcDecisions/progress/current, JobWindow/timing, reset/v3 PostResume,
EventDrain, ETL, application output and liveness, plus a dump on failure, before
exact package/devnode cleanup to ordinary GPU-visible Code28. Emergency-hidden
recovery remains only the existing fallback. The prelude opt-in probe is a
separate later run; successful gated GravityMark does not qualify Prelude=1 or
fatal-firmware recovery. No hardware, guest, builder or package action here.
