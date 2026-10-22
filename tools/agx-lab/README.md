# AGX lab tooling

Host-side scripts that take an AppleAgx driver change from a commit to a
hardware verdict on the J313 (MacBook Air M1) Windows guest, and the analysis
tools for the evidence they collect. They grew out of experiments EXP1092 to
EXP1155 and keep those experiments' exact guest commands and timings, so new
runs stay comparable with the recorded baselines.

The procedure around them (ledger entries, hypothesis gate, rollback,
acceptance criteria) is in `.claude/skills/agx-hardware-experiment/SKILL.md`.

## Configuration

Nothing host-specific is tracked. Copy `lab.env.example` to
`<main checkout>/.local/agx-lab.env` (or set `AGX_LAB_ENV`) and fill in the
guest, builder, serial ports and local paths. `lib.sh` finds the main
checkout through `git rev-parse --git-common-dir`, so the tools work from any
worktree. The scripts target macOS `/bin/bash` 3.2.

## Layout

| Path | Role |
| --- | --- |
| `lib.sh` | Config loading and helpers: `guest`, `guest_put/get`, `builder`, `wait_proxy_ready`, `wait_guest_up`, `snap_receipts`, `run_load`, `profile_load`, `post_vid` |
| `experiment/package_source.py` | Freeze `drivers/apple-agx/**` of a commit: `source.zip`, `source-manifest.json`, `build-kmd.ps1` |
| `experiment/builder.sh` | `build` (upload and build), `copy` (reuse an unchanged package), `kill` |
| `experiment/prepare.sh` | Fetch and verify the package, clone the previous experiment's scripts, manifests, upload, hash check, stage, verify-launch, recovery scripts |
| `experiment/clone_experiment.py`, `write_manifests.py` | Used by `prepare.sh` |
| `experiment/run-probe.sh` | Ordered restart, launch, probe, collect (no rollback) |
| `probes/*.sh` | What runs on the live guest; each takes the evidence directory first |
| `guest/*.ps1` | Guest-side loads, receipts, events, interactive launcher, ETW profile |
| `builder/*.ps1` | ETL decode (xperf) and dump stacks (cdb) on the builder |
| `analysis/*.py` | Decoders and summaries for vsync traces, receipts and ETW CSVs |
| `proxy_nop.py` | m1n1 proxy liveness check ("Running proxy") |

Per-experiment launch and recovery scripts (`boot<N>.ps1`, `stage<N>.ps1`,
`ordered-restart<N>.ps1`, `full-owner<N>.sh`, `verify-launch<N>.py`,
`cleanup<N>/`, `evidence/rec<N>h/recover.sh`) live in the private experiment
directory and are derived from the previous experiment by `prepare.sh`.

## One experiment

```bash
L=tools/agx-lab
# 1. Package: a new build from the committed source ...
python3 $L/experiment/package_source.py EXP1156 EXP1156-short-name 1156 --repo "$PWD"
bash $L/experiment/builder.sh build EXP1156-short-name        # long; run in the background
#    ... or reuse an unchanged package from the builder:
bash $L/experiment/builder.sh copy EXP1155-previous EXP1156-short-name
# 2. Stage (prints receipt hashes, HASHES_MATCH, the verify-launch PASS line):
bash $L/experiment/prepare.sh 1155 EXP1155-previous 1156 EXP1156-short-name "purpose"
# 3. Run (background; follow <experiment>/evidence/EXP1156/probe.txt):
bash $L/experiment/run-probe.sh EXP1156-short-name 1156 EXP1156 $L/probes/desktop-loads.sh
# 4. Roll back (always, also after a failure):
bash "$AGX_EXPERIMENTS/EXP1156-short-name/evidence/rec1156h/recover.sh"
```

A probe is any bash script whose first argument is the evidence directory;
`run-probe.sh` passes its remaining arguments through. Compose the building
blocks (`probes/desktop-then-icd.sh` is an example). Run the desktop control
(`desktop-loads.sh`) before any other GPU client: EXP1150 to EXP1152 showed
that the probe order changes the typing and charmap pacing.

## Evidence conventions

- Guest done files are UTF-16LE (`Out-File`); read them with `utf16_cat`.
- `rcpt-<tag>/` holds the KMD registry receipts (`Wom1*.bin`) at that point.
- `vsync-<load>.txt` are AppleAgxVsyncTrace logs: `t=` in 100 ns units,
  `k=3` latch events, `as=` flip counter.
- `analysis/latch_pacing.py vsync-*.txt` gives latches/s and the share of
  one-period intervals; `analysis/vsync_fps.py` gives flips per 10 s window.
- ETW: `profile_load` captures `guest/cswitch.wprp!AgxWait`; decode on the
  builder with `builder/xperf-dump.ps1`, find the composition thread with
  `analysis/dwm_thread.py`, then `dwm_frames.py`, `dwm_slow_frames.py`,
  `dwm_slow_kernel.py`, `dwm_switch_outs.py`, `dwm_readier.py`,
  `thread_waits.py`, `cswitch_threads.py`.
- Receipt decoders: `job_timing.py` (Wom1JobTiming971), `submit_path.py`
  (Wom1SubmitPath1135), `display_ring.py` (Wom1DisplayRing1131),
  `private_procs.py` (Wom1G3PrivateProcs), `soak_summary.py` (a soak's
  evidence directory), `g4_phases.py` (UMD trace `measure-g4-phase` lines).

## Real applications (EXP1876G)

- `guest/realapp/game-run.ps1 -Dir <guest dir> -TraceDir <dir> -Tag <t> -ProcName <p> (-Exe <path> | -SteamApp <id>)`:
  launch, liveness/working set/screenshot ticks, CloseMainWindow, Application Error/Hang events, `game-<tag>.json`.
- `guest/realapp/trace-env-on.ps1 -TraceFile <path> [-SteamApp <id>]` / `trace-env-off.ps1`: user-level
  `APPLE_AGX_UMD_TRACE_FILE` for processes Steam/3DMark start (Steam is restarted to inherit it); always remove after.
- `experiment/guest-ui.sh shot OUT | click X Y | wheel X Y DELTA`: DPI-aware screenshots and mouse input in the
  interactive session (physical 2560x1600 coordinates; screenshots are downscaled to 1600 px, multiply by 1.6).
- 3DMark must be started through Steam (`steam.exe -applaunch 223850`); its workloads take a JSON `--in` that the UI
  generates. Night Raid: Benchmarks > Night Raid > Custom run, "Enable window mode" until the KMD offers display modes
  beyond A8R8G8B8.
