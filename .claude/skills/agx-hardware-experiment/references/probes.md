# Probes

A probe is a bash script that `run-probe.sh` runs once Windows answers SSH
after the experiment launch. Its first argument is the evidence directory;
`run-probe.sh` passes any further arguments through. Everything it prints
should also go to `<evidence>/probe.txt` (the helpers do this).

Before the probe, `run-probe.sh` records `state-start.txt`; after it,
`state-end.txt`, the final receipts (`rcpt-final/`), screenshots, the UMD
trace (`C:\ProgramData\EXP<N>-Trace\umd.log`), the launcher log and the
scanout snapshot count.

## Available probes

| Probe | Use it for | Duration | Main evidence |
| --- | --- | --- | --- |
| `selftests.sh` | Driver health without the desktop: no-layout self-test C/B/A, video self-test, WATCHDOG dump count | ~2 min | `probe.txt` exit codes, watchdog counts |
| `desktop-loads.sh [cycles]` | The desktop control (EXP1143 order). `cycles` > 1 is the soak (EXP1144: 4 cycles, ~24 min) | ~6 min per cycle + ~2 min selftests | `vsync-<load>[-cN].txt`, `rcpt-<load>/`, `procs-<load>.txt`, `settings-done*.txt`, `swap-timeline*.txt`, `events-after-loads.txt`, `pp-after.b64` |
| `icd-smoke.sh <icd dir> [mode] [frames]` | The x86 OpenGL ICD smoke test (`AGX_GL_SMOKE: PASS`), modes '' / nodepth / fbo / depthtest | ~2 min | `smoke-out.txt`, `smoke-done.txt`, `icd-umd.log`, dumps, `vsync-smoke.txt`, `icd-*-hashes.txt` |
| `desktop-then-icd.sh <icd dir> [mode]` | ICD change that must not regress the desktop | ~10 min | both of the above |
| `cpu-profile.sh` | Where DWM/driver time goes in typing and the same-size drag (EXP1145) | ~4 min | `cpu-typing.etl`, `cpu-resize.etl` + vsync/receipts |

Loads (in `desktop-loads.sh` order), each 30 s with a 42 s vsync trace and
receipts 20 s in:

1. `npmove` - Notepad position-only drag (SWP_NOSIZE), after `notepad-reset.ps1`
2. `nptype` - typing into that Notepad (SendKeys every 15 ms)
3. `exmove` - File Explorer position-only drag
4. `gdimove` - charmap position-only drag (a GDI window)
5. `gdiresize` - charmap "same-size" drag (SetWindowPos with size; win32k
   recreates the redirection bitmap every call, EXP1145)
6. `settings` - Settings open/close x3 with a 52 s vsync trace and the host-side
   DCP swap timeline from the launcher log

## Writing a new probe

Start from the closest existing probe and keep everything you are not
testing identical, so its numbers stay comparable with the baselines.
Building blocks in `lib.sh`:

- `upload_guest_tools` - loads, receipts, events, launcher, vsync tracer
- `run_load E name load.ps1 done.txt [vsync s] [settle s]`
- `profile_load E name load.ps1 done.txt etl-name`
- `snap_receipts E tag`, `guest_kill name...`, `post_vid E`, `guest_alive`
- `arm_interactive 'C:\...\script.ps1' ['positional args']` - anything that
  must draw on the user's desktop runs as a scheduled task in the interactive
  session; an SSH command has no desktop.
- `guest_timeout secs 'command'` - for anything that can hang with the GPU.

Rules that keep a probe safe and useful:

- Check `guest_alive` between phases and stop the probe (exit 0) if the guest
  is gone, so `run-probe.sh` still collects what exists.
- Never let a probe delete guest evidence; move or archive instead.
- New guest scripts go to `tools/agx-lab/guest/`, write their result to
  `$env:USERPROFILE\<name>-done.txt` and take positional parameters only.
- A new one-off probe for a single experiment may live in the experiment
  directory; promote it to `tools/agx-lab/probes/` when a second experiment
  needs it.
- Record the probe file and its order in the BEFORE entry.

## Application runs (games, real GL/D3D clients)

Pattern used for CS 1.6 (EXP1153-EXP1155): copy only the files that are absent
into the application directory, record the directory listing before and after,
remove exactly what was added afterwards, and kill the process after a fixed
timeout. Verify which `opengl32.dll`/ICD the process actually loaded (module
list) before attributing a result to the driver: EXP1155 found hl.exe on the
system GDI Generic OpenGL, not the private ICD. Sign-ins (Steam etc.) are the
user's; never enter credentials.
