# GPU experiment-agent protocol

## Current binding workflow — 2026-09-06, effective from EXP503

One long-lived implementation agent owns the entire remaining GPU mission.
`/root/gpu_long_lived_implementation` exclusively controls source, Air, proxy,
launcher, builds and experiment ledgers. The parent only orchestrates and waits.
The same implementation agent performs EXP503, evidence, exact cleanup, causal
correction, EXP504 and every subsequent iteration through hardware-accelerated
OpenGL and CS1.6 rendering a real game scene. Individual experiments, admission,
first submission, fence, frame or present are internal checkpoints, never a
handoff or completion. Read-only review is permitted; implementation and hardware
ownership cannot be delegated away.

Continue permitted independent work after any platform restriction; never retry
or rephrase a restricted action or treat refusal as hardware evidence. Return
early only for unavoidable physical action, an indispensable platform restriction
or unrecoverable environment failure, after permitted independent work is spent.
At final acceptance leave the known-good KMD/UMD installed, enabled and active.
Intermediate exact package cleanup and GPU-visible ordinary recovery still apply.

The following EXP500–502 workflow is preserved as superseded history. It no
longer controls live execution. `.local/gpu-long-lived-mission.md` contains the
complete binding mission and proof requirements.

## Superseded workflow — effective EXP500 through EXP502

User-requested workflow, effective from EXP500 (2026-09-06).

## One new executor per experiment

The controller assigns each new EXP to a fresh agent with an isolated context and
a bounded brief. That executor returns its complete evidence and verdict to the
controller. It does not start the next EXP. After review and identification of
the next falsifiable boundary, the controller creates a new executor.

The original acceptance target remains an actually working accelerated AppleAgx
graphics driver, ultimately CS1.6 in OpenGL using AGX rather than software.
An EXP PASS, triangle, single submit or fence is a checkpoint, not completion.

## Exclusive hardware ownership

- At most one executor controls the Air, SSH mutations, proxy/vUART or launcher.
- The controller and read-only reviewers do not operate the Air while it is
  assigned. They may independently inspect frozen local source/artifacts/logs.
- The executor checks both Windows SSH and USB/launcher ownership before asking
  for physical action. No duplicate launcher and no repeated failed candidate.
- The executor follows the existing AGENTS.md experiment gates, source-first
  evidence rules, exact package lifecycle and known-good recovery constraints.
- ANS worktrees/branches and unrelated dirty changes remain outside GPU scope.

## Per-EXP lifecycle

1. Read GPU_CURRENT_STATE.md, exact brief, and only referenced evidence/contracts.
2. Finish the scoped causal implementation and offline gates if not already done.
3. Freeze source, build using the pinned builder, sign and record exact hashes.
4. Preregister the hypothesis and single variable in EXPERIMENTS.md.
5. Verify the clean ordinary baseline and stage only that exact package.
6. Perform one natural bind and collect actual receipts/host log/ETL/dump.
7. Record the verdict before exact package cleanup and ordinary restoration.
8. Update compact state and return a report with evidence, health, remaining
   boundary and current launcher/session ownership. Do not execute the next EXP.

For EXP500, steps2–5 were already completed by the controller before delegation.
The agent must not repeat them or rebuild/restage a new package under that ID.

## Handoff and review

Brief/report paths live under `.local/experiments/<exact-EXP-directory>/`.
The report distinguishes IMPLEMENTED, OFFLINE PROVEN and HARDWARE PROVEN, names
the exact first failing primitive, and records cleanup even after a crash.
The controller reviews both compliance with the brief and evidence quality:
artifact identity, actual boundary, causality, cleanup and safe next action.
Questions about missing context go to the controller, not redundant user gates.

Follow-ups to the same executor are allowed only to finish evidence or recovery
for its existing EXP. A new hardware candidate receives a new ID and new agent.
No executor may spawn helpers or reviewers; the controller owns coordination.
Any platform-restricted action is not retried through an equivalent workaround;
permitted independent engineering work continues without inventing evidence.

## Persistence

GPU_CURRENT_STATE.md remains the authoritative live hardware/source state.
EXPERIMENTS.md remains the immutable intended/observed experiment ledger.
CHANGES.csv remains the commit-to-change index. The controller owns protocol
bookkeeping; executors own their experiment evidence and compact-state updates.
The controller's orchestration ledger records the active executor identity, so
compaction cannot accidentally dispatch the same experiment twice.
