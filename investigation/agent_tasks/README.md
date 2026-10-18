# Persistent worker-task contracts

This directory defines bounded work delegated by the Apple AGX Full Graphics
architect. It augments, but does not replace, `AGENTS.md`,
`GPU_ENGINEERING_PROCEDURE.md`, or the Full Graphics mission.

## Roles

| Tier | Role | May do | May not do |
| --- | --- | --- | --- |
| A | Lead architect | causal decisions, ABI/ownership/capability decisions, hardware hypothesis and verdict | routine broad log/build loops when a bounded task exists |
| B | Spark implementation | approved bounded edits/tests/commits | architecture, ownership, capabilities, hardware verdicts |
| C | Devstral mechanical | exact commands, source search, compile/test/log/hash evidence | source changes, project planning, hardware or next-action decisions |

Every task starts from one immutable input commit and must use its named worker
worktree. Worker branches are never automatically merged. A Tier B commit is
reviewed and explicitly integrated by Tier A only after scope verification.

## Required lifecycle

1. Architect writes a contract from `TASK_TEMPLATE.md`.
2. `create_task_worktree.py` creates an isolated `agent/` branch at INPUT_COMMIT.
3. Worker does only contract actions and writes a compact return bundle.
4. `verify_worker_scope.py` fail-closes on changed paths outside ALLOWED_PATHS.
5. `collect_worker_evidence.py` retains raw logs and emits compact JSON.
6. Architect checks summary, scope result and raw evidence only when decisive.
7. Architect explicitly accepts/rejects output. No auto merge/cherry-pick.
8. `cleanup_worker_worktree.py` removes only clean, exact worker paths; its
   branch is preserved for audit unless the architect intentionally deletes it.

No task may expand its own permissions. An unexpected result stops that task
with evidence. Two equivalent failures at a causal boundary return to Tier A.

## Scripts

```text
scripts/agent/create_task_worktree.py
scripts/agent/verify_worker_scope.py
scripts/agent/collect_worker_evidence.py
scripts/agent/cleanup_worker_worktree.py
```

All scripts use explicit paths/commits and fail closed. They do not stage,
commit, merge, launch hardware, delete branches, or touch any other worktree.

## Evidence compression

Workers preserve raw files, but report only exact compact fields from
`RETURN_TEMPLATE.md`. A long log is represented by result, first diagnostic,
toolchain/target, changed-source state, blocker class and raw evidence path.
The architect decides whether raw evidence needs reading.

## Current worker layout

`/Users/pavel/public_windows` is the architect checkout and may remain dirty.
Dedicated worker paths below `.worktrees/` must begin at an explicit commit:

```text
.worktrees/spark-implementation/  agent/spark-implementation
.worktrees/spark-review/          agent/spark-review
.worktrees/devstral-mechanical/   agent/devstral-mechanical
```

Existing historical worktrees are not deleted or repurposed by this framework.

## Phase 2 runner status — 2026-09-11

`tier_c_runner.py` is the execution authority. It maps a small immutable command
ID registry to fixed argv arrays; JSON task contracts can only permit IDs and can
never provide shell strings. It validates input commit/repository/tier, captures
stdout/stderr/exit code, bounds timeout, checks source/forbidden paths, writes a
compact summary and never merges/cherry-picks or launches hardware. Devstral is
optional classification only and receives no shell authority.

Local runner suite: 11/11 PASS. `VERIFY-FRYZZING-CLANG` was then run once from
a disposable worktree. The fixed SSH version query timed out at20 seconds with
empty stdout/stderr; scope was clean and worktree cleanup succeeded. This is a
control-plane INCONCLUSIVE result, not a compiler result. Evidence is under
`investigation/evidence/agent_tasks/VERIFY-FRYZZING-CLANG/`.

## Initial verification status — 2026-09-11

The three persistent worktrees were created at
`5f42d6481661df44c6c114921c514adee50c2988`. `ORCH-DRY-RUN-001` verified
create → clean scope check → compact evidence → clean exact removal, while the
architect checkout retained its pre-existing dirty state. The dry-run branch is
preserved and the temporary worktree is removed.

`AD04-CLANG-DISCRIMINATOR` is prepared in `agent/devstral-mechanical`, but has
not been dispatched: FRYZZING SSH `192.168.1.24:22` returned `No route to host`
and no local `127.0.0.1:11434` Ollama tunnel was listening. Do not substitute a
different model/tier or run the task manually; revalidate this control plane,
then dispatch the unchanged Tier C contract.
