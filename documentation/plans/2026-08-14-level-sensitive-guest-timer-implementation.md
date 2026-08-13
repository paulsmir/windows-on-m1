# Level-Sensitive Guest Timer Delivery Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Eliminate J313's minute-scale Windows stalls by making m1n1 preserve every asserted architectural-timer level through LR acknowledgement and EOI without relying on a high-frequency recovery heartbeat.

**Architecture:** A pure, host-tested LR transition helper defines Pending, Active, and Active+Pending behavior. A per-CPU timer delivery owner synchronizes the sampled Apple timer line with one live LR or one deferred queue entry, derives the old diagnostic latch from real ownership, and recomputes `HCR_EL2.VI` after mutations. The existing 100 Hz secondary heartbeat remains unchanged for EXP-019 so hardware testing changes only timer/vGIC synchronization.

**Tech Stack:** m1n1 freestanding C, Apple architectural timer FIQ routing, Arm GICv3 virtual list registers, C host-test harness, Python `unittest`, assisted J313 launch tooling.

**Spec:** `documentation/design/2026-08-14-level-sensitive-guest-timer.md`

## Global Constraints

- Work only in `/Users/pavel/public_windows`; `/Users/pavel/windows` and worktrees are not build or launch sources.
- Target MacBook Air M1 J313/T8103 with all eight CPUs.
- Do not change Mu, ACPI, Windows, NVMe, USB, display, Apple input, CPU topology, or the 100 Hz secondary recovery heartbeat.
- Do not attach a second proxy client while the assisted launcher owns USB callbacks.
- Every production change begins with a test observed failing for the intended reason.
- Record EXP-019 before building or launching its artifact and append the actual result afterward.
- Preserve EXP-016 as the recovery artifact and never replace the ESP during EXP-019.
- Do not add assistant attribution, session URLs, or `Co-Authored-By` trailers.
- Do not call the result stable after one boot; hardware acceptance requires the checkpoints in the spec.

## File Map

### m1n1 fork

- Modify `m1n1_windows/src/hv_vgic_diag.h`: declare the pure level-LR transition result.
- Modify `m1n1_windows/src/hv_vgic_diag.c`: implement asserted/deasserted LR transitions.
- Modify `m1n1_windows/tests/hv_vgic_diag_test.c`: cover every transition with literal expected values.
- Create `m1n1_windows/src/hv_timer_delivery.h`: define a small host-testable deferred-delivery owner independent of hardware registers.
- Create `m1n1_windows/src/hv_timer_delivery.c`: implement unique queued ownership, withdrawal, and pop validation.
- Create `m1n1_windows/tests/hv_timer_delivery_test.c`: exercise duplicate suppression, deassertion, and queue order.
- Modify `m1n1_windows/tests/run_host_tests.sh`: compile the new focused test.
- Modify `m1n1_windows/Makefile`: link `hv_timer_delivery.o` into m1n1.
- Modify `m1n1_windows/src/hv_exc.c`: replace boolean-first timer delivery with the unified synchronizer for INTIDs 17 and 18.
- Modify `m1n1_windows/src/hv_vgic.c`: notify the timer owner after software EOI frees or repends an LR and recompute VI once.
- Modify `m1n1_windows/src/hv_vgic.h`: expose only the narrow timer-drain hook required by EOI.
- Modify `m1n1_windows/src/hv_watchdog_snapshot.h`: retain ABI fields but document/consume them as derived ownership.

### root integration repository

- Modify `tests/test_vgic_irq_queue_contract.py`: assert the integration ordering at the C boundary.
- Modify `investigation/EXPERIMENTS.md`: append EXP-019 intent before launch and result afterward.
- Modify `investigation/CHANGES.csv`: index the implementation commit after software verification, then mark it validated or rejected after hardware evidence.

---

### Task 1: Specify every LR level transition in executable tests

**Files:**
- Modify: `m1n1_windows/tests/hv_vgic_diag_test.c`
- Modify: `m1n1_windows/src/hv_vgic_diag.h`
- Modify: `m1n1_windows/src/hv_vgic_diag.c`

**Interfaces:**
- Produces: `struct hv_vgic_level_result hv_vgic_diag_sync_level_lr(u64 lr, bool asserted)`.
- `hv_vgic_level_result.lr` is the next LR value.
- `hv_vgic_level_result.changed` reports a real LR mutation.
- `hv_vgic_level_result.newly_pending` is true only when synchronization adds Pending to a state that lacked it.

- [ ] **Step 1: Write the failing transition-table test**

Add a table-driven test whose expected values are literal and independent of the implementation:

```c
static void test_level_sync_covers_every_lr_state(void)
{
    const uint64_t p = 1ULL << 62;
    const uint64_t a = 1ULL << 63;
    const uint64_t payload = (0x20ULL << 48) | 18;
    const struct {
        uint64_t before;
        bool asserted;
        uint64_t after;
        bool changed;
        bool newly_pending;
    } cases[] = {
        {payload, false, payload, false, false},
        {p | payload, false, payload, true, false},
        {a | payload, false, a | payload, false, false},
        {a | p | payload, false, a | payload, true, false},
        {p | payload, true, p | payload, false, false},
        {a | payload, true, a | p | payload, true, true},
        {a | p | payload, true, a | p | payload, false, false},
    };

    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        struct hv_vgic_level_result got =
            hv_vgic_diag_sync_level_lr(cases[i].before, cases[i].asserted);
        assert(got.lr == cases[i].after);
        assert(got.changed == cases[i].changed);
        assert(got.newly_pending == cases[i].newly_pending);
    }
}
```

The Empty+asserted case is intentionally excluded: creating a correctly formatted new LR needs the INTID and programmed priority and remains the delivery owner's responsibility.

- [ ] **Step 2: Run RED and retain the failure output**

Run:

```bash
cd /Users/pavel/public_windows/m1n1_windows
./tests/run_host_tests.sh hv_vgic_diag_test
```

Expected: compile failure because `hv_vgic_level_result` and `hv_vgic_diag_sync_level_lr()` do not exist.

- [ ] **Step 3: Implement the minimal pure transition**

Implement only the state-bit transformation. Preserve all INTID, priority, group, and hardware payload bits. Assertion ORs Pending into a live LR; deassertion clears Pending and never clears Active. Do not read hardware registers, queues, or global state in this helper.

- [ ] **Step 4: Run GREEN and the existing EOI regression**

Run the focused command again. Expected: `hv_vgic_diag_test: ok`, including the existing `Active+Pending -> Pending` EOI assertion.

- [ ] **Step 5: Mutation-check the test**

Temporarily make Active+asserted return Active-only. Confirm the focused test fails at the literal Active+Pending expectation, then restore the correct implementation and rerun GREEN. Do not commit the temporary mutation.

### Task 2: Make deferred timer ownership unique and withdrawable

**Files:**
- Create: `m1n1_windows/src/hv_timer_delivery.h`
- Create: `m1n1_windows/src/hv_timer_delivery.c`
- Create: `m1n1_windows/tests/hv_timer_delivery_test.c`
- Modify: `m1n1_windows/tests/run_host_tests.sh`
- Modify: `m1n1_windows/Makefile`

**Interfaces:**
- Produces: `struct hv_timer_delivery_queue` with fixed capacity two, one slot for INTID 17 and one for INTID 18.
- Produces: `bool hv_timer_delivery_assert(struct hv_timer_delivery_queue *q, u32 intid, u8 priority)`; returns true only when it creates new deferred ownership.
- Produces: `bool hv_timer_delivery_deassert(struct hv_timer_delivery_queue *q, u32 intid)`; removes stale deferred ownership.
- Produces: `bool hv_timer_delivery_pop(struct hv_timer_delivery_queue *q, struct hv_timer_delivery *out)`; yields only currently asserted unique ownership in FIFO order.
- Produces: `bool hv_timer_delivery_contains(const struct hv_timer_delivery_queue *q, u32 intid)`.

- [ ] **Step 1: Write failing ownership tests**

Test these real behaviors:

```c
static void test_repeated_assertion_has_one_owner(void)
{
    struct hv_timer_delivery_queue q = {0};
    assert(hv_timer_delivery_assert(&q, 18, 0x20));
    assert(!hv_timer_delivery_assert(&q, 18, 0x20));
    assert(hv_timer_delivery_contains(&q, 18));
}

static void test_deassertion_withdraws_stale_delivery(void)
{
    struct hv_timer_delivery_queue q = {0};
    struct hv_timer_delivery out = {0};
    assert(hv_timer_delivery_assert(&q, 18, 0x20));
    assert(hv_timer_delivery_deassert(&q, 18));
    assert(!hv_timer_delivery_pop(&q, &out));
}

static void test_two_timer_sources_keep_fifo_order(void)
{
    struct hv_timer_delivery_queue q = {0};
    struct hv_timer_delivery out = {0};
    assert(hv_timer_delivery_assert(&q, 17, 0x30));
    assert(hv_timer_delivery_assert(&q, 18, 0x20));
    assert(hv_timer_delivery_pop(&q, &out) && out.intid == 17);
    assert(hv_timer_delivery_pop(&q, &out) && out.intid == 18);
}
```

- [ ] **Step 2: Run RED**

Add `hv_timer_delivery_test` to `run_host_tests.sh` with `src/hv_timer_delivery.c`, then run:

```bash
cd /Users/pavel/public_windows/m1n1_windows
./tests/run_host_tests.sh hv_timer_delivery_test
```

Expected: missing header/API failure before the production files exist.

- [ ] **Step 3: Implement the fixed two-source owner**

Use no allocation, printing, hardware access, or generic 32-entry `virq_queue_t`. A two-source fixed owner makes duplicate suppression and withdrawal explicit and avoids compacting a concurrent generic ring. Reject INTIDs other than 17 and 18. `pop()` clears ownership only after copying a valid asserted entry.

- [ ] **Step 4: Run GREEN and add the object to m1n1**

Run the focused test, then add `hv_timer_delivery.o` to `OBJECTS` and run:

```bash
./tests/run_host_tests.sh hv_timer_delivery_test hv_vgic_diag_test
```

Expected: both tests print `ok`.

### Task 3: Replace the boolean-first production timer path

**Files:**
- Modify: `m1n1_windows/src/hv_exc.c`
- Modify: `m1n1_windows/src/hv_vgic.c`
- Modify: `m1n1_windows/src/hv_vgic.h`
- Modify: `m1n1_windows/src/hv_watchdog_snapshot.h`
- Modify: `tests/test_vgic_irq_queue_contract.py`

**Interfaces:**
- Produces in `hv_exc.c`: `static bool hv_sync_timer_level(u32 intid, bool asserted)`.
- Produces in `hv_exc.c`: `void hv_vgic3_drain_timer_queue(void)` for the software EOI and maintenance paths.
- Consumes `hv_vgic_diag_sync_level_lr()` and the per-CPU `hv_timer_delivery_queue`.

- [ ] **Step 1: Write failing integration-contract tests**

Extend `tests/test_vgic_irq_queue_contract.py` with behavior-boundary assertions:

```python
def test_timer_assertion_syncs_a_live_lr_before_considering_new_delivery(self):
    update = function_body(HV_EXC.read_text(), "static bool hv_sync_timer_level(")
    self.assertIn("hv_vgic_diag_sync_level_lr", update)
    self.assertLess(update.index("hv_vgic_diag_sync_level_lr"),
                    update.index("hv_vgic3_get_free_lr"))

def test_timer_deassertion_withdraws_deferred_ownership(self):
    update = function_body(HV_EXC.read_text(), "static bool hv_sync_timer_level(")
    self.assertIn("hv_timer_delivery_deassert", update)

def test_timer_eoi_drains_deferred_timer_delivery_before_vi_recompute(self):
    eoi = function_body(HV_VGIC.read_text(), "void hv_vgic3_do_eoir1(u64 reg)")
    self.assertIn("hv_vgic3_drain_timer_queue();", eoi)
    self.assertLess(eoi.index("hv_vgic3_drain_timer_queue();"),
                    eoi.index("hv_vgic3_update_vi();"))
```

These source-boundary tests supplement, rather than replace, the real C transition and queue behavior tests.

- [ ] **Step 2: Run RED**

Run:

```bash
cd /Users/pavel/public_windows
./proxyenv/bin/python -m unittest tests.test_vgic_irq_queue_contract -v
```

Expected: failures because the unified synchronizer and timer drain hook do not exist.

- [ ] **Step 3: Implement one synchronizer for INTIDs 17 and 18**

For each sampled line:

1. Read all live LRs once and find the matching INTID.
2. If found, apply `hv_vgic_diag_sync_level_lr()`, write only on change, and do not create or retain a queued duplicate.
3. If not found and asserted, inject one Pending LR when free; otherwise create one deferred owner.
4. If deasserted, withdraw deferred ownership and leave no Pending-only state.
5. Derive `timer_p_injected[cpu]` and `timer_v_injected[cpu]` from live-or-deferred ownership only for watchdog ABI compatibility.
6. Recompute VI after any LR or deferred-to-LR mutation and execute `isb` before returning to the guest.

Both CNTP and CNTV branches call this function. Delete `timer_irq_outstanding()` and `timer_repend_live_irq()` after no call sites remain. The sampled ISTATUS value, not an injected boolean, controls the Apple FIQ mask/unmask decision.

- [ ] **Step 4: Drain deferred timers at every LR-release boundary**

Replace direct maintenance-path popping of `PERCPU(timer_queue)` with `hv_vgic3_drain_timer_queue()`. Call the same hook in `hv_vgic3_do_eoir1()` before `hv_vgic3_update_vi()`. The hook injects deferred ownership only while an LR is free; it never duplicates a live INTID and never injects a deasserted source.

No remote timer synchronization exists in this path, so do not add speculative physical IPIs. Local Apple timer FIQ entry is already the physical wake event. Preserve the existing race-safe IPI+SEV path for actual cross-CPU work unchanged.

- [ ] **Step 5: Run GREEN**

Run:

```bash
cd /Users/pavel/public_windows
./proxyenv/bin/python -m unittest tests.test_vgic_irq_queue_contract -v
cd m1n1_windows
./tests/run_host_tests.sh hv_vgic_diag_test hv_timer_delivery_test hv_fiq_fast_path_test hv_tick_policy_test
```

Expected: all focused suites pass; `hv_tick_policy_test` confirms the unchanged 100 Hz secondary rate.

### Task 4: Verify the complete software surface and commit m1n1

**Files:**
- All m1n1 files from Tasks 1-3.
- No root ledger update until the 40-character implementation commit exists.

**Interfaces:**
- Produces one reviewable m1n1 implementation commit used by EXP-019.

- [ ] **Step 1: Run the complete nested host suite**

```bash
cd /Users/pavel/public_windows/m1n1_windows
./tests/run_host_tests.sh
```

Expected: every listed host test prints `ok` and the script exits zero.

- [ ] **Step 2: Run the focused SMP suite**

```bash
cd /Users/pavel/public_windows/m1n1_windows
../proxyenv/bin/python -m unittest \
  tests.python.test_proxyclient.test_m1n1.test_smp_secondary_launch -v
```

Expected: 10 tests pass.

- [ ] **Step 3: Run the complete root suite and hygiene checks**

```bash
cd /Users/pavel/public_windows
./proxyenv/bin/python -m unittest discover -s tests -v
git diff --check
git -C m1n1_windows diff --check
```

Expected: all tests pass and both diff checks are silent.

- [ ] **Step 4: Review the diff for scope**

Confirm that there is no Mu, ACPI, Windows driver, NVMe, USB, display, Apple-input, CPU-topology, or heartbeat-rate change. Confirm that no unconditional timer print was added to a hot path.

- [ ] **Step 5: Commit m1n1 without attribution trailers**

```bash
cd /Users/pavel/public_windows/m1n1_windows
git add src/hv_exc.c src/hv_vgic.c src/hv_vgic.h \
  src/hv_vgic_diag.c src/hv_vgic_diag.h src/hv_timer_delivery.c \
  src/hv_timer_delivery.h src/hv_watchdog_snapshot.h \
  tests/hv_vgic_diag_test.c tests/hv_timer_delivery_test.c \
  tests/run_host_tests.sh Makefile
git commit -m "hv: preserve level-sensitive guest timer delivery"
```

### Task 5: Record and build EXP-019 before touching hardware

**Files:**
- Modify: `investigation/EXPERIMENTS.md`
- Modify: `investigation/CHANGES.csv`
- Create locally: `investigation/artifacts/EXP-20260814-019/m1n1.macho` (ignored artifact)
- Copy unchanged: `investigation/artifacts/EXP-20260814-019/J313_EFI.fd` (ignored artifact)

**Interfaces:**
- Produces an immutable artifact/hash/launch record before the assisted run.

- [ ] **Step 1: Append the EXP-019 pre-run entry**

Record UTC time; root, m1n1, and Mu commits; dirty-diff hashes; the single changed variable; exact build and launch commands; physical/monitor/eight-core profile; expected checkpoint; failure criterion; evidence paths; and EXP-016 recovery hash. Status is `planned` until launch.

- [ ] **Step 2: Append the software-only CSV row**

Use the new 40-character m1n1 commit. Set `status=implemented`; leave hardware result and artifact fields populated only with verified software/build evidence and the forthcoming EXP-019 identifiers. Do not rewrite CHG-004.

- [ ] **Step 3: Build from the canonical public m1n1 commit**

```bash
cd /Users/pavel/public_windows/m1n1_windows
PATH="/opt/homebrew/opt/rustup/bin:$PATH" make clean
PATH="/opt/homebrew/opt/rustup/bin:$PATH" make -j8
```

Copy `build/m1n1.macho` and the unchanged EXP-018 Mu file into the EXP-019 artifact directory. Record `shasum -a 256` for both and verify the m1n1 binary contains the new build tag.

- [ ] **Step 4: Re-run the artifact preflight**

Use the canonical public launcher dry-run with the exact EXP-019 artifact paths and physical/monitor profile. Stop if the launcher resolves any path under `/Users/pavel/windows`, a worktree, or an unmatched Mu artifact.

### Task 6: Run one falsifiable assisted hardware experiment

**Files:**
- Append result: `investigation/EXPERIMENTS.md`
- Update result: `investigation/CHANGES.csv`
- Preserve locally: `investigation/artifacts/EXP-20260814-019/hv.log` and any watchdog snapshot.

**Interfaces:**
- Consumes the single recorded EXP-019 m1n1 artifact and unchanged Mu artifact.
- Produces a confirmed, rejected, or inconclusive verdict; never an inferred success.

- [ ] **Step 1: Launch with one USB owner**

Use `scripts/run-windows.sh --execution assisted --display physical --debug monitor` with explicit proxy/vUART endpoints and the recorded EXP-019 m1n1 and Mu paths. Do not run `hang_telemetry.py` or another `run_uefi.py`.

- [ ] **Step 2: Observe boot checkpoints**

Verify launch preflight, CPU_ENTRY exactly once for CPUs 0-7, NVMe initialization, xHCI handoff, Windows login/desktop, and external USB input. Record elapsed phase times rather than relying on visual impressions alone.

- [ ] **Step 3: Exercise the previous failure window**

Perform ordinary pointer/window interaction followed by a bounded CPU/storage workload. Observe for longer than the EXP-018 minute-scale pause window. Request a lock-free snapshot through the existing launcher's SIGINT handler only if progress stalls; do not attach a new proxy client.

- [ ] **Step 4: Evaluate the architectural invariants**

Accept only if timer IAR/EOI counters continue to advance without diagnostic IPIs, no asserted timer is left Active-only after synchronization, Active+Pending becomes Pending at EOI, and there is no global pause, watchdog, reset, or continuous EXP-017 micro-stutter.

- [ ] **Step 5: Record the result and update the CSV status**

Append observed phases, exact stop code or stall evidence, input/display/storage/CPU state, log paths and hashes, and verdict. Set the new CSV row to `validated` only on complete acceptance; otherwise set `rejected` or leave `implemented` for an inconclusive run.

- [ ] **Step 6: Commit root bookkeeping without changing the implementation result**

Commit the root submodule pointer, EXP-019 record, CSV row, integration test, design, and plan. Do not push until local verification and the hardware record agree. Push the m1n1 commit before pushing the root submodule pointer.
