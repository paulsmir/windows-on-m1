# J313 AGX G1Q Queue Qualification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Qualify one interrupt-completed AGX barrier/no-op command across ten cold-reset-separated assisted lifecycles without modifying the stable Windows boot path.

**Architecture:** A pure Python state machine validates one immutable queue-completion receipt and owns fail-closed evidence aggregation. A separate m1n1 adapter composes the proven G1 lifecycle backend, creates context 63 and one guarded canary mapping, submits one barrier to queue index 1's 3D channel, and records queue/event/fault state. A shell runner enforces provenance and cold reboot boundaries; none of these components participate in the eventual Windows submission path.

**Tech Stack:** Python 3 standard library, `unittest`, pinned m1n1 AGX/UAT/context/channel/event/command structures, POSIX shell, JSON, SHA-256, existing G0 contract and G1 recovery-manifest validation.

**Spec:** `documentation/design/2026-08-25-j313-agx-g1q-queue-qualification.md`

## Global Constraints

- Work only on `feature/j313-gpu-acceleration` under `/Users/pavel/public_windows`.
- Preserve `.local/recovery/STABLE-j313-8core-native-input-v1/` byte-for-byte.
- Keep normal assisted launch, standalone launch, Mu, ACPI, stable firmware, and Windows unchanged.
- G1Q runs only before guest entry with sole proxy ownership.
- Use context ID `63`, UAT page size `0x4000`, queue index `1`, queue type `3D`, exactly one barrier/no-op command, and a fixed `0.5` second deadline.
- The context maps exactly one gate-owned `0x4000` canary page with one unmapped guard page on each side; the command never accesses it.
- Pass requires exact producer/consumer progress and exactly one matching firmware completion event. Polling is evidence, never a completion substitute.
- No shader, render target, display address, guest allocation, or arbitrary physical address may be submitted or mapped.
- Every failure captures evidence, blocks Windows, and requires physical reboot before retry.
- Every accepted qualification has ten one-shot cycles and ten changed proxy identities.
- Every production behavior is written test-first and observed failing before implementation.
- Every implementation commit receives an exact 40-character row in `investigation/CHANGES.csv`; each ledger-only commit remains separate and receives no row of its own.
- Every hardware run is preregistered in `investigation/EXPERIMENTS.md` with exact source commits, artifact hashes, command, deadline, evidence directory, pass criteria, and stop rules.
- No `Co-Authored-By`, assistant attribution, or session trailer is permitted.

---

## File map

- `tools/agx_queue_gate.py`: pure G1Q receipt validation, lifecycle, atomic evidence, cold-cycle aggregation, proxy-receipt binding, and verification CLI.
- `tools/agx_m1n1_queue_backend.py`: sole hardware adapter; composes `M1n1AgxBackend` and wraps pinned context, UAT, queue, barrier, event, and snapshot primitives.
- `scripts/run-agx-queue-gate.sh`: assisted-only provenance guard and cold-reset orchestration.
- `tests/test_agx_queue_gate.py`: pure lifecycle, receipt mutation, deadline, cleanup, aggregation, and CLI tests.
- `tests/test_agx_m1n1_queue_backend.py`: real adapter behavior against complete deterministic fakes.
- `tests/test_run_agx_queue_gate.py`: shell boundary, immutable-artifact, guest-ownership, and launch-isolation tests.
- `documentation/AGX_BRINGUP.md`: exact reproduction and result interpretation.
- `investigation/EXPERIMENTS.md`: preregistration and append-only real-hardware result.
- `investigation/CHANGES.csv`: exact implementation-commit ledger.

---

### Task 1: Pure queue receipt and lifecycle gate

**Files:**
- Create: `tools/agx_queue_gate.py`
- Create: `tests/test_agx_queue_gate.py`

**Interfaces:**
- Consumes: `AgxContract`, a `QueueGateBackend`, exactly `cycles=1` for live one-shot execution, and a fresh evidence directory.
- Produces: `QueueGateError`, `QueueGateBackend`, `QueueGateResult`, `validate_completion(receipt: dict) -> dict`, `run_queue_gate(...) -> QueueGateResult`, `aggregate_cold_queue_results(...) -> dict`, `verify_queue_gate_result(path: Path) -> dict`, and CLI commands `run-one`, `proxy-receipt`, `aggregate-cold`, and `verify-result`.
- `QueueGateBackend` methods are `prepare(contract)`, `start()`, `heartbeat()`, `configure_context(context_id)`, `submit_barrier(queue_index, timeout_s)`, `snapshot(reason)`, `stop()`, `reset()`, and `released()`.

- [ ] **Step 1: Write the failing receipt-validation tests**

Name the break: a queue pointer, event, mapping, canary, or deadline can be wrong while a receipt is accepted.

Use a literal valid receipt:

```python
VALID_COMPLETION = {
    "context_id": 63,
    "page_size": 0x4000,
    "queue_index": 1,
    "queue_type": "3D",
    "submitted_commands": 1,
    "producer_before": 0,
    "producer_after": 1,
    "consumer_before": 0,
    "consumer_after": 1,
    "event_id": 0,
    "event_count_before": 7,
    "event_count_after": 8,
    "matching_event_count": 1,
    "stamp_before": 0x51000000,
    "stamp_after": 0x51000000,
    "elapsed_s": 0.004,
    "deadline_s": 0.5,
    "canary_sha256_before": "1" * 64,
    "canary_sha256_after": "1" * 64,
    "guards_unmapped": True,
    "declared_mapping_count": 1,
    "unexpected_mappings": [],
}
```

Add independent tests that mutate exactly one field: wrong context, page size,
queue index/type, command count, producer delta, consumer mismatch, event delta,
matching-event count, stamp, elapsed deadline, canary hash, guard state, mapping
count, and unexpected mappings.  Each mutation must raise `QueueGateError` with
the failing boundary's name.

- [ ] **Step 2: Run RED**

Run: `python3 -m unittest tests.test_agx_queue_gate.QueueCompletionTests -v`

Expected: import failure for `tools.agx_queue_gate`, proving the behavior does not exist.

- [ ] **Step 3: Implement minimal strict completion validation**

Implement exact-key validation, reject booleans where integers are required,
require 64-character lowercase SHA-256 strings, calculate pointer deltas without
accepting wraparound ambiguity, and return a defensive copy only after every
literal contract above passes.  Define constants:

```python
GATE_VERSION = 1
AGGREGATE_VERSION = 2
CONTEXT_ID = 63
PAGE_SIZE = 0x4000
QUEUE_INDEX = 1
QUEUE_TYPE = "3D"
COMPLETION_DEADLINE_S = 0.5
QUALIFICATION_CYCLES = 10
```

- [ ] **Step 4: Run GREEN and mutation-check the validator**

Run: `python3 -m unittest tests.test_agx_queue_gate.QueueCompletionTests -v`

Expected: all receipt tests pass.  Manually change `matching_event_count == 1`
to `>= 1`, rerun the duplicate-event test to observe failure, then restore the
implementation and rerun GREEN.

- [ ] **Step 5: Write failing lifecycle tests**

Name the break: the gate can continue after submission, snapshot, cleanup, or
release failure, or can grant Windows permission after a one-shot cycle.

Create a deterministic real fake backend that records calls and returns the
literal completion.  Test:

```python
result = run_queue_gate(fake, contract, cycles=1, evidence_dir=path)
self.assertEqual(result.verdict, "incomplete")
self.assertFalse(result.windows_launch_permitted)
self.assertEqual(
    fake.calls,
    ["prepare", "start", "heartbeat", ("context", 63),
     ("submit", 1, 0.5), ("snapshot", "cycle-complete"),
     "stop", "reset", "released"],
)
```

Add separate failure tests for malformed completion, submit exception, deadline,
snapshot exception, stop exception, reset exception, and false release.  Assert
the atomic JSON contains the original error plus every obtainable failure
snapshot and cleanup error, and always keeps `windows_launch_permitted: false`.

- [ ] **Step 6: Run RED, implement the lifecycle, then run GREEN**

Run RED: `python3 -m unittest tests.test_agx_queue_gate.QueueLifecycleTests -v`

Expected: `run_queue_gate` is missing.

Implement one-shot lifecycle orchestration with `time.monotonic`, `_atomic_json`,
and fail-closed cleanup patterned after G1 without importing hardware modules.

Run GREEN: `python3 -m unittest tests.test_agx_queue_gate -v`

- [ ] **Step 7: Write failing cold-aggregation tests**

Name the break: edited, reordered, reused-identity, warm-reset, incomplete, or
fewer-than-ten cycle evidence can permit Windows.

Create ten literal one-shot results plus ten reset receipts.  Assert a valid set
produces `queue_gate_version: 2`, ten completed cycles,
`cold_reset_between_cycles: true`, a deterministic `aggregate_sha256`, and
Windows permission.  Mutate each boundary in its own test.

- [ ] **Step 8: Run RED, implement aggregation/verification, then run GREEN**

Run RED: `python3 -m unittest tests.test_agx_queue_gate.QueueAggregateTests -v`

Implement canonical per-cycle SHA-256 binding and reuse G1's strict live proxy
identity fields without accepting a G1 result as G1Q evidence.

Run GREEN: `python3 -m unittest tests.test_agx_queue_gate -v`

- [ ] **Step 9: Commit implementation and record its exact commit separately**

```sh
git add tools/agx_queue_gate.py tests/test_agx_queue_gate.py
git commit -m "gpu: add strict AGX queue qualification state machine"
```

Append the exact 40-character commit to `investigation/CHANGES.csv` with status
`implemented`, the receipt mutations as reproduction, and no hardware result.
Commit only the ledger with `docs: record AGX queue gate state machine`.

---

### Task 2: Pinned m1n1 queue backend

**Files:**
- Create: `tools/agx_m1n1_queue_backend.py`
- Create: `tests/test_agx_m1n1_queue_backend.py`

**Interfaces:**
- Consumes: a live m1n1 `u`, the proven `M1n1AgxBackend`, `GPUContext`,
  `GPUContextData`, `JobList`, `GPU3DWorkQueue`, `StampCounter`,
  `WorkCommandBarrier`, and `GPUEventManager` from the pinned m1n1 commit.
- Produces: `M1n1AgxQueueBackend` implementing `QueueGateBackend` and returning
  the exact completion receipt defined in Task 1.

- [ ] **Step 1: Write failing context-isolation tests**

Name the break: context zero, wrong page size, extra mappings, or mapped guards
can be used by the backend.

Use complete fakes for the actual m1n1 object shape.  Assert `configure_context(63)`:

- binds only context 63;
- allocates one `0x4000` canary page containing a literal repeated pattern;
- records one invalid translation immediately before and after the mapping;
- reports one declared mapping and no unexpected mapping;
- refuses every other context ID and a live UAT page size other than `0x4000`.

- [ ] **Step 2: Run RED, implement context setup, then run GREEN**

Run RED: `python3 -m unittest tests.test_agx_m1n1_queue_backend.ContextIsolationTests -v`

Implement composition over `M1n1AgxBackend`; do not subclass or duplicate G1
prepare/start/heartbeat/fault logic.  Delay all m1n1 AGX context imports until
hardware construction.

Run GREEN: same command; all tests pass.

- [ ] **Step 3: Write failing one-command queue tests**

Name the break: the backend can submit more than one command, use the wrong
channel, accept polling-only completion, or hide a duplicate/spurious event.

Assert the real adapter behavior through complete fakes:

- one `WorkCommandBarrier` is pushed once;
- `wait_value` equals the initial stamp, so no shader or render work is needed;
- one ring entry advances producer `0 -> 1`;
- only `agx.ch.queue[1].q_3D.run(...)` is called;
- the loop calls `asc.work()` and `poll_channels()` only until the allocated
  event fires;
- success needs done pointer `1`, event delta `1`, and matching event count `1`;
- timeout at `0.5` seconds records final pointers and raises;
- event without done-pointer progress, done-pointer progress without event,
  duplicate event, spurious ID, and ring wrap each fail.

- [ ] **Step 4: Run RED, implement submission, then run GREEN**

Run RED: `python3 -m unittest tests.test_agx_m1n1_queue_backend.QueueSubmissionTests -v`

Implement only the barrier command.  Do not import `m1n1.agx.render`,
`GPURenderer`, `GPUFrame`, TA commands, shaders, or display helpers.

Run GREEN: `python3 -m unittest tests.test_agx_m1n1_queue_backend -v`

- [ ] **Step 5: Write failing evidence and teardown tests**

Name the break: physical fault read can touch a power-gated register, canary
mutation can be hidden, or context 63 can remain bound after release.

Assert that snapshot always contains firmware, firmware fault, SGX IRQs, queue
pointers, event identity/count, stamp, mapping inventory, canary hashes, and
physical-fault readability.  A power-gated physical register is represented as
`{"readable": false, "reason": "power-domain-not-qualified"}` and is never
read.  Teardown must clear both context-63 roots under the UAT handoff lock,
flush, invalidate, then invoke the G1 reset that clears context zero.

- [ ] **Step 6: Run RED, implement evidence/teardown, then run GREEN**

Run RED: `python3 -m unittest tests.test_agx_m1n1_queue_backend.EvidenceAndTeardownTests -v`

Run GREEN: `python3 -m unittest tests.test_agx_m1n1_queue_backend tests.test_agx_m1n1_backend -v`

- [ ] **Step 7: Add a source-boundary regression test**

Run the production adapter through a source audit that rejects
`m1n1.agx.render`, `GPURenderer`, `GPUFrame`, display addresses, guest-memory
helpers, and any queue index other than the exported constant.  This audit is a
secondary defense; behavioral tests remain authoritative.

- [ ] **Step 8: Commit implementation and record its exact commit separately**

```sh
git add tools/agx_m1n1_queue_backend.py tests/test_agx_m1n1_queue_backend.py
git commit -m "gpu: add bounded m1n1 AGX barrier backend"
```

Append the exact commit to `investigation/CHANGES.csv`; commit only the ledger
with `docs: record bounded AGX barrier backend`.

---

### Task 3: Assisted cold-reset operator

**Files:**
- Create: `scripts/run-agx-queue-gate.sh`
- Create: `tests/test_run_agx_queue_gate.py`
- Modify: `tools/agx_queue_gate.py`

**Interfaces:**
- Consumes: `--proxy`, `--contract`, `--artifact-dir`, `--evidence-dir`, literal
  `--cycles 10`, and optional `--launch-stable-windows` or `--dry-run`.
- Produces: ten immutable one-shot receipts, ten fresh-proxy receipts, one
  atomic aggregate, and optional launch of the unchanged stable artifacts only
  after `verify-result` succeeds.

- [ ] **Step 1: Write failing shell-boundary tests**

Name the break: the operator can tune the deadline, reuse evidence, run with an
active guest, accept dirty/mismatched artifacts, skip reboot, or alter normal
launch files.

Copy the real artifact-manifest fixture pattern from `test_run_agx_gate.py` and
assert dry-run output contains:

```text
mode: assisted AGX G1Q queue gate
context: 63
queue: 3D index 1
commands per cycle: 1
completion deadline: 0.5 seconds
cycles: 10
reset policy: physical cold reset after every cycle
```

Assert there is no `--timeout` option.  Add separate rejection tests for cycle
count 9, nonempty evidence, missing manifest, changed artifact, unexpected
recovery entry, active guest, false aggregate, and a failed cycle.  Hash normal
launch files before/after every dry-run rejection.

- [ ] **Step 2: Run RED**

Run: `python3 -m unittest tests.test_run_agx_queue_gate -v`

Expected: script-not-found failures.

- [ ] **Step 3: Implement the minimal shell runner and CLI wiring**

Reuse `preflight_operator` from `tools.agx_gate` and the same physical reboot +
fresh-receipt sequence as the qualified G1 runner.  Invoke only
`tools.agx_queue_gate run-one`, `proxy-receipt`, `aggregate-cold`, and
`verify-result`.  Never call the G1 `run-one` as a substitute for G1Q.

- [ ] **Step 4: Run GREEN and launch-isolation regression tests**

Run:

```sh
python3 -m unittest \
  tests.test_run_agx_queue_gate \
  tests.test_run_agx_gate \
  tests.test_launch_profiles \
  tests.test_standalone_image -v
```

- [ ] **Step 5: Commit implementation and record its exact commit separately**

```sh
git add scripts/run-agx-queue-gate.sh tools/agx_queue_gate.py tests/test_run_agx_queue_gate.py
git commit -m "gpu: add cold-reset AGX queue qualification runner"
```

Append the exact commit to `investigation/CHANGES.csv`; commit only the ledger
with `docs: record AGX queue qualification runner`.

---

### Task 4: Full host verification and hardware preregistration

**Files:**
- Modify: `documentation/AGX_BRINGUP.md`
- Modify: `investigation/EXPERIMENTS.md`

**Interfaces:**
- Consumes: the exact committed implementation tree and immutable recovery manifest.
- Produces: an operator guide and one append-only experiment preregistration.

- [ ] **Step 1: Run the complete host verification before documenting a command**

Run the full root suite with no guest owner:

```sh
python3 -m unittest discover -s tests -v
```

Run the complete nested m1n1 host suite:

```sh
./m1n1_windows/tests/run_host_tests.sh
```

Run `git diff --check` and verify the immutable artifact manifest with:

```sh
python3 -m tools.artifact_manifest verify \
  .local/recovery/STABLE-j313-8core-native-input-v1/MANIFEST.json \
  --profile debug --display both --debug monitor
```

Record exact pass/fail counts rather than summarizing partial runs as complete.

- [ ] **Step 2: Update the operator guide**

Document the exact command:

```sh
./scripts/run-agx-queue-gate.sh \
  --proxy /dev/cu.usbmodem.PROXY \
  --contract config/j313-agx.json \
  --artifact-dir .local/recovery/STABLE-j313-8core-native-input-v1 \
  --evidence-dir investigation/artifacts/EXP-20260825-080-agx-g1q \
  --cycles 10
```

State the 500-ms deadline, ten cold resets, immutable evidence rule, stop
conditions, verification command, recovery procedure, and explicit non-render
status.

- [ ] **Step 3: Preregister the first one-cycle hardware probe**

Append an experiment entry with status `preregistered`.  Include exact root,
m1n1, and Mu commits; every implementation and ledger commit; contract digest;
all recovery artifact SHA-256 values; proxy device; empty evidence directory;
literal one-cycle developer command; 500-ms deadline; success fields; failure
snapshot requirements; physical-reboot rule; and prohibition on Windows launch.

The one-cycle probe is not qualification and cannot produce an aggregate or
permit Windows.

- [ ] **Step 4: Commit docs/preregistration without pushing**

```sh
git add documentation/AGX_BRINGUP.md investigation/EXPERIMENTS.md
git commit -m "docs: preregister J313 AGX queue probe"
```

Do not push before the hardware gate passes.

---

### Task 5: One-cycle hardware probe and ten-cycle qualification

**Files:**
- Append only: `investigation/EXPERIMENTS.md`
- Create locally, never track raw evidence: `investigation/artifacts/EXP-20260825-079-agx-g1q-probe/` and `investigation/artifacts/EXP-20260825-080-agx-g1q/`

**Interfaces:**
- Consumes: the preregistered source tree, immutable stable artifacts, and the live J313 proxy.
- Produces: one accepted/rejected probe record, then a separately preregistered ten-cycle qualification if and only if the probe passes unchanged.

- [ ] **Step 1: Return the Air to sole `Running proxy...` ownership**

Shut Windows down cleanly if available.  Confirm the guest PID/lock is absent,
the proxy answers with the expected J313/V13_5 identity, and the evidence
directory does not exist.  Do not kill a live guest merely to satisfy preflight.

- [ ] **Step 2: Run the preregistered one-cycle probe exactly once**

Use the registered command.  On any error, preserve evidence, physically reboot,
append the rejected result, and return to systematic root-cause investigation.
Do not layer a second fix onto the same experiment.

- [ ] **Step 3: Verify and close the one-cycle record**

Independently parse the receipt and confirm one command, exact queue progress,
one event, unchanged canary/guards, zero firmware faults, explicit physical-fault
readability, clean teardown, and changed proxy identity after reboot.  Append
actual timings and hashes to the experiment ledger.

- [ ] **Step 4: Preregister and commit the ten-cycle qualification**

Create a new empty evidence directory and a new experiment entry.  Pin the
unchanged implementation commits and hashes.  The command must use the public
runner with literal `--cycles 10`; no deadline override exists.

- [ ] **Step 5: Run all ten cold-reset-separated cycles**

Stop on the first failure.  Never retry a cycle or reuse an evidence directory.
After cycle ten, independently run:

```sh
./proxyenv/bin/python -m tools.agx_queue_gate verify-result \
  investigation/artifacts/EXP-20260825-080-agx-g1q/gate-result.json
```

- [ ] **Step 6: Launch unchanged stable Windows only after verification**

Use the immutable recovery artifacts with display `both` and debug `monitor`.
Require lock screen inside 30 seconds, eight CPUs, NVMe, physical xHCI, native
keyboard and Precision Touchpad, advancing physical display, no bugcheck, and
no AGX fault.  This checks non-regression; it does not claim acceleration.

- [ ] **Step 7: Close evidence, commit the result, and push**

Append exact aggregate digest, cycle timing range, all fault/IRQ counts, proxy
identity results, and Windows health result.  Update `AGX_BRINGUP.md` with the
qualified result and explicit limitations.  Run full verification again, then:

```sh
git add documentation/AGX_BRINGUP.md investigation/EXPERIMENTS.md
git commit -m "docs: qualify J313 AGX queue completion"
git push origin feature/j313-gpu-acceleration
```

Push only when every host check, ten-cycle hardware check, and post-gate Windows
check has fresh evidence.

---

## Plan self-review checklist

- Every G1Q spec requirement maps to a task above.
- The plan contains no shader, pixel, display, UMD, or production WDDM claim.
- Hardware imports stay isolated in one backend.
- G1 lifecycle logic is composed, not copied.
- Every wait has the exact 500-ms deadline or the already-qualified G1 deadline.
- Every failure blocks Windows and requires cold reboot.
- Every production change has an explicit RED and GREEN command.
- Every implementation commit has a subsequent ledger-only commit.
- Hardware execution follows a committed preregistration and uses immutable evidence.
- Normal launch and standalone artifacts remain unchanged.
