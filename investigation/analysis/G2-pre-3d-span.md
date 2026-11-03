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
