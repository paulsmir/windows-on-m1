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
