# Bench harness helpers (runtime copies live in .local/scratchpad-tools-backup)

Snapshot of the bench-side helpers that wait on measured guest runs, kept in the
repository so harness changes are reviewable and ledgered:

- `gm-repeat.sh` -- GravityMark HD PERF repeats (w / r2 / r3); waits on `<tag>-done.txt`.
- `cs-bench-run.sh` -- Counter-Strike 1.6 timedemo bench; waits on the `end ` line of `cs-bench.log`.
- `quiet-gate.sh` -- quiet-guest gate before the first arm and before every FULL arm.

Rule since EXP2338 (2026-11-11): no PowerShell process is started on the guest between the
start and the done file of a measured run. In-run waits poll every 5 s with
`cmd /c if exist <file> echo True` (or `cmd /c findstr /b /c:"end " <log>`). EXP2338 showed
that a PowerShell start per poll stalls the benchmark's render thread (23 long frames on the
~6.7 s poll grid, about 1 % of the FULL score); the cmd form and no-poll arms showed none.
Probe-local arm()/full() loops of each experiment follow the same rule from EXP2339 on.
