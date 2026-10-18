# Codex handoff: D3D12 on the J313 AGX (from Claude, 2026-10-18T01:00Z)

You continue the D3D12 driver work autonomously, without stopping, in the
same way it ran so far. Repository worktree:
/Volumes/pwdev/public_windows/.worktrees/integration-ad04-windows-compiler
(branch integration/ad04-windows-compiler). Obey AGENTS.md exactly (source
first, one variable per experiment, EXPERIMENTS.md BEFORE/ACTUAL entries,
CHANGES.csv rows, no attribution trailers, UTC stamps, never `git stash`).
The complete working method, scripts and gates are in
investigation/CLAUDE_WORKING_METHOD.md -- follow it step by step.
Bench (hardware) is always free; never ask for permission, keep going.

## Goal

Production-quality D3D11/D3D12 on the AGX through the WDDM driver; long
term CS 1.6 and modern D3D12 applications.

## D3D12 status (proven on hardware)

- C2-C5d device, buffers, queues/fences, copies, compute with every binding.
- C6a-C6f clears/copies, first triangle, graphics root bindings, indexed
  draws, texture sampling, depth.
- C7c swapchain Present (DXGI flip model) -- EXP1372.
- C8c placed buffers -- EXP1372.
- C9 compute texture SRVs/samplers/typed UAVs, C10 UAV clears, C11 query
  heaps (statistics, binary occlusion, timestamps at batch granularity),
  C11d KMD DxgkDdiCalibrateGpuClock (GetTimestampFrequency 1e9), C14
  indirect dispatch -- EXP1375.
- Regressions baseline: 73 SMOKE PASS + known MSAA-a000r FAIL, GL ~52 fps,
  bench ~59.6 fps, corpus createfail 64/4, DWM faults 0.

## In flight at handoff

- EXP1376 (package dc8d857f, probe c510ff8a): the guest hung during the
  idle SEGSTATS stage (GUEST_WATCHDOG_ABORT 00:55:56Z, before any D3D12
  step). rec1376h/recover.sh was started (scratchpad rec1376.log). Record
  EXP1376 ACTUAL as a hang (KMD identical to EXP1375 which passed; cause
  not established) once the newexp "Record EXP1375 rollback and EXP1376
  plan" commit state is clear (check git log / EXPERIMENTS.md tail; if the
  BEFORE entry is missing, append it from scratchpad before1376.txt first).
- EXP1377 = rerun of the same sources (only docs changed since dc8d857f):
  build1377.sh started (scratchpad build1377.log, newexp-kmd1377.sh, probe
  agx_d3d12_probe_e1377_arm64.exe). Write before1377.txt (rerun after the
  EXP1376 hang; same hypotheses as before1376.txt) and chain1377.sh /
  run1377.sh (copy the 1376 ones, waiting for rec1376.log RECOVERY_DONE).
- Pending correction: append to EXPERIMENTS.md a correction of EXP1375
  ACTUAL -- the timestamps were batch-end GPU times (both EndQuery in one
  render batch), not a CPU fallback (see D3D12-C11-queries.md).
- Expected in EXP1377: C13A PASS (graphics root UAV setter c93c9b20),
  C14A PASS (indirect draw with count buffer), C11A PASS (sequential
  occlusion), C12A/B measurement (COPY/COMPUTE queues, cross-queue Wait).

## Next work after EXP1377 (in this order)

1. Fix whatever EXP1377 shows (receipts first).
2. Root signature register spaces != 0 and SRV/UAV/CBV slot limits
   (currently space != 0 refuses, reason 5).
3. Typed texture UAV / SRV formats coverage, cube and 3D textures, mip
   chains in views; MSAA stays out (Asahi path has no multisample yet).
4. DXIL (SM6) shaders: design in investigation/analysis/D3D12-C15-dxil-plan.md.
   Option 1 (dxil-spirv, MIT) needs the AGENTS license review -- record the
   review before any external code enters the build; do not copy code.
5. Enable D3D12 outside the APPLE_AGX_D3D12=1 profile only with an
   explicit experiment proving DWM/desktop unaffected.

## Hard rules learned

- No explicit PIPE_BIND_SHADER_IMAGE on owner resources (EXP1373).
- Command slots are zeroed in AppendCommand (EXP1374); keep it.
- Every compute root setter needs its graphics twin (EXP1375).
- One active occlusion query per context (Asahi).
- Do not change the guest WER profile; never send to the guest while
  recover.sh runs; grep -a on logs; stamps from `date -u`.
- Chat/report language for the user: Russian.
