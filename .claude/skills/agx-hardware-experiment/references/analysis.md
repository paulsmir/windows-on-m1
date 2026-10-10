# Analysis

All tools are in `tools/agx-lab/analysis/` and read the evidence directory
`$AGX_EXPERIMENTS/<dir>/evidence/EXP<N>/`. Compare every number with a named
baseline experiment and say which one; a number without a baseline is not a
result.

## First pass (every run)

1. `state-start.txt` / `state-end.txt`: line 1 is the APPL0002 problem code
   (0 = started; 28 = staged but not started; 43/10 = failed), then DWM and
   explorer pids, then uptime in seconds. A different DWM pid at the end means
   DWM restarted (device removed/TDR); an uptime shorter than the run means
   the guest rebooted.
2. `probe.txt`: self-test exit codes, watchdog counts (an increase = a
   WATCHDOG dump = a TDR or hang), load done lines (`moves`/`typed` counts
   show the load actually ran), `GUEST_DOWN_*` markers.
3. UMD trace: `grep -a '^reject-' umd.log | cut -d' ' -f1 | sort | uniq -c`
   (run-probe prints it). Any reject or `device-terminal`/`runtime-set-error`
   line is a finding.
4. Receipts: `rcpt-final.txt` lists which `Wom1*` values exist. Failure
   receipts (`Wom1G4SubmitFailure*`, `Wom1G3PagingFailure`,
   `Wom1G3PrivateFailure`, `Wom1G3Poison`, `Wom1G3CopyQueryFailure`) should be
   absent; their presence names the failing contract (branch, status, sizes).
5. `events-after-loads.txt` (or run `guest/events.ps1`): unexpected shutdown,
   bugcheck, LiveKernelEvent 141/117 (TDR), new dumps.

## Desktop smoothness

```bash
A=tools/agx-lab/analysis; E=<evidence dir>
for f in $E/vsync-*.txt; do echo "$(basename $f): $(python3 $A/vsync_fps.py $f)"; done
python3 $A/latch_pacing.py $E/vsync-*.txt
python3 $A/soak_summary.py $E          # soak runs (CYCLES > 1)
```

Report each load as `<min>-<max> flips/s over the steady 10 s windows / <1p %>`
one-vblank latch share. Two-period (2p) intervals are visible stutter.

Baselines (best validated stack; same probe order):

| Load | EXP1143 | EXP1144 soak (4 cycles) | EXP1152 (PACKAGE1151) |
| --- | --- | --- | --- |
| Notepad drag | 57.3-59.1 / 97.1 % | 60.0-60.1 / 97.7-98.8 % | 59.1-60.0 / 98.4 % |
| Notepad typing | 57.1-58.2 / 95.6 % | 57.4-58.2 / 93.6-94.4 % | 57.1-57.5 / 94.9 % |
| Explorer drag | 55.4-60.0 | 60.1 / 98.9-99.1 % | 54.6-60.0 |
| charmap drag | 59.6-60.0 / 99.2 % | 60.1 / 98.5-99.3 % | 59.9-60.0 / 99.3 % |
| charmap same-size drag | 55.9-57.6 / 93.8 % | 56.5-57.8 / 91.2-93.8 % | 57.3-58.1 / 95.2 % |

Known non-driver behaviour: the same-size drag's extra two-period frames are
win32k recreating the redirection bitmap on every SetWindowPos with a size
(EXP1145); do not chase them in AppleAgx. A changed probe order moves typing
and charmap pacing by several points (EXP1150-EXP1152); re-run the canonical
order before blaming a package.

Soak health (EXP1144): DWM handles ~1000 and private memory ~160 MB flat
across cycles, private-pool pressure 0, misses < 0.5 %, max AwaitWork wait
< 2 ms. Growth across cycles is a leak candidate.

## OpenGL ICD

`smoke-out.txt` must end in `AGX_GL_SMOKE: PASS` with correct frame 0 / last
frame pixel checks and `glGetError 0`; 300 frames take ~5.0 s (60 fps) in
EXP1149 (fbo, 59.9 fps) and EXP1151 (depth, 60.0 fps). `icd-umd.log` must have
no `reject-` lines. A missing `smoke-done.txt` with a live process is often a
modal assert dialog, not a GPU hang: fetch `agx_gl_smoke*.dmp` and read the
stack on the builder with `builder/cdb-stack.ps1 -Dump <dmp> -SymbolDirs <ICD build dirs>`.
`Wom1G4SubmitFailure` names the refused G4 job (branch, status, DMA size).

## Receipt decoders

| Receipt (`rcpt-<tag>/`) | Tool | Shows |
| --- | --- | --- |
| `Wom1JobTiming971.bin` | `job_timing.py` | submit -> worker -> kick -> complete -> notify latencies, firmware TA/3D ticks |
| `Wom1SubmitPath1135.bin` | `submit_path.py` | immediate/deferred submits, binds, waits by reason, max wait |
| `Wom1DisplayRing1131.bin` | `display_ring.py` (+ `settings-done.txt`) | flip timeline by kind with gaps; Settings open/close marks |
| `Wom1G3PrivateProcs.bin` | `private_procs.py` | per-process private-pool units, scenes, overflow |
| `Wom1G3PagingProfile.bin` / `pp-after.b64` | `soak_summary.py` (last 16 u64) | private-pool hit/miss/trim/pressure/mapped/unmapped/reap |
| UMD trace `measure-g4-phase` | `g4_phases.py umd.log` | G4 phase durations |

When a decoder's layout no longer matches (version field), read the
producing structure in the KMD source (`drivers/apple-agx/render-admission`)
and update the decoder in the same change.

## ETW profile (where the time goes)

1. Capture with `probes/cpu-profile.sh` (or `profile_load` in a custom probe).
   Stack-walking capture perturbs light loads: EXP1145's typing capture cut
   typing to ~25 latches/s and was inconclusive. Prefer loads that keep their
   rate under capture, or compare against an uncaptured run.
2. Copy the ETL to the builder and decode:
   `builder/xperf-dump.ps1 -Etl C:\...\cpu-resize.etl -SymbolDirs <KMD/UMD PDB dirs>`.
3. On the host:
   ```bash
   python3 $A/dwm_thread.py cpu.csv                 # composition thread tid
   python3 $A/dwm_frames.py cpu.csv <tid>           # frames, busy median/p90, waits per frame by category
   python3 $A/dwm_slow_frames.py cpu.csv <tid>      # what slow (>= 16 ms) frames wait on
   python3 $A/dwm_slow_kernel.py cpu.csv <tid>      # kernel-only waits
   python3 $A/dwm_switch_outs.py cpu.csv <tid>      # off-CPU by state/reason/preemptor
   python3 $A/dwm_readier.py cpu.csv <tid>          # who wakes DWM after WrResource
   python3 $A/thread_waits.py cpu.csv <tid> [depth] # any thread's waits by stack
   python3 $A/cswitch_threads.py cpu.csv <process>  # run/wait split per thread of a process
   ```
   Reference (EXP1145 same-size drag): 899 frames, busy median 9.54 ms, p90
   14.62 ms; per frame 6.47 ms running, retire waits 2.31 ms, sync 0.51 ms.
   A slow-frame wait without an AppleAgx frame on its stack is not the
   driver's.

## Kernel crashes and TDRs

Bugcheck/LiveKernelEvent dumps: list them with `guest/events.ps1`, copy them
off the guest before recovery, and analyse on the builder with the KMD PDB.
Record the stop code and the four parameters in the ACTUAL line.
