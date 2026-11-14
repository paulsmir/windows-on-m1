# Claude working method (D3D12 phase, written 2026-10-18 before the Codex handoff)

Purpose: after the usage reset, resume exactly the way the D3D12 work ran
on 2026-10-17. Read this, then investigation/GPU_CURRENT_STATE.md, then the
newest EXPERIMENTS.md entries, then what Codex did since (git log, CHANGES.csv).

## Loop per feature (C-step)

1. Pick the next fail-closed DDI that real applications hit (rejected-DDI
   list: grep `AGX_REJECT(` in drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp).
2. Primary sources first: WDK 26100 d3d12umddi.h
   (.local/reference/wdk26100), winsdk d3dkmdt.h, Mesa asahi
   (.local/reference/mesa/src/gallium/drivers/asahi), the repo's D3D11
   path (mesa/d3d11/agx_d3d11_ddi.cpp) as the proven Gallium translation.
   Check every Gallium entry called is assigned by Asahi.
3. Design note investigation/analysis/D3D12-C<n>-*.md with WINDOWS
   CONTRACT / AGX/ASAHI CONTRACT / TRANSLATION / WHAT IS STILL UNKNOWN /
   Checkpoint.
4. Implement: DDI side (agx_d3d12_ddi.cpp: record into AgxD3d12Command,
   replay in ExecuteCommandLists) + owner side (mesa/winsys/agx_d3d12_windows.{h,cpp}).
   Rules: no u_format.h inline helpers in the UMD; keep replay frames small
   (STACK_FRAMES gate 16384); descriptors are CPU records; CPU descriptors
   read at record time, table descriptors at execution.
5. Tests: tests/test_d3d12_abi_gate.py + tests/test_d3d12_native_owner.py
   (run from tests/: `python3 -m unittest test_d3d12_abi_gate test_d3d12_native_owner`).
   Add a test only for a real bug or deterministic invariant.
6. Compile gate on the builder (B=pauls@192.168.1.24, K=~/.ssh/windows_builder;
   in zsh quote "${B}:path"):
   scp d3d12/agx_d3d12_ddi.cpp -> syncheck-d3d12/d3d12/, winsys agx_d3d12_windows.{cpp,h}
   -> syncheck-d3d12/mesa/winsys/, then
   `ssh $B 'cd C:\Users\pauls\syncheck-d3d12 && powershell -NoProfile -ExecutionPolicy Bypass -File syncheck-d3d12.ps1'`
   (3 x rc=0 required).
7. Probe step in drivers/apple-agx/windows/icd/agx_d3d12_probe.c, run on a
   fresh device via run_isolated, own AGX_D3D12_<step>: PASS/FAIL lines.
   Shaders: fxc on builder (`"C:\Program Files (x86)\Windows Kits\10\bin\10.0.26100.0\x64\fxc.exe" /T cs_5_0 ...`),
   DXIL shaders: the SDK dxc (`...\10.0.26100.0\x64\dxc.exe -T ps_6_0 -E main -Vn g_<name> -Fh`).
   Build probe: `bash $S/build-d3d12probe.sh NNNN` -- copies the CURRENT
   probe.c, probe headers and dxbc_tir.h into a fresh builder dir, builds
   arm64/x64/x86 and fetches agx_d3d12_probe_eNNNN_<arch>.exe hash-checked.
   (The old per-arch scripts compiled stale copies and silently lost includes.)
   zsh: always write `"${B}:path"` -- `"$B:a..."` applies the :a modifier.
8. Commit code (no attribution trailers), then CHANGES.csv row via python
   csv writer (CRLF, 40-char commit, UTC stamps from `date -u`, status
   implemented/validated/rejected), gate commits on `grep -q '^OK'`.

## Hardware experiment pipeline (scratchpad scripts)

Scratchpad: /private/tmp/claude-502/-Volumes-pwdev-public-windows/8c9ead37-29d6-4c63-9bb0-35c6eb850f40/scratchpad
(backups: .local/scratchpad-tools-backup, tools/agx-lab).

- Package pipeline (current): `bash $S/mkexp.sh <prevN> <prevDir> <N> <dir> X86B<k> X86B<k+1>`
  snapshots HEAD (package_source.py), derives run<N>.ps1/x86b<k+1>.ps1 with
  the new pins, uploads and starts chain<N>.sh (x86 Umd32 then package);
  then `bash $S/fetch-package.sh <dir>` -> FETCH_PASS, then prepare.sh,
  agx-install-visible-recovery.py, BEFORE, run<N>.sh. Copy probe<N>.sh and
  guest/ (rename *<prev>* files, sed EXP<prev> in .ps1 only) before prepare.
- buildNNNN.sh  (sed from previous: EXP dir name + number) -> mkicd snapshot of
  HEAD + build-kmd on builder; wait BUILDNNNN_DONE, require
  "PACKAGENNNN BUILD PASS" and both STACK_FRAMES PASS. No commits of code
  between build start and the mkicd snapshot line (first line = HEAD hash).
- probe binary: scp builder d3d12probe-arm64/agx_d3d12_probe.exe ->
  scratchpad agx_d3d12_probe_eNNNN_arm64.exe; newexp-kmdNNNN.sh = previous
  with the probe name replaced.
- beforeNNNN.txt: hypothesis, WHY THIS HYPOTHESIS, ATOMIC CONTRACT, WINDOWS
  CONTRACT, AGX/ASAHI CONTRACT, TRANSLATION, WHAT IS STILL UNKNOWN,
  Expected, Failure criteria, Recovery, Evidence. newexp appends it to
  EXPERIMENTS.md and commits "Record EXPprev rollback and EXPNNNN plan" --
  do not touch EXPERIMENTS.md until that commit appears.
- chainNNNN.sh waits for recPREV RECOVERY_DONE then runs newexp + guest-watchdog;
  runNNNN.sh waits for the build gate then runs the chain (background).
- Monitor tail of build/rec/chain/newexp/watchdog logs for
  BUILD PASS|RECOVERY_DONE|EXP_START|TOOL_d3d12-on_END|NEWEXP_DONE|ABORT|SMOKE: FAIL|...
- After TOOL_d3d12-on_END: read evidence/EXPNNNN/tool-d3d12-on-std.txt
  (AGX_D3D12_* lines) and tool-d3d12-on-umd.log receipts.
- After NEWEXP_DONE: check regressions (73 SMOKE PASS + known MSAA-a000r
  FAIL, GL ~52 fps, bench ~59.6 fps, corpus createfail 64/4, DWM faults 0),
  run evidence/recNNNNh/recover.sh directly to RECOVERY_DONE: it orders the
  restart itself. Never `shutdown /r` first: the reset returns the air to
  Running proxy, run_uefi exits, and recover.sh loses the owner (EXP1879,
  again EXP1907 2026-10-23); from proxy use AGX_RECOVERY_ALLOW_AT_PROXY=1.
- Record EXPNNNN ACTUAL (UTC) + CHANGES rows (validated with hardware
  result); never edit old results -- append corrections.
- Guest hang (GUEST_WATCHDOG_ABORT / no SSH): stop the monitor, kill the
  experiment's run_uefi, run recNNNNh/recover.sh directly, rerun as a new ID.
- 15-minute hang check from the user: pgrep pipeline, log mtime < 15 min,
  airssh 'echo up'; silent if healthy.

## Debugging habits that paid off

- Read receipts first (measure-d3d12-rejected {table, offset} -> map the
  offset to the DDI member with the WDK struct; measure-d3d12-* reason codes).
- When a whole class fails after a change, `git diff <good> <bad>` the
  function on the failing path (EXP1373 shader-image bind, EXP1374
  unzeroed command slots).
- Probe bugs vs driver bugs: check D3D12 API rules (copy-list states,
  SM5 PS UAV register >= RT count, one active occlusion query in Asahi).
- Report per EXP in Russian chat; plan/stage note before tasks.

## User-authorized throughput update (2026-10-18)

The user approved supervisor guidance: independent changes may share one
experiment when each has a fresh-device probe step, its own
AGX_D3D12_<step>: PASS/FAIL and receipts, and BEFORE explicitly records
`ATOMIC CONTRACT: none -- separate steps ...`. Dependent changes remain one
coherent change per experiment. A failed independent step does not block
recording the others. While hardware runs, implement/offline-test/builder-check
and prebuild the next candidate after its snapshot is pinned. Prepare for the
next install immediately after exact rollback reports RECOVERY_DONE.

Investigate/fix the known KMD private-scene-pool exhaustion in parallel with
D3D12; do not hide it in the UMD or silently increase capacity. Give it a real
regression and a dedicated hardware checkpoint/receipt. Keep shared Git commits,
package snapshots, ledgers and the hardware launcher under one coordinator.

Performance correction: the ~59.6fps regression reference in older summaries
is the internal GL2400-frame benchmark, not the CS timedemo. EXP1377 CS did not
launch (BENCH_COPY_FAIL); EXP1378 post-harness CS44.784fps and EXP1379 early
CS46.391fps are separate measurements. Preserve that distinction.

## Ordinary recovery update after EXP1380

After prepare generates recNNNNh/recover.sh, run
`python3 scripts/agx-install-visible-recovery.py "$N" "$X" --worktree "$W"`
before hardware BEFORE. Append its visible-recovery-source.json hashes to BEFORE.
The factory preserves the original emergency hidden helper without overwriting it.
The same recover.sh entry now calls the tracked GPU-visible rollback: exact
identity/disarmed guard, service Start4 and durable receipt, ordered normal boot,
Code32/serviceStopped, exact package removal, durable restart and normal
Code28/PackageAbsent/noService/8CPU preflight. Source lifecycle consumes arms by
deleting them; proven absence and explicit DWORD0 are distinct safe states.
Keep all guest commands inside the active recovery controller. Cleanup may order
a restart after an SSH timeout/EOF; require owned reset plus durable preflight,
and never repeat deletion solely because the client timed out. Emergency hidden
boot is reserved for a GPU-visible guest that cannot be recovered.

EXP1380 guard verification: package affected45/full1509 applies to driver changes;
recovery guard80e35fbb separately passed11 native PowerShell cases and actual
Code32/serviceStopped -> Code28/PackageAbsent transition.

Recovery template generation: prepare.sh must select the previous original with
`agx-install-visible-recovery.py PREVIOUS_DIR PREVIOUS_ID --legacy-template-only`;
never apply old waitcycle text patches to an installed visible wrapper. Repeat the
normal factory invocation immediately before recovery to verify recorded current
host source hashes idempotently. If preparation stops after stage, only resume
before first hardware launch after verifying all package hashes, manifest, same
boot event, Code28, no live binding and expected arm; never repeat installation
blindly. Preserve stopped logs and preregister the continuation.

Process review 2026-10-23 (EXP1887/EXP1888):
- Commit code only through the scratchpad `gated-commit.sh` (BASELINE_OK);
  `test | tail && git commit` let a broken test_dxil_to_nir in. A new source
  file a fixture links goes into every suite that builds that fixture.
- `prepare.sh` copies the experiment's `guest/` helpers (hash check, DXGI
  modes probe, sample wrappers) and prints GUEST_HELPERS_STAGED; EXP1887
  ran without them (empty hash gate, no DXGI modes).
- Two sessions share this worktree: baseline failures in files the other
  session has uncommitted are not regressions of the change at hand; run the
  suites of the touched layer (D3D10/11: test_d3d10*/test_d3d11*/
  test_wddm2*/test_dxbc*/test_dxil_to_nir) and tell the other session.
- The X86B closure is the first full compile of D3D10/11 UMD C++; check
  pinned-Mesa Gallium signatures (set_constant_buffer has no
  take_ownership here) before packaging.
- Asahi advertises PIPE_MAX_SHADER_IMAGES (64) but keeps image_mask in
  32 bits: never bind image slots above 31.

## UMD crash triage without a PDB (ARM64)

- The probe prints `PROBE_CRASH_MODULE <dll>+<offset> data <address>`;
  in-process MiniDumpWriteDump fails (ERROR_NOACCESS) on the guest.
- Disassemble the packaged DLL at ImageBase+offset (`llvm-objdump -d
  --start-address`), take 16 bytes of the faulting instructions and search
  them in the build's objects (`$S/findbytes.ps1` on the builder:
  `C:\Users\pauls\<EXP>\native-arm64\objects`), then `llvm-objdump -dr`
  the hit object: the enclosing symbol is the function. EXP1891B found
  Asahi agx_update_vs this way in minutes.
- The x64 UMD has a PDB (umd-x64); reproduce on x64 when a stack is needed.

## Experiments whose variable is the m1n1

- prepare.sh takes `AGX_M1N1=<macho>`; the at-proxy recovery reboots the
  experiment profile, i.e. the same m1n1. If that m1n1 cannot boot Windows
  (EXP1894), recovery hangs at AT_PROXY_SSH_NOT_UP.
- prepare.sh with `AGX_M1N1` now writes the lastgood pair itself (`mk-lastgood.sh`, default lastgood m1n1 21c3648d, override `AGX_M1N1_LASTGOOD`); `probe-lint.sh <probe>` checks every referenced scratchpad binary exists (a renumbering once produced nonexistent probe names).
- Before such a run, keep `full-owner<N>.sh.lastgood` + a manifest copy
  naming the last good m1n1; on a boot failure SIGTERM run_uefi (snapshot +
  Air reboot), swap them in (update the full-owner hash in
  hardware-manifest.json, `verify-launch<N>.py` must pass) and run
  `AGX_RECOVERY_ALLOW_AT_PROXY=1 recover.sh`.
- A hung guest: two `kill -INT <run_uefi>` HV WATCHDOG snapshots ~25 s apart
  before any recovery (per-CPU pc/spsr/LRs; the vGIC evidence).

## Real-app TDR triage and UI driving (2026-10-23, EXP1899N)

- After a real application ends with DEVICE_REMOVED and the UMD trace shows no refusal, read the KMD before anything else. Run `bash $S/kmd-receipts.sh <evidence> <tag>`. It dumps every APPL0002 Device Parameters value and decodes:
  - `Wom1GpuReset`: engine/adapter reset, SchedulerFaulted file tag and line, packet and fence state;
  - `Wom1PreemptRefusal`: the scheduler at the first refused boundary preemption.
- EXP1899N GT2 was found this way: TDR after `scheduler_windows.c:399`.
- Run it before recovery, because recovery removes the package and its values.
- Drive 3DMark and other UIs with `$S/guest-ui.sh shot|click|wheel|ps`:
  - physical 2560x1600 coordinates = screenshot coordinates x 1.6;
  - it uses `C:\Users\pavel\realapp` helpers, since guest pruning removed the EXP1876 copies.
- Both scripts are backed up in `.local/scratchpad-tools-backup`.
- 3DMark resets Custom-run toggles between sessions, so check every toggle on a screenshot before RUN CUSTOM.
- New experiment directory:
  - `prepare.sh` now calls `$S/clone-exp-scripts.sh <prevN> <prevDir> <N> <dir>`.
  - It carries `guest/` (renumbered `hashcheck<N>.ps1` and `sample-run<N>.ps1`), `run<N>.sh` and, only when absent, `probe<N>.sh`.
  - So edit `probe<N>.sh` before `prepare.sh` runs, and your edits are kept.
- Querying guest logs: PowerShell regexes with `|` break inside the airssh/cmd quoting. Use the realapp helpers instead:
  - `tgrep.ps1 -File -Pattern -Last`;
  - `tq.ps1 -Pid2` (per-process refusals, and the lines before the first throw);
  - `appevents.ps1 -Minutes` (WER and Application Error).
- `guest-ui.sh shot` deletes the old picture and reports SHOT_FAILED (and resets the tasks with `ui-reset.ps1`) rather than returning a stale frame.
- The worktree is shared with the tiled/min-max session, which may have unfinished hunks in the same files. To commit only your change:
  1. Tag it with a unique comment marker (e.g. `C68 (`).
  2. `bash $S/own-hunks.sh /tmp/x.patch "<marker>" <files>` extracts only those hunks.
  3. `bash $S/clean-syncheck.sh /tmp/x.patch` compiles git HEAD plus exactly those hunks; the plain syncheck copies the working tree and fails on the other session's hunks.
  4. `git apply --cached /tmp/x.patch` puts only those hunks in the index, then commit from the index.
  5. Tell the other session which hunk of the shared file you committed.
- When commits land after mkexp and before install, run `bash $S/rebuild-exp.sh <prevN> <prevDir> <N> <oldDir> <newDir> <prevX86B> <newX86B>`.
  - It stops the chain, moves the old directory to `-superseded`, carries `probe<N>.sh`, sets the logs aside, prints the driver commits it adds, and reruns mkexp.
  - In the BEFORE entry, note the superseded build as "never installed".

## Offline work through Codex (2026-10-23)

- Briefs live in `.local/handoff/codex/README.md` and `<task>/TASK.md`. Each task gets its own worktree, `.worktrees/codex-<task>`, on branch `codex/<task>`.
- Launch one Codex per task in the background: `scratchpad/codex-run.sh <task>`.
  - It runs `codex exec -m gpt-6-astra -c model_reasoning_effort=high -s workspace-write`.
  - It overrides `writable_roots` with canonical `/Volumes/pwdev` paths: the user's config lists `$HOME/public_windows/.local`, a symlink, and Codex refuses every command with it.
  - Never edit `~/.codex/config.toml`.
- At every experiment boundary, check `scratchpad/codex-status.sh` (REPORT.md, commits ahead). Then review each branch diff, rebase or cherry-pick it onto the integration branch, run `run_against_baseline.py` (BASELINE_OK), add the CHANGES rows, and validate on hardware.
- Pipeline waiters: use `scratchpad/wait-log.sh <log> '^NAME '`. It refuses unanchored patterns: EXP1907's `PROBE_DONE` matched `AGX_D3D11_FL111_PROBE_DONE`.
- Pre-merge gate: `scratchpad/codex-review.sh <task>`. It:
  - skips commits already applied (`git cherry`);
  - flags commits that touch `known_failures.txt` or the ledgers, or carry attribution (T4 tried to hide fresh-worktree failures in known_failures);
  - trial-cherry-picks the clean commits into `.worktrees/codex-review-<pid>`, which must sit under `.worktrees/` because the tests resolve `../../.local/reference`, and runs the baseline there.
  - Only the two fresh-worktree environment failures are tolerated (texture1d fxc pin, m1n1 broker replay). Merge only after REVIEW_OK.
- Bench throughput: `scratchpad/chain-next.sh <cur> <curDir> <next> <nextDir> <before.txt>`. It:
  1. waits for `^RUN_DONE`;
  2. saves `exp-summary.sh` output to `summary<cur>.txt` while the guest still has the package and its events;
  3. runs `rec<cur>h/recover.sh` directly;
  4. calls `launch-exp.sh`: fetch, prepare, visible recovery, BEFORE with measured hashes, commit, start.

  Write the next BEFORE text and probe while the current run is in progress; the bench then never waits for a turn. Record the ACTUAL of `<cur>` from `summary<cur>.txt`.
- Disk: the pwdev volume is 300 GiB and verify-launch needs 5 GiB free.
  - `scratchpad/evidence-compress.sh 3 50` gzips evidence files over 50 MiB in all but the newest 3 experiments (lossless; archives untouched). chain-next runs it before every launch.
  - Never delete the guest-archive or guest-trace-archive directories without the user.
- A failed launch after staging: do not re-stage, which refuses with "old staged package remains". Verify with `verify-launch<N>.py`, run only the recovery section of prepare.sh, then rerun launch-exp.sh; it falls back to verify-launch when prepare prints no PASS.
- Guest UI chains (Night Raid, Steam): every interactive step must verify its own precondition and abort cleanly. nr-c82.sh therefore:
  - preflights argument passing through arm-interactive with argtest.ps1 (EXP1914: quoted -Arguments never reached the script);
  - aborts with env cleanup when 3DMark does not appear, instead of clicking a desktop without it.

  New guest helpers get one dry run (argtest style) before they join a chain.
- Protected invariants: `.local/handoff/codex/protected-invariants.txt` lists hardware-validated source lines (file | exact line | experiment).
  - codex-review flags a commit that removes such a line without re-adding it. T7b lesson: green tests, but the commit disabled the validated C75c path.
  - Add a row whenever a hardware experiment validates a behavioural line. Use the exact full line: a substring survives a weakening rewrite.

## Fast GravityMark loop (user, 2026-10-23: "focus on GravityMark only, ~5 min per iteration")

- **KMD changes still need a boot.** The m1n1 firmware arm is one-shot per boot, and a device restart cannot re-initialise the GPU. Use a SHORT probe profile: package state, GravityMark D3D12, KMD receipts (probe1917.sh). The full regression set runs only before an accepted state.
- **UMD changes need no reboot:**
  1. `scratchpad/umd-quick.sh <baseN> <baseDir> <tag>` builds only the ARM64 UMD from HEAD, at the installed package version (run<baseN>.ps1 cut at ARM64_PACKAGE_DONE).
  2. `umd-hotswap.sh <dll>` replaces System32 and DriverStore AppleAgxRenderAdmissionUmdArm64.dll; the original is kept as .orig, and `umd-hotswap.sh restore` puts it back.
  3. `gm-quick.sh <tag> [asteroids] [api]` runs one GravityMark with a 90 s timeout.
- Each hot-swap run is recorded as EXP<baseN>U<k> in EXPERIMENTS.md (UMD sha256, commit, result). The final package of an accepted UMD still goes through the normal build and full set.
- `gm-quick.sh` ends with `GMQ_JOIN` lines from `tools/agx_gm_alloc_join.py <umd.log> <kmd-receipts.txt>`. The tool joins the G4 refusal Va with the D3D12 heap receipts (size, VA, CPU page property) and the T7g CPU-access slots, and names the owner of the refused range (a heap or a committed/owner BO, CPU-mapped or not). Use it instead of ad-hoc decoding. EXP1918U5: the join showed the refused BO was a committed resource that was never mapped.
- Before handing a placement idea to Codex, check every axis that was already tried: CpuVisible x segment set (EXP1027/T7c: CpuVisible=1 without the aperture -> E_INVALIDARG). An untried combination (CpuVisible=0, native-leaf segment only = T7h) is a new variable; a retried one is archaeology.
- `umd-hotswap.sh restore <baseDir>` reinstalls the base package's own `package/AppleAgxRenderAdmissionUmdArm64.dll`, verified by hash. The guest-side backup is not trusted: in EXP1918U it was taken after the first swap, so restore brought back q1. Always swap back to the package DLL before running recover.sh.
- `codex-review.sh` flags a protected invariant only when a removed diff line (`^-`) carries it. The earlier `grep -F -- "-"` also matched context lines containing `->` (T7h false positive). A NEW baseline failure in the review worktree that passes on a clean rerun is a concurrency flake (Codex tests ran at the same time): rerun once before rejecting.
- Builder layout (2026-10-23): old experiment dirs live in `C:\Users\pauls\APPLE AGX\old`, which the user may delete. Live dependencies stay in `C:\Users\pauls` and are computed as the transitive closure of the template references (`closure.ps1`). Never move builder dirs while a build runs. A missing old dir -> look in `APPLE AGX\old`.
- `kmd-receipts.sh` prints `GPU_FAULT_EVENT`, `GPU_FAULT` and `FW_RECOVERY` lines, decoded with `tools/decode_wom1_gpu_fault.py`. `exp-summary.sh` and `gm-quick.sh` show them. Read them before concluding "no KMD refusal": in EXP1919 two firmware timeouts were missed by the summary.
- `gm-run.ps1` closes GravityMark after "device removed" (CloseMainWindow, kill after 15 s) so the UMD trace tail is flushed. A kill at the timeout lost the last ~1.5 s before the removal in EXP1919.
- The probe's `RUN_DONE` line is in `newexp<N>.log`, not in `run<N>.out` (which ends with `EXP<N>_PIPELINE_RC`). Wait on the right file.
- Never start recover.sh, a launcher or any long hardware step with `&` inside a tool call: the job dies with the tool's shell (EXP1921: the rollback died after the receipt snapshot). Use the tool's background mode.
- A rollback that stops after `CLEANUP_SSH_STATUS` without `RECOVERY_DONE`:
  1. Check the guest read-only (boot time, APPL0002 problem, service key, CPU count).
  2. If Windows is up after the cleanup restart with problem 28 (FAILED_INSTALL), no AppleAgxAdmission service and 8 CPUs, run only `scratchpad/recover-final.sh <expDir> <N>` (durable preflight + inert gate). Append its output to rec<N>.log so launch-exp sees `RECOVERY_DONE`.
  3. If the machine is at proxy instead, run `recover-tail.sh` (wait for the proxy, normal-absent boot, preflight, gate).
  4. Never rerun the whole recover.sh after the cleanup.
- KMD device switches (=1 DWORDs the KMD reads at StartDevice, e.g. `Wom1GpuFaultInfoRead`, `Wom1TdrDrillAllowed`) go through `AGX_DEVICE_SWITCHES="..." launch-exp.sh`. It sets them on the staged devnode before the ordered restart, verifies them and records them in BEFORE. Rebuild the package from the same driver source so the switch is the only variable (check that the source archive hash equals the previous experiment's).
- After `launch-exp`, run `scratchpad/wait-exp.sh <N> <dir>` in the background. It waits for RUN_DONE/GUEST_DOWN, prints the summary and, on a crash, saves full-crash.log, stops the watchdog and prints the at-proxy recovery command.
- A Codex task runs as `scratchpad/codex-cycle.sh <task>` in the background (run + review in one call). Merge only on REVIEW_OK.
- At most one receipt-only experiment in a row per failure boundary; the next Codex task must change behaviour or prove that no correct change exists (Codex README rule).
- Two consecutive experiments that find the same defect class (EXP1925-1928: one NULL dereference after a failed allocation per run) stop hardware runs. The next Codex task audits the whole class and adds a fault-injection sweep test before the next package (mem2).
- `codex-run.sh` feeds Codex stdin from /dev/null. In a background chain an open stdin left `codex exec` stuck at "Reading additional input from stdin..." for 37 minutes (exp1926). If a Codex log stays under 1 KiB for more than 2 minutes, it is stuck: kill it by its PID and rerun.
- `post-exp.sh <N> <dir>` (background) chains wait-exp, the right recovery, exp-handoff and codex-cycle; use it right after launch-exp.
- Since 2026-10-24 (user: Claude usage limit) Codex owns analysis and fixes. After each experiment and its recovery: `exp-handoff.sh <N> <dir>`, then `codex-cycle.sh exp<N>`. Claude merges on REVIEW_OK, appends Codex's ACTUAL draft, builds with mkexp and launches with the switches from REPORT. The goal is a working D3D12 FL 12_1/12_2 (Codex README).
- Every Codex brief gets `scratchpad/brief-context.sh <task-dir> <keywords>`. It appends the design notes and CHANGES rows that already cover the topic. T7c lesson: the brief did not cite M2-vidmm-64k-decision.md or EXP1027/EXP1266, and the guessed placement fix failed on hardware within 2 s. The script splits its arguments into words and exits 3 (`BRIEF_CONTEXT_EMPTY`) when no note matches: check for `BRIEF_CONTEXT_ADDED <n> notes` before launching Codex. T7h lesson: one quoted phrase matched nothing and an empty section went to Codex.
