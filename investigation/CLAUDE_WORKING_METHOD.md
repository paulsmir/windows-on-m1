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
  - It overrides `writable_roots` with canonical `/Volumes/pwdev` paths: the user's config lists `/Users/pavel/public_windows/.local`, a symlink, and Codex refuses every command with it.
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
