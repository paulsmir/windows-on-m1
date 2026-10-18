---
name: agx-d3d12-loop
description: Use when continuing the AppleAgx D3D12 (or D3D11) WDDM driver work on the J313 AGX in public_windows -- implementing a fail-closed DDI, writing a probe step, building a package, running a hardware experiment (EXPnnnn) with recovery, or recording EXPERIMENTS.md/CHANGES.csv ledgers.
---

# AGX D3D12 development loop

Single source of truth for details: `investigation/CLAUDE_WORKING_METHOD.md`
in the worktree `/Volumes/pwdev/public_windows/.worktrees/integration-ad04-windows-compiler`.
Read it, `AGENTS.md`, `investigation/GPU_CURRENT_STATE.md` and the newest
`investigation/EXPERIMENTS.md` entries before acting.

## Per feature (C-step)

1. Choose the next fail-closed DDI real applications hit
   (`grep AGX_REJECT( drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp`) or the
   failure the last experiment showed (receipts first:
   `measure-d3d12-rejected {table, offset}` -> WDK struct member).
2. Primary sources: WDK 26100 headers (`.local/reference/wdk26100`,
   `winsdk26100`), Mesa Asahi (`.local/reference/mesa/src/gallium/drivers/asahi`),
   the repo's D3D11 path (`drivers/apple-agx/mesa/d3d11/agx_d3d11_ddi.cpp`).
   Confirm every Gallium entry you call is assigned by Asahi.
3. Design note `investigation/analysis/D3D12-C<n>-*.md`: WINDOWS CONTRACT,
   AGX/ASAHI CONTRACT, TRANSLATION, WHAT IS STILL UNKNOWN, Checkpoint.
4. Implement: record in `agx_d3d12_ddi.cpp` (commands from zeroed
   `AppendCommand` slots), replay in `ExecuteCommandLists` through the owner
   (`drivers/apple-agx/mesa/winsys/agx_d3d12_windows.{h,cpp}`).
   Rules: no u_format.h inline helpers in the UMD; small replay frames
   (STACK_FRAMES <= 16384); no explicit PIPE_BIND_SHADER_IMAGE; every compute
   root setter has a graphics twin.
5. Tests: `cd tests && python3 -m unittest test_d3d12_abi_gate test_d3d12_native_owner`
   (add tests only for real bugs / deterministic invariants).
6. Builder compile gate (`B=pauls@192.168.1.24`, `K=~/.ssh/windows_builder`,
   quote `"${B}:..."` in zsh): copy the DDI and winsys files into
   `syncheck-d3d12/`, run `syncheck-d3d12.ps1` -> three `rc=0`.
7. Probe step (`drivers/apple-agx/windows/icd/agx_d3d12_probe.c`, fresh
   device via `run_isolated`, own `AGX_D3D12_<step>: PASS|FAIL` lines;
   shaders via fxc on the builder; build with `build-d3d12probe-arm64.ps1`).
8. Commit (no assistant attribution), then a `investigation/CHANGES.csv` row
   (python csv writer, CRLF, 40-char commit, `date -u` stamp,
   implemented/validated/rejected); gate on `grep -q '^OK'`.

## Hardware experiment

Scratchpad scripts (copy from the previous number with sed):
`buildNNNN.sh` -> package (require `PACKAGENNNN BUILD PASS` + both
STACK_FRAMES PASS; no code commits until the snapshot line), probe
`agx_d3d12_probe_eNNNN_arm64.exe`, `newexp-kmdNNNN.sh`, `beforeNNNN.txt`
(hypothesis, WHY THIS HYPOTHESIS, ATOMIC CONTRACT, WINDOWS/AGX CONTRACT,
TRANSLATION, WHAT IS STILL UNKNOWN, Expected, Failure criteria, Recovery),
`chainNNNN.sh` (waits for the previous recovery), `runNNNN.sh` (waits for the
build). Monitor logs for `TOOL_d3d12-on_END`, `NEWEXP_DONE`, aborts.
Then: read `evidence/EXPNNNN/tool-d3d12-on-std.txt` + `-umd.log`, check
regressions (73 SMOKE PASS + known MSAA FAIL, GL ~52 fps, bench ~59.6 fps,
corpus 64/4, DWM faults 0), `airssh 'shutdown /r /t 3'`,
`evidence/recNNNNh/recover.sh` -> RECOVERY_DONE, record EXPNNNN ACTUAL and
CHANGES verdicts. Guest hang: kill the experiment's run_uefi, run recover.sh,
rerun under a new ID. Never edit old results; append corrections.

## Habits

- One variable per experiment; per-step PASS lines allow bundles.
- When a whole class breaks, `git diff <good> <bad>` the failing path.
- Separate probe bugs from driver bugs (D3D12 API rules).
- Report to the user in Russian, one short report per experiment.
