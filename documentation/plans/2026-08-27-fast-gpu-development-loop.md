# Fast J313 GPU Development Loop Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Windows-driver GPU iteration wait for real driver completion, expose a compact session state, and avoid unnecessary full tests and hardware reboots.

**Architecture:** Extend the existing hash-pinned lifecycle runner instead of adding a competing installer. A small tracked state document is the session entry point, and a read-only shell helper prints that state plus bounded repository identity. Python contract tests validate both tools without touching Windows hardware.

**Tech Stack:** PowerShell 5.1, POSIX shell, Python `unittest`, Git, Windows PnP and registry receipts.

**Spec:** `documentation/design/2026-08-27-fast-gpu-development-loop.md`

## Global Constraints

- Work only in `/Users/pavel/public_windows` on `feature/j313-gpu-acceleration`.
- Do not create a worktree and do not touch `/Users/pavel/windows`.
- Preserve existing m1n1 and Mu submodule dirt.
- Never use `pnputil /force`.
- Never infer driver success from `pnputil` completion or transient PnP `OK`.
- Do not perform a hardware action while implementing this plan.

---

### Task 1: Compact current-state contract

**Files:**
- Create: `investigation/CURRENT_STATE.md`
- Create: `tests/test_fast_gpu_workflow.py`

**Interfaces:**
- Produces: the bounded session entry point required by later tools.

- [ ] **Step 1: Write the failing state-contract test**

Require headings for stable recovery, repository identity, live machine, last
confirmed boundary, active hypothesis, next action, rollback and context budget.
Require the file to remain below 180 lines and to reference EXP-123 and EXP-136.

- [ ] **Step 2: Run the focused test and observe RED**

Run: `./proxyenv/bin/python -m unittest tests.test_fast_gpu_workflow -v`

Expected: failure because `investigation/CURRENT_STATE.md` does not exist.

- [ ] **Step 3: Add the current factual state**

Record the current branch and pins, exact live display-both boot hashes, current
`oem17.inf` package version, Problem 43, RTKit phase 1/flags 1, timeout statuses,
healthy Windows services, the RTKit-HELLO hypothesis, and EXP-123 rollback.

- [ ] **Step 4: Run the focused test and observe GREEN**

Run: `./proxyenv/bin/python -m unittest tests.test_fast_gpu_workflow -v`

- [ ] **Step 5: Commit**

Commit message: `docs: add compact GPU development state`

### Task 2: Receipt-complete Windows hot cycle

**Files:**
- Modify: `drivers/apple-agx/windows/scripts/cycle-lifecycle-driver.ps1`
- Modify: `tests/test_fast_gpu_workflow.py`
- Modify: `tests/test_apple_agx_windows_package.py`

**Interfaces:**
- Consumes: fresh `Wom1StartDeviceStatus` under the APPL0002 device-parameter key.
- Produces: `Completion` in `result.json` with outcome, elapsed time, final status and timeout.

- [ ] **Step 1: Write failing runner contract tests**

Require `CompletionTimeoutSeconds`, bounded polling, no fixed eight-second sleep,
a fresh final receipt, explicit `completed`/`timeout` outcome, and continued
absence of `/force`.

- [ ] **Step 2: Run the two focused suites and observe RED**

Run: `./proxyenv/bin/python -m unittest tests.test_fast_gpu_workflow tests.test_apple_agx_windows_package -v`

- [ ] **Step 3: Implement the minimal completion waiter**

Add validated timeout and poll interval parameters. Poll the existing receipt
function after the one device restart. Stop only when the fresh
`Wom1StartDeviceStatus` key exists or the deadline expires. Persist completion
metadata before applying health gates, and fail closed on timeout.

- [ ] **Step 4: Run the focused suites and observe GREEN**

Run: `./proxyenv/bin/python -m unittest tests.test_fast_gpu_workflow tests.test_apple_agx_windows_package -v`

- [ ] **Step 5: Commit**

Commit message: `tools: wait for final AGX StartDevice receipt`

### Task 3: Bounded context helper

**Files:**
- Create: `scripts/gpu-dev-context.sh`
- Modify: `tests/test_fast_gpu_workflow.py`

**Interfaces:**
- Consumes: `investigation/CURRENT_STATE.md`, Git/submodule state and the tail of `investigation/CHANGES.csv`.
- Produces: a read-only output capped at 220 lines.

- [ ] **Step 1: Add a failing helper-contract test**

Require repository-root resolution, current-state output, root/submodule commit
identity, bounded ledger tail, a hard line cap, and no network or mutation command.

- [ ] **Step 2: Run the focused test and observe RED**

Run: `./proxyenv/bin/python -m unittest tests.test_fast_gpu_workflow -v`

- [ ] **Step 3: Implement the read-only helper**

Use `git rev-parse`, `git status --short`, `sed`, and `tail`; reject execution
outside the canonical public checkout; trim output to 220 lines.

- [ ] **Step 4: Run the helper and focused test**

Run: `./scripts/gpu-dev-context.sh`

Run: `./proxyenv/bin/python -m unittest tests.test_fast_gpu_workflow -v`

- [ ] **Step 5: Commit**

Commit message: `tools: add bounded GPU session context`

### Task 4: Close EXP-136 and publish the workflow

**Files:**
- Modify: `investigation/EXPERIMENTS.md`
- Create: `investigation/artifacts/EXP-20260827-136-agx-rtkit-phase-diagnostics/VERDICT.md`
- Modify: `investigation/CHANGES.csv`

**Interfaces:**
- Consumes: saved EXP-136 preflight, installation, postflight and PnP evidence.
- Produces: an immutable rejected hardware verdict and machine-readable process rows.

- [ ] **Step 1: Record the actual EXP-136 result**

Document Problem 43, stage 6, `0xC00000B5`, RTKit phase 1, flags 1, no HELLO,
final CPU status `0x2d`, `0xC00000BB` cleanup, responsive eight-core Windows,
healthy platform services, no fresh post-install Event 129, and the `pnputil`
asynchronous-status race.

- [ ] **Step 2: Verify focused and complete software suites**

Run the workflow/package tests, `tests.test_change_ledger`, and then the complete
repository suite once because this is a significant push boundary.

- [ ] **Step 3: Commit the documentation and experiment result**

Commit message: `docs: close RTKit phase diagnostic experiment`

- [ ] **Step 4: Append exact implementation commits to CHANGES.csv**

Add process rows for the compact state, completion waiter, and context helper.
Use `implemented` for software-only workflow changes and `rejected` for EXP-136.

- [ ] **Step 5: Verify ledger, commit bookkeeping and push**

Run: `./proxyenv/bin/python -m unittest tests.test_change_ledger -v`

Commit message: `docs: index fast GPU development workflow`

Push only `feature/j313-gpu-acceleration` after confirming status contains only
the preserved m1n1 and Mu dirt.
