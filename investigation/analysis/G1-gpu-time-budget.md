# G1: GravityMark frame is GPU-execution bound (offline analysis, 2026-11-03)

Sources: EXP2110 UMD trace `gm2110a-umd.log` (HD 100k temporal 0, depth two, window 16, shared-load vectorization), measure-gpu-completion firmware start/end ticks (24 MHz); scores EXP2110/EXP2119/EXP2120 and macOS (HD 9739, FHD 7814).

## GPU-side gap before compute jobs
Compute jobs carry a valid firmware start. For 9319 render -> compute transitions of the GravityMark process, start(C) - end(previous R) on the firmware clock: median 0.013 ms, p10 -0.686 ms (overlap at depth two), p90 0.133 ms. The GPU is not idle between jobs; the earlier ~0.3-0.5 ms "launch" values of tools/agx_gpu_frame_breakdown.py are a model artefact (single-flight assumption, also noted by Codex c1). This explains why depth two (EXP2107-2121, up to ~4 overlapped successors per frame) does not shorten the frame.

## Pixel vs fixed cost (two-resolution linear model)
| | ours | macOS | ratio |
|---|---|---|---|
| HD frame | 27.9 ms | 17.2 ms | 1.62x |
| per pixel | 5.69 ns | 3.70 ns | 1.54x |
| HD pixel-dependent | 5.2 ms | 3.4 ms | +1.8 ms |
| resolution-independent | 22.7 ms | 13.8 ms | +8.9 ms |

~80% of the HD gap is resolution-independent GPU work (compute + geometry/TA). Largest item: the per-asteroid compute job C0 (~7.7 ms, EXP2110 budget), then TA of the first render (~2 ms) and compute jobs C2/C4/C6 (~3.6 ms).

## Consequence
Scheduling work (depth two, submission window, paging) is exhausted for GravityMark; the lever is GPU execution time of the compute kernels and geometry. Next: per-kernel split of C0 on the current code (EXP2076-2078 method) and comparison with Apple's compiled counterparts (k1 apple-dis).
