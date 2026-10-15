# Fast evidence-driven iteration (how the D3D/AGX work is done)

This is the working loop used for the D3D10/11/12 and AGX driver work since
EXP1290 (EXP1296 -> EXP1297: a GPU timeout storm traced to one missing
`driver_location` in about two hours). It complements AGENTS.md; AGENTS.md
wins on any conflict. Every agent (Claude or Codex) works this way.

## 1. Start at the current boundary

- Read `investigation/GPU_CURRENT_STATE.md` and the last `EXP<n> ACTUAL`
  entry of `investigation/EXPERIMENTS.md` (search for it; never reread the
  whole ledger).
- Name the one failing checkpoint you are working on and the strongest
  causal lead. Everything else waits.

## 2. Evidence before any change

- Get the exact failing artifact first: the per-step smoke output, the UMD
  trace (`*-umd.log`), the KMD receipts (`Wom1*` registry blobs decoded with
  the scratchpad decoders: `fwrecovery.py`, `g4rcpt.py`, ...), the m1n1
  HV log, dumps.
- Symbolize crashes, do not guess: user-mode minidumps with the matching
  PDB (`llvm-symbolizer --obj=<dll> --relative-address <rva>`, plus
  `llvm-objdump -d` around the address); kernel dumps with `cdb !analyze -v`
  on the Windows builder.
- Compare counters with the previous run (receipts are per boot: e.g.
  firmware recoveries 1 per run is normal, 2435 is a storm).
- Quote the primary source line that explains the observation: Mesa/Asahi
  source (`.local/reference/mesa`, `.local/reference/asahi-linux-asahi`),
  WDK headers (`.local/reference/wdk26100`), Microsoft documentation.
  A claim without a quoted line is labelled HYPOTHESIS.

## 3. Make it deterministic offline (RED -> GREEN)

- Reproduce the invariant offline before fixing it, with the cheapest
  existing harness:
  - projection checks: `tests/test_d3d10_shader_fields_init.py::project()`
    runs the real Mesa projection into a temp dir; assert on
    `function_body(...)` of the projected source;
  - translator checks: `tests/dxbc_to_nir_fixture.c` (built by
    `tests/test_dxbc_to_nir.py`) translates DXBC fixtures and runs Asahi's
    lowering; extend its printed summary with the quantity you need (e.g.
    `vs-input-bases`) and assert on it;
  - KMD/UMD C code: the existing host-compiled fixtures under `tests/`.
- Run the new test, see it fail for the right reason, then fix, then see it
  pass, then run the neighbouring suites
  (`python3 -m unittest test_d3d11_ddi_table test_d3d10_shader_nir_path
  test_d3d10_shader_fields_init test_dxbc_to_nir ...`).
- A test must catch a real bug or a deterministic contract (AGENTS.md
  "WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?").

## 4. Small commits, ledger immediately

- One reviewable change per commit; the message states the evidence (EXP id,
  numbers, the source line) and the fix. No assistant attribution.
- Append the `investigation/CHANGES.csv` row right after the commit (CRLF,
  40-hex commit, `feature|correction|process`, full sha256 or empty,
  `implemented` until hardware validates it); run
  `tests/test_change_ledger.py` before committing the row.
- Gate commits on test success (`grep -q '^OK'` on the saved test log),
  never on `tail`.

## 5. Hardware is a discriminator, not a guess

- When offline analysis cannot separate two causes, design the smallest run
  that does, and prefer running it on the already installed package in the
  same boot by changing only a tool or a per-process environment switch
  (smoke tool, corpus runner, `APPLE_AGX_*` variables read per creation).
- Build tools that turn the hardware into a differential oracle:
  - `agx_corpus_smoke` draws each real-world shader through the TGSI path
    and the translator on two devices of one process and names
    translator-only hangs and pixel differences;
  - a mini corpus of hand-written HLSL (compiled with fxc on the builder)
    splits a hypothesis in one boot (EXP1297 D3: VS vs PS, per-vertex vs
    uniform index);
  - `AGX_MESA_DEBUG=shaders` dumps the compiled NIR of both paths for a
    direct comparison (EXP1297 D4).
- Dry-run every new tool on the builder's own D3D11 adapter first; a tool
  bug must never cost a hardware boot.
- Record a short BEFORE line (hypothesis, the one changed variable, hashes,
  expected result) and an ACTUAL line for every run, including tool-only
  reruns.

## 6. Pipelines and builds

- Package builds: closure (`closure-and-umd.sh N prev out pb`) -> WOW64 UMD32
  -> `mkicd.py source` + `build-kmd.ps1` (KMD + ARM64 UMD from the committed
  HEAD) -> `newexp-kmd<X>.sh` pipeline with per-step checkpoints. Never
  commit between a closure start and the KMD build when that closure feeds
  the package.
- Watch pipelines with a filtered monitor (verdict lines only); never poll
  in short sleeps.

## 7. Parallelism and speed

- While hardware runs, do the next offline fix. Long waits run in the
  background with an until-loop on the done marker.
- Read-only research and independent implementation go to parallel agents;
  each writer works in its own git worktree on its own branch; merges are
  done by the integrator after a test comparison against the pre-merge
  commit (new failures only count).

## 8. Anti-loop

- One complete offline pass per hypothesis. If it yields no new evidence,
  stop and design the discriminating experiment instead of a second pass.
- Each step must exclude, confirm or narrow a cause.

## 9. Reporting

- Per experiment: a table of checkpoints with CONFIRMED / REJECTED /
  INCONCLUSIVE, the root cause found, the fix commits, and the next step.
- Before a task: what exactly will be fixed and where the work stands on the
  road to the full driver (D3D11 FL 11_0 default, flexible memory, D3D12).

## 10. Environment quirks

- `grep` is ugrep: use `grep -a` on logs.
- Ledger timestamps: `date -u`.
- Never use bare `git stash`; `rm` only with `"${VAR:?}"`.
- The Codex sandbox cannot write the shared `.git`: Codex edits files and
  runs tests inside the worktree the integrator created, then reports; the
  integrator reviews the diff and commits. Start each Codex task with its
  working directory inside that worktree (`cd <worktree>` before invoking
  the companion): Codex's writable roots follow its start directory, and a
  task started elsewhere fails every write with "Operation not permitted".
  Codex has no Windows SDK headers: the integrator compiles natively on
  the builder (clang-cl /Zs) before committing. Codex has no SSH to the builder
  or the guest: builder and hardware steps stay with the integrator.
