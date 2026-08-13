# J313 Windows Platform Stability Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate reproducible whole-system pauses, SGI storms, `CLOCK_WATCHDOG_TIMEOUT`, and `IPI_WATCHDOG_TIMEOUT` while retaining all eight M1 cores and the production standalone boot path.

**Architecture:** Keep correctness instrumentation in fixed-size, per-CPU m1n1 flight recorders rather than printing from interrupt/timer hot paths. A host collector correlates those records with UART, framebuffer age, Windows SSH liveness, and bugcheck capture; a deterministic classifier selects exactly one timer/vGIC/PSCI hypothesis for the next experiment.

**Tech Stack:** m1n1 freestanding C/EL2, ARM64 architectural timer, GICv3/vGIC list registers, PSCI, Mu ACPI MADT/GTDT/PPTT, Python 3 `unittest`, pyserial, Windows PowerShell/OpenSSH, KD helpers, J313 hardware.

## Global Constraints

- Target hardware is MacBook Air M1 `j313` / T8103.
- Preserve the known-good production image and its hashes before installing a diagnostic image.
- First capture uses the current eight-core configuration; topology reduction is allowed only after the evidence identifies a CPU or cluster boundary.
- One experiment changes one variable. Every run records artifact hashes and launch-profile flags.
- Do not disable Windows watchdogs, drop SGIs, mask timer PPIs, pin Windows to one core, or report a run as fixed because the bugcheck was hidden.
- Do not print per interrupt, timer tick, WFI transition, or SGI. Hot-path evidence goes to fixed-size per-CPU rings.
- Diagnostic capture must never block guest execution and must remain valid if host USB disconnects.
- A stale framebuffer is informational. Freeze classification requires CPU, timer, interrupt, and Windows-liveness evidence.
- No change to keyboard/trackpad, memory layout, NVMe queueing, GPU, audio, or display policy belongs in this plan.
- Do not add assistant attribution, session URLs, or `Co-Authored-By` trailers to commits.
- Do not commit raw Windows data, serial identifiers, crash dumps, firmware binaries, or private keys.

## File Map

### Root repository

- Create `tools/platform_stability.py`: capture supervisor, artifact manifest, SSH probe, parser, and deterministic classification.
- Create `tests/test_platform_stability.py`: parser, counter-wrap, incomplete-link, and classifier tests.
- Create `scripts/capture-stability.sh`: one-command capture wrapper with explicit serial devices and output directory.
- Modify `scripts/run-assisted.sh`: opt-in `STABILITY_CAPTURE=1` integration; unchanged default behavior.
- Modify `scripts/log-standalone.sh`: decode the same records for standalone diagnostic images.
- Create `documentation/PLATFORM_STABILITY.md`: operator runbook and result template.
- Modify `documentation/DEBUGGING.md`, `LIMITATIONS.md`, and `ROADMAP.md` only when hardware evidence changes their claims.

### m1n1 fork

- Create `m1n1_windows/src/hv_stability_trace.h`: versioned event and snapshot ABI.
- Create `m1n1_windows/src/hv_stability_trace.c`: per-CPU fixed rings and freeze snapshot.
- Create `m1n1_windows/tests/hv_stability_trace_test.c`: wrap, ordering, ABI, and snapshot tests.
- Modify `m1n1_windows/src/hv_exc.c`: bounded timer, SGI, WFI, guest-entry, and guest-exit record sites.
- Modify `m1n1_windows/src/hv_vgic.c`: LR insert/retire and pending/active transition record sites.
- Modify `m1n1_windows/src/hv.c`: low-rate drain/snapshot service outside the exception hot path.
- Modify `m1n1_windows/src/proxy.c` and `proxyclient/m1n1/proxy.py`: explicit read-only trace snapshot request.
- Modify `m1n1_windows/Makefile`: compile and host-test the trace implementation.

### Mu fork

- Modify only if evidence proves an ACPI defect: J313 MADT, GTDT, or PPTT source and its existing contract test.

---

### Task 1: Freeze the diagnostic baseline

**Files:**
- Create: `.local/platform-stability/baseline/manifest.json` (runtime artifact, ignored)
- Create: `.local/platform-stability/baseline/hashes.txt` (runtime artifact, ignored)
- Create: `documentation/PLATFORM_STABILITY.md`

**Interfaces:**
- Consumes: current production and diagnostic `boot.bin`, m1n1/Mu/root Git IDs, firmware hash, launch profile, Windows build, and BCD state.
- Produces: an immutable baseline manifest used by every later capture.

- [ ] **Step 1: Record repository and artifact identities**

Run from the public root:

```bash
mkdir -p .local/platform-stability/baseline
git rev-parse HEAD
git -C m1n1_windows rev-parse HEAD
git -C mu rev-parse HEAD
shasum -a 256 firmware/J313_EFI_8core.fd m1n1_windows/build/m1n1.macho
```

Record the output, active launch-profile flags, Windows build number, enabled
processor count, and the SHA-256 of the installed production and candidate
diagnostic ESP images in `manifest.json` and `hashes.txt`.

- [ ] **Step 2: Record the pre-change symptom contract**

The runbook must distinguish these outcomes:

```text
healthy          all CPU progress advances; SSH and UI respond
ui_pause         CPU/timers/SSH advance; only desktop/RDP presentation stalls
guest_freeze     CPU progress stops while EL2/USB capture remains alive
timer_loss       one CPU stops with enabled, expired virtual timer and no queued/LR PPI
sgi_storm        SGI0 rate > 10000/s for 2 s with no matching useful progress
cpu_stall        one CPU progress stops while peers and architectural counter advance
transport_loss   USB capture disappears without sufficient CPU evidence
bugcheck         stop code and four bugcheck parameters captured
host_reset       fresh boot banner appears without a captured Windows bugcheck
```

- [ ] **Step 3: Document recovery boundaries**

Record the exact reversible ESP restore command, known-good image hash, normal
Windows shutdown command, and the rule that external USB input remains attached.

- [ ] **Step 4: Commit the runbook only**

```bash
git add documentation/PLATFORM_STABILITY.md
git commit -m "docs: define J313 stability capture baseline"
```

Do not commit `.local/platform-stability`.

### Task 2: Define and test the capture/classification contract

**Files:**
- Create: `tools/platform_stability.py`
- Create: `tests/test_platform_stability.py`

**Interfaces:**
- Produces: `TraceEvent`, `CpuSnapshot`, `ProbeSample`, and `RunClassification` immutable dataclasses.
- Produces: `classify_run(events, snapshots, probes, link_events) -> RunClassification`.
- Produces: CLI `python3 tools/platform_stability.py capture|classify|report ...`.

- [ ] **Step 1: Write failing classifier tests**

Cover the nine outcomes from Task 1 with synthetic fixtures. Include 32-bit and
64-bit counter wrap, missing CPUs, out-of-order USB chunks, duplicated chunks,
stale framebuffer with live SSH, USB loss after a complete freeze snapshot, and
USB loss before any snapshot.

The essential assertions are:

```python
self.assertEqual(classify(timer_lost_fixture).kind, "timer_loss")
self.assertEqual(classify(sgi_storm_fixture).kind, "sgi_storm")
self.assertEqual(classify(stale_fb_live_ssh_fixture).kind, "ui_pause")
self.assertEqual(classify(early_disconnect_fixture).kind, "transport_loss")
```

- [ ] **Step 2: Verify RED**

Run:

```bash
python3 -m unittest tests.test_platform_stability -v
```

Expected: import failure because `tools/platform_stability.py` does not exist.

- [ ] **Step 3: Implement strict parsing and classification**

Reject unknown ABI versions, invalid CPU indices, impossible counter regressions,
and truncated records. Preserve incomplete runs as `transport_loss`; never infer
`guest_freeze` solely from missing USB or framebuffer updates.

- [ ] **Step 4: Verify GREEN**

Run:

```bash
python3 -m unittest tests.test_platform_stability -v
python3 -m unittest discover -s tests -v
```

Expected: PASS.

- [ ] **Step 5: Commit**

```bash
git add tools/platform_stability.py tests/test_platform_stability.py
git commit -m "test: classify J313 platform stalls"
```

### Task 3: Add a non-blocking per-CPU EL2 flight recorder

**Files:**
- Create: `m1n1_windows/src/hv_stability_trace.h`
- Create: `m1n1_windows/src/hv_stability_trace.c`
- Create: `m1n1_windows/tests/hv_stability_trace_test.c`
- Modify: `m1n1_windows/Makefile`

**Interfaces:**
- Produces: `void hv_stability_record(u32 cpu, u16 type, u32 arg0, u64 arg1)`.
- Produces: `bool hv_stability_snapshot(struct hv_stability_snapshot *out)`.
- Produces: `size_t hv_stability_drain(void *dst, size_t capacity)`.
- ABI begins with magic `JSTB`, version `1`, record size, CPU count, and monotonic sequence.

- [ ] **Step 1: Write failing host tests**

Test independent eight-CPU rings, ring wrap, monotonic sequence, partial drain,
snapshot consistency, and a writer interrupted by a snapshot. The writer must not
allocate, print, wait on USB, or take a global lock.

- [ ] **Step 2: Verify RED**

Run:

```bash
make -C m1n1_windows test-hv-stability-trace
```

Expected: missing source/API failure.

- [ ] **Step 3: Implement the fixed-size recorder**

Use one power-of-two ring per CPU and release/acquire publication of complete
records. A record contains sequence, architectural counter, CPU, type, two
arguments, and a commit marker. Overwrite the oldest record on wrap and increment
an explicit lost-record counter.

- [ ] **Step 4: Verify GREEN and existing hypervisor tests**

Run:

```bash
make -C m1n1_windows test-hv-stability-trace
make -C m1n1_windows test-hv-vgic
make -C m1n1_windows test-hv-irq-routes
```

Expected: PASS.

- [ ] **Step 5: Commit in the m1n1 fork**

```bash
git -C m1n1_windows add src/hv_stability_trace.c src/hv_stability_trace.h \
  tests/hv_stability_trace_test.c Makefile
git -C m1n1_windows commit -m "diag: add per-CPU stability flight recorder"
```

### Task 4: Instrument only the state transitions needed to classify a freeze

**Files:**
- Modify: `m1n1_windows/src/hv_exc.c`
- Modify: `m1n1_windows/src/hv_vgic.c`
- Modify: `m1n1_windows/src/hv.c`
- Modify: `m1n1_windows/src/hv_stability_trace.h`
- Modify: `m1n1_windows/tests/hv_stability_trace_test.c`

**Interfaces:**
- Adds event types: `GUEST_ENTER`, `GUEST_EXIT`, `WFI_ENTER`, `WFI_EXIT`,
  `TIMER_PROGRAM`, `TIMER_EXPIRE`, `PPI_QUEUE`, `SGI_SEND`, `SGI_ACCEPT`,
  `IRQ_LR_INSERT`, `IRQ_LR_RETIRE`, and `CPU_PROGRESS`.
- Snapshot contains, per CPU: PC, PSTATE, CNTV_CTL/CVAL, current counter, VMCR,
  HCR, APRs, LR contents, pending timer/SGI state, WFI state, and event counters.

- [ ] **Step 1: Extend failing tests with event-rate and snapshot invariants**

Verify disabled tracing is a near-empty branch, rate-limited `CPU_PROGRESS` emits
at most 20 records per second per CPU, and no event records an uninitialized LR.

- [ ] **Step 2: Add bounded record sites**

Record transitions after their architectural state change is committed. Do not
record every generic MMIO access, every tick, or every loop iteration. Drain only
from the existing low-rate EL2 service path; USB backpressure drops exported
chunks but never blocks or stops guest entry.

- [ ] **Step 3: Add automatic freeze snapshot triggering**

If one CPU's progress counter is unchanged for two seconds while the platform
counter and any peer advance, capture one snapshot and latch the trigger. Re-arm
only after all online CPUs advance for five seconds. A high SGI rate captures a
snapshot but does not mask or alter the SGI.

- [ ] **Step 4: Run tests and compare disabled-build size**

Run:

```bash
make -C m1n1_windows test-hv-stability-trace test-hv-vgic test-hv-irq-routes
make -C m1n1_windows RELEASE=1
```

Expected: PASS; the production profile compiles tracing out or leaves it disabled
without per-event output.

- [ ] **Step 5: Commit**

```bash
git -C m1n1_windows add src/hv_exc.c src/hv_vgic.c src/hv.c \
  src/hv_stability_trace.h tests/hv_stability_trace_test.c
git -C m1n1_windows commit -m "diag: capture timer and vGIC stall state"
```

### Task 5: Expose snapshots and build one-command host capture

**Files:**
- Modify: `m1n1_windows/src/proxy.c`
- Modify: `m1n1_windows/proxyclient/m1n1/proxy.py`
- Create: `scripts/capture-stability.sh`
- Modify: `scripts/run-assisted.sh`
- Modify: `scripts/log-standalone.sh`
- Modify: `tools/platform_stability.py`
- Modify: `tests/test_platform_stability.py`

**Interfaces:**
- Produces: read-only proxy command `hv_stability_read(sequence, capacity)`.
- Produces: `scripts/capture-stability.sh --console DEV --vuart DEV --host HOST --output DIR`.
- Produces in each run directory: `manifest.json`, `console.raw`, `vuart.raw`,
  `trace.bin`, `ssh.jsonl`, `events.jsonl`, `classification.json`, and `report.txt`.

- [ ] **Step 1: Add failing fragmented-transfer and reconnect tests**

Assert that repeated reads resume from sequence, duplicated chunks are ignored,
an overrun is explicit, serial reconnect starts a new generation, and the last
complete in-memory EL2 snapshot remains classifiable.

- [ ] **Step 2: Implement the read-only proxy endpoint**

Return immediately with the records already available. Never wait for a future
record and never hold the big hypervisor lock while USB transmits.

- [ ] **Step 3: Implement the capture wrapper**

The wrapper validates both serial devices are distinct, validates the baseline
hashes, starts raw capture before guest launch, probes Windows once per second
over SSH with a two-second timeout, and writes artifacts atomically under a new
timestamped directory. `Ctrl-C` finalizes the report instead of deleting partial
evidence.

- [ ] **Step 4: Verify host tests and shell syntax**

Run:

```bash
python3 -m unittest tests.test_platform_stability -v
bash -n scripts/capture-stability.sh scripts/run-assisted.sh scripts/log-standalone.sh
python3 -m unittest discover -s tests -v
```

Expected: PASS.

- [ ] **Step 5: Commit both repositories**

```bash
git -C m1n1_windows add src/proxy.c proxyclient/m1n1/proxy.py
git -C m1n1_windows commit -m "diag: export stability snapshots"
git add scripts/capture-stability.sh scripts/run-assisted.sh \
  scripts/log-standalone.sh tools/platform_stability.py \
  tests/test_platform_stability.py
git commit -m "diag: capture correlated Windows stalls"
```

### Task 6: Capture the first complete eight-core failure

**Files:**
- Runtime: `.local/platform-stability/<timestamp>/`
- Create after sanitization: `documentation/results/<date>-j313-stability.md`

**Interfaces:**
- Consumes: exact diagnostic image built from Tasks 3-5 and the unchanged Windows installation.
- Produces: one complete classified failure or a completed 60-minute acceptance run.

- [ ] **Step 1: Arm capture before reboot**

Resolve the Windows hostname or current address first. The example uses the
stable local DNS name `windows-m1.local`; replace it only when the test machine
does not publish that name. Then run with the actual two USB serial device paths:

```bash
scripts/capture-stability.sh \
  --console /dev/cu.usbmodemCONSOLE \
  --vuart /dev/cu.usbmodemVUART \
  --host windows-m1.local \
  --output .local/platform-stability
```

Expected: `ARMED` followed by a new run directory and validated hashes. Do not
reboot before `ARMED`.

- [ ] **Step 2: Reboot once and reach Windows**

Use the diagnostic profile with eight cores. Do not change core count, display
mode, NVMe mode, BCD, or Windows services in this run.

- [ ] **Step 3: Apply one repeatable workload**

After SSH reports ready, start Steam and continue one download/install while
moving a window periodically. Stop after the first user-visible pause, bugcheck,
reset, or after 60 healthy minutes. Record the wall-clock time of any visible
pause, but do not reset the machine until the collector finalizes or USB is
confirmed lost.

- [ ] **Step 4: Generate and inspect the report**

Run:

```bash
python3 tools/platform_stability.py classify .local/platform-stability/<timestamp>
python3 tools/platform_stability.py report .local/platform-stability/<timestamp>
```

Expected: exactly one primary classification plus evidence completeness flags.

- [ ] **Step 5: Publish only a sanitized result**

The result document records hashes, topology, workload, duration, classification,
per-CPU delta table, timer/vGIC facts, and the next single hypothesis. It excludes
serial paths, credentials, raw crash memory, and private Windows data.

### Task 7: Select the next experiment from evidence, not symptoms

**Files:**
- Modify only the component selected by the classification.
- Create a focused regression test beside that component.

**Interfaces:**
- Consumes: `classification.json` from Task 6.
- Produces: one falsifiable hypothesis, one code change, and one repeat run.

- [ ] **Step 1: Apply this deterministic decision table**

```text
timer_loss  -> inspect CNTV_CTL/CVAL, PPI pending/queued/LR, re-arm and WFI wake
sgi_storm   -> inspect source-target matrix, pending/active transitions, EOI and resend
cpu_stall   -> inspect last guest exit, WFI state, PSCI context, redistributor ownership
guest_freeze-> inspect global vGIC/lock progress and shared EL2 service ownership
ui_pause    -> capture Windows ETW/RDP/GDI evidence; do not change timer/vGIC code
bugcheck    -> decode stop parameters and stalled CPU from KD/crash data first
host_reset  -> inspect EL2 exception/panic and boot-generation boundary
transport_loss -> repair capture path and repeat unchanged; no platform fix is authorized
```

- [ ] **Step 2: Write the failing regression test before changing behavior**

The test reconstructs the exact pre-failure state from the trace and asserts the
required architectural outcome. It must fail on the captured revision.

- [ ] **Step 3: Implement the minimum correction**

Touch only the selected timer, vGIC, PSCI, or ACPI component. Preserve tracing and
all unrelated launch contracts.

- [ ] **Step 4: Run focused tests, all platform contract tests, and rebuild**

Run the component test, `test-hv-vgic`, `test-hv-irq-routes`, launch-contract
tests, root Python tests, m1n1 build, and Mu J313 build. All must pass before the
next hardware boot.

- [ ] **Step 5: Repeat the identical eight-core workload**

Use the same Windows installation, launch profile, display mode, and workload.
If the primary classification changes, preserve both runs and return to Step 1;
do not stack a second speculative fix.

### Task 8: Complete the stability acceptance gate

**Files:**
- Create: `documentation/results/<date>-j313-eight-core-stability.md`
- Modify: `documentation/LIMITATIONS.md`
- Modify: `documentation/DEBUGGING.md`
- Modify: `documentation/ROADMAP.md`

**Interfaces:**
- Produces: a reviewed hardware acceptance record and known-good artifact hashes.

- [ ] **Step 1: Run 20 cold boots**

Each boot must reach sign-in, report eight online processors, respond over SSH,
and shut down normally. Record duration and result; do not discard failed boots.

- [ ] **Step 2: Run the 60-minute mixed workload**

Run Steam download/install, sustained disk writes, network traffic, RDP/SSH
liveness, and continuous per-CPU monitoring. Any global pause over two seconds,
watchdog, reset, or lost CPU fails the gate.

- [ ] **Step 3: Verify diagnostics are observational**

Repeat a 30-minute subset with flight recording disabled. CPU throughput and
latency must remain within one percent of the diagnostic run after excluding USB
export overhead.

- [ ] **Step 4: Update public claims and commit**

Document exact evidence, remaining limitations, artifact hashes, and recovery.
Do not call the platform stable if any acceptance condition failed.

- [ ] **Step 5: Tag only after hardware acceptance**

Create and push a milestone tag only after root, m1n1, and Mu commits are pushed
and the tag message references the sanitized acceptance record.

## Exit condition

This plan is complete only when the Phase 0 acceptance gate in
`documentation/ROADMAP.md` passes. Built-in keyboard/trackpad implementation then
resumes from its existing approved plan. Memory, storage, GPU, audio, and external
display work must not be used as concurrent explanations for an unresolved CPU,
timer, or interrupt freeze.
