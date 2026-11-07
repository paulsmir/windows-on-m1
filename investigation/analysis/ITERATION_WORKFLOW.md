# Iteration workflow: cost per stage and the FAST/PERF/FULL modes (2026-11-03)

## Measured cost of one KMD iteration (EXP2134, file mtimes, CEST)
| Stage | Interval | Time | Critical path |
|---|---|---|---|
| 1 source change/analysis | - | varies | yes |
| 2-3 snapshot + builder package (mkexp, X86B 97 s inside) | 16:11:21 -> 16:16:03 | 4 m 42 s | no: overlaps the previous recovery |
| 4-5 stage + ordered restart -> guest ready | 16:16:03 -> 16:19:20 | 3 m 17 s | yes |
| D3D12 suite | 16:19:22 -> 16:20:11 | 50 s | yes |
| 6-7 GravityMark HD (load 19.7 s + 167 s benchmark + copy) | 16:20:11 -> 16:23:51 | 3 m 40 s | yes |
| 8 KMD receipts | | 13 s | yes |
| 6-7 GravityMark FHD | 16:24:04 -> 16:27:26 | 3 m 22 s | yes |
| 8 final collection | | 20 s | yes |
| 9 analysis (UMD log, 290 MB: agx_gm_perf.py ~1 s) | | <1 min | yes |
| 10 recovery (two boots) | 16:27:49 -> 16:32:19 | 4 m 30 s | yes (before the next launch) |
Critical path code -> HD+FHD result: ~12 min after the package exists; ~16.5 min including recovery.
Three most expensive critical stages: GravityMark (7 m for HD+FHD), recovery (4.5 m), stage+restart (3.3 m).

## GravityMark facts (installed 1.89)
- Option table (from GravityMark.exe): device split alternate cuda screen width height position fulldesktop
  fullscreen vsync 10bit mode temporal multisample fps info sensors benchmark close report status count times
  stats image name asteroids lod raytracing query stereo panorama multiview. No frame-count or duration option.
- Score = frames of the ~167 s benchmark (EXP2134: 6257 points, 6258 frames); the camera path is time-based.
- MESA_SHADER_CACHE_DISABLE runs compile shaders in the first benchmark frames (gaps 0.5-9 s).

## Modes (tools/agx_gravitymark_run.ps1 -Mode, tools/agx_gm_perf.py)
| Mode | GravityMark | Analysis window | Use | Run time (expected) |
|---|---|---|---|---|
| FAST | same arguments, closed at 55 s | anchor + 5 s, 20 s | errors, TDR, fences, image, GPU metrics; no speed claim | ~1 m incl. copy |
| PERF | same arguments, closed at 100 s | anchor + 10 s, 60 s | BEFORE/AFTER, paired 8-block sign test, 1 % noise floor | ~1 m 50 s |
| FULL | unchanged official benchmark | whole run | score HD/FHD; required for defaults and final gains | ~3 m 40 s |
Offline fidelity of the PERF window (HD logs EXP2124/2126/2129/2131/2134): keeps the order of the full scores,
about 0.7x of their change; verdicts: 2126->2131 faster (-3.0 %, 7/8 blocks), 2131->2134 no-significant-change,
2126->2129 and 2129->2131 inconclusive (smaller changes), i.e. no false gain. FAST/PERF times are expected,
not yet measured on hardware.

## Change -> checks (tools/agx_iterate.py plan)
- UMD only: builder ARM64 UMD (umd-quick.sh) + umd-hotswap.sh onto the installed base package, no reboot.
- KMD/shared: package pipeline (mkexp in parallel with the previous recovery, launch-exp, recovery).
- m1n1: separate build, explicit user permission.
- tests/tools only: selected unit tests + the baseline suite; no hardware. docs: nothing.
- Every code change still runs tests/run_against_baseline.py before commit (name-based test selection misses
  behavioural tests such as test_d3d12_wait_global).

## Diagnostics cost
- UMD trace (APPLE_AGX_UMD_TRACE_SYNC=1, ~2 M lines / 290 MB per HD run): no score effect (EXP2119 no-trace
  control); its cost is the copy (~10 s per run) and disk. Keep it on for PERF/FAST (the analysis needs it).
- Wom1JobDiagnostics + live ring fetches: -0.4 to -2.3 % (EXP2128 5964 vs ~5990, EXP2130 5930 vs 6069, EXP2132
  6159 vs 6242): diagnostic runs only, never PERF/FULL arms.

## Remaining options (not implemented: they change rollback/install procedure and need a user decision)
- R54 series (AGENTS.md): for one preregistered hypothesis, replace the next hash-verified package in the same
  guest; saves the 4.5 min recovery per run.
- Switch-only arms (device values, same package) as a series: one ordered restart per arm instead of
  recovery + stage + restart (~7.5 min -> ~3 min).
