# G2: next GravityMark target after EXP2124 (offline, 2026-11-03)

Closed levers (hardware): scheduling/depth two (G1, EXP2121), waits W1 (EXP2124: 5999 vs 5994),
K1 shared-load spacing (EXP2080), D3D buffer robustness (EXP2057, +0.35 %), per-dispatch
overhead (EXP2077). K3 rows 1/3 (shared-store vectorization, narrowed adjacency) are
estimated at 0.09 / 0.003 ms per frame, below the run-to-run noise (~1 %): not worth a run alone.

Largest unattributed item (K2 budget, EXP2110 HD): "render span before 3D" 6.28 ms/frame,
of which measured TA is ~3.0 ms (R1 0.23, R5 1.96, R7 0.77); ~3.3 ms is kick + prelude +
any compute->TA idle. G1 measured only render->compute gaps (median 0.013 ms);
compute->render gaps are unmeasured because render start stamps are stale on G13
(EXP1112: objects 28/30 written once; the TA work command's ts1 holds the end value).

Asahi contract (drivers/gpu/drm/asahi/queue/render.rs:1036-1070, 525-562): a Timestamp op
with flag true and update_ts = timestamp_pointers.start_addr before WaitForIdle(Vertex/Fragment),
then one with flag false and update_ts = end_addr after it.

Next: read the render template microsequence's Timestamp ops (template objects 28-31) against
that contract; if the start op names the end pointer or lacks the flag, a receipt-only fix gives
valid TA/3D starts and measures compute->TA idle. Hardware only if the template differs.

## Template Timestamp ops (offline dump, scratchpad tsdump.c over apple_agx_render_template.generated.c)
| microsequence | op | flag | command_time | ts_pointers | update_ts | work_queue |
|---|---|---|---|---|---|---|
| 3D (obj 15, arena 0x78000) | +0x1ac | 1 (start) | obj18+0x918 | obj18+0x920 | 0xdeadbeef | obj3 |
| 3D | +0x1ec | 0 (end) | obj18+0x918 | obj18+0x920 | 0xdeadbeef | obj3 |
| TA (obj 17, arena 0x88000) | +0x18c | 1 (start) | obj19+0x5b4 | obj19+0x5bc | 0xdeadbeef | obj6 |
| TA | +0x1cc | 0 (end) | obj19+0x5b4 | obj19+0x5bc | 0xdeadbeef | obj6 |

Difference from Asahi: Asahi sets update_ts to &timestamp_pointers.start_addr (start op) and
&timestamp_pointers.end_addr (end op); the captured macOS template leaves 0xdeadbeef in both
(the firmware evidently does not dereference it, or the job would fault). Both ops share one
command_time, which matches EXP1112 (ts1 = end value). Hypothesis (unverified): the G13 V13_5
firmware writes the start value through update_ts, so the start pointers stay stale.
Smallest checkpoint: receipt-only KMD patch setting update_ts as Asahi does for the TA pair only,
then check that TA start advances per job and lies between the previous job's end and TA end.
WHAT IS STILL UNKNOWN: whether update_ts is the field the firmware uses for the start write.
Recovery: the normal visible profile; no score change is expected (diagnostic only).

## Result (EXP2126)
Zeroing the TA start/end stamp objects per build revives TA starts (the firmware writes the start only into a zero slot).
GravityMark HD: compute -> TA idle median 0.695 ms per boundary, 8.2 % of GPU time (~2.5 ms/frame), although the render
was submitted ~1.8 ms before the compute ended. Next: kick-QPC split (EXP2127).

## EXP2129-2132: depth-two classes and the remaining boundary
- Prelude pairs (EXP2129): render -> compute idle 0.351 -> 0.065 ms; compute-on-compute (EXP2131): compute -> prelude start
  0.59 -> 0.063 ms, GravityMark HD 6242 (best). But with every second boundary paired, the boundary after each pair regressed
  to ~0.4 ms (EXP2131 render -> compute 0.398 ms).
- EXP2132 ring: within a pair the KMD sees Complete(N) only ~29 us before Complete(N+1): the notifier threshold (object 12)
  counts every kicked job, so N's event is deferred to N+1's end (U4, render_job_window.h). The worker's completion test is
  event-driven (AppleAgxG13ObserveQueue / AppleAgxG13CompletionSatisfiedWindow), so N leaves the window only with N+1 and
  N+2 can never be kicked behind N+1.

## Next design (offline, before any hardware run)
Goal: retire N from the window when its own completion stamp is written, kick N+2 behind the still-running N+1.
Questions to settle from Asahi/m1n1 source first:
1. Asahi event model: does Asahi get one event per stamp (event channel slot/value) even with the notifier threshold raised per
   job, i.e. is U4 an artefact of our notifier use? (queue/mod.rs, event.rs, channel.rs)
2. After FinalizeCompute/Finalize3D writes the stamp, which firmware ops still read the job's microsequence/work objects
   (RetireStamp, statistics, event signalling)? N+2 reuses N's slot copy (objects 13-19, 28-35, sidecar set).
3. If the stamp is not a safe release point, a third slot copy (depth-three object sets) instead of early retirement.
WINDOWS CONTRACT: dxgkrnl fences may be signalled per job as soon as the job is complete (DXGK_INTERRUPT_DMA_COMPLETED order
preserved). WHAT IS STILL UNKNOWN: items 1-2.

### Answer to question 1 (Asahi source)
Asahi queue/mod.rs:281-330: the notifier threshold is raised per job at run(); back-to-back jobs on one queue "are signalled
together" (case 3). U4 is inherent firmware behaviour, not our defect. Asahi does not suffer from it because drm_sched hands
the firmware every runnable job (no depth limit) and each job has its own freshly allocated firmware objects; it never reuses
a job's objects before that job's (possibly deferred) event. Consequence: early retirement on the stamp would go beyond the
Asahi contract (question 2 stays open and risky). The contract-preserving fix is a deeper window: a third copy of the
per-job objects (13-19, 28-35) and a third compute-sidecar job-word set, so N+2 can be kicked behind N+1 while N's deferred
event is still pending. Next: measure the free space below each slotted object in its 16 KiB page (slot_delta layout,
apple_agx_render_shared_memory.c) and the sidecar layout, then design ADMISSION_JOB_WINDOW_DEPTH 3 with RED/GREEN window
tests.

### Depth-three feasibility (offline, scratchpad slotfit.c over the template layouts)
- Slotted render objects: each sits at the end of its 16 KiB page; copies that fit below it: 13 BufferThing 227, 14 Barrier 512,
  15/17 microsequences 4, 16 InitBM 512, 18 WorkCommand3D 6, 19 WorkCommandTA 10, 28-35 timestamps 2048 -> a third copy fits
  (assuming the space below the second copy is unused; verify against the mapping/packing before implementation).
- Compute sidecar (object 39): set A ends at 0x9420, set B = +0x2000 inside the free range 0x9420-0xc000; gpu_buf at 0xc000.
  A set C does not fit: it needs its own allocation plus a job-owned 32 KiB preemption buffer (as set B).
- Slot choice is stamp parity today (job_slot: ((TaExpectedStamp >> 8) ^ 1) & 1) -> needs a slot index that cycles over three.
- Window: ADMISSION_JOB_WINDOW_DEPTH 2, KickRefusal requires InFlight == 1 -> InFlight <= 2 with per-pair barrier classes for
  N+2 against N+1, and flight/notifier accounting for three published jobs.
Estimated benefit: the ~0.4 ms unhidden boundary after each pair (EXP2131 render -> compute) ~4 % of GPU time.
Plan: (1) window state machine + RED/GREEN tests (pure C, render_job_window.c); (2) slot modulo three + layout test;
(3) sidecar set C allocation; (4) receipts; (5) one hardware gate behind a new switch (Wom1JobDepth3).

## EXP2134 state and the completion constraint (2026-11-03)
- Current idle: render -> compute boundaries after each pair ~0.40 ms x 4 per frame (EXP2131 budget), ~6 % of the frame.
- KMD part is small: prev pair Complete -> kick 76-233 us, build ~5 us (EXP2132 rings); the rest is firmware start.
- The notifier event of a pipelined job is deferred until all kicked jobs end (U4; Asahi signals back-to-back jobs
  together). Any deeper pipelining keeps the window non-empty and so suppresses events: completion would have to come
  from stamps/done pointers. rc95 samples: TA done ~300 us before Complete; TA/prelude stamps first seen only 20-40 us
  before Complete (also for pair heads) -> inconclusive (late stamp write vs no polling). Stopped after one offline pass
  (AGENTS anti-loop); the next step needs a receipt that polls the head's stamp at a fixed short period.
