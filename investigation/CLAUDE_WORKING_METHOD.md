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
   convert .h to LF. Build probe: scp probe.c + headers to builder home,
   `powershell -File C:\Users\pauls\build-d3d12probe-arm64.ps1` -> BUILD_PASS sha.
8. Commit code (no attribution trailers), then CHANGES.csv row via python
   csv writer (CRLF, 40-char commit, UTC stamps from `date -u`, status
   implemented/validated/rejected), gate commits on `grep -q '^OK'`.

## Hardware experiment pipeline (scratchpad scripts)

Scratchpad: /private/tmp/claude-502/-Volumes-pwdev-public-windows/8c9ead37-29d6-4c63-9bb0-35c6eb850f40/scratchpad
(backups: .local/scratchpad-tools-backup, tools/agx-lab).

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
  `airssh 'shutdown /r /t 3'`, run evidence/recNNNNh/recover.sh to RECOVERY_DONE.
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
