# EXP575 Per-class Retained VA Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Extend the retained-root broker with versioned, bounded A040/A071 arenas and use them for the existing production render-shared graph before one EXP575 retirement discriminator.

**Architecture:** ABI v4 exposes a read-only current-epoch arena query.  EL2 validates and reserves empty windows in its owned root slot; Windows applies those ranges to the existing render-shared owner and mapping inventory before the ordinary broker maps the production graph.  Existing relocation, queue, firmware, submission, and completion implementations remain unchanged.

**Tech Stack:** freestanding C, m1n1 EL2, Windows WDDM KMD, WDK 10.0.26100.0, host C tests, Python contract tests, FRYZZING ARM64 build/sign pipeline.

**Spec:** `docs/superpowers/specs/2026-09-07-retained-root-per-class-arenas.md`

## Global Constraints

- Preserve retained-root physical identity and firmware-private prefix slots 0/1.
- Windows receives no raw page-table or firmware-private page access.
- Only Windows-owned physical pages may be mapped; only exact owned handles may be unmapped.
- Existing A000, A020, context-63, HVC, scheduler, display, IRQ, backend command generation, TA/3D and completion behavior do not change.
- One causal hardware variable: restore A040/A071 allocator-class VA identity.
- EXP575 must be preregistered before launch and exactly cleaned up afterward unless it becomes the final known-good package.

---

### Task 1: Define and enforce retained-root ABI v4 arenas

**Files:**
- Modify: `drivers/apple-agx/shared/include/apple_agx_retained_root_abi.h`
- Modify: `m1n1_windows/src/hv_agx_retained_root.h`
- Modify: `m1n1_windows/src/hv_agx_retained_root.c`
- Modify: `m1n1_windows/src/hv_agx_retained_platform.c`
- Modify: `m1n1_windows/src/hv_agx_retained_mmio.c`
- Test: `tests/agx_retained_root_test.c`
- Test: `tests/agx_retained_mmio_test.c`

**Interfaces:**
- Produces: `AGX_RR_QUERY_ARENA`, `AGX_RR_ARENA_SHARED`, `AGX_RR_ARENA_TIMESTAMP`, and `hv_agx_retained_query_arena(core, epoch, class, descriptor)`.
- Produces: response fields `ArenaVersion`, `ArenaClass`, `ArenaVa`, `ArenaBytes` authenticated by the existing receipt/epoch/root response.

- [ ] **Step 1: Add failing host assertions**

Add assertions that a current-epoch query returns exact aligned shared and
timestamp descriptors; invalid classes/epochs and queries before activation
fail; mapping either class before its query fails; mapping after query succeeds;
outside-edge mappings fail; exact query/unmap succeeds; a fresh lifetime starts
with no stale queried-class state; root identity and prefix never change.

- [ ] **Step 2: Run focused tests and confirm RED**

Run the repository's existing retained-root and retained-MMIO host test commands
discovered from the test harness.  Expected: compile failure on the new command,
types, and query function.

- [ ] **Step 3: Implement the minimal broker contract**

Bump the wire ABI to 4, add the read-only query command and response fields, add
two fixed descriptors (`0xffffffa041000000/0x01000000` and
`0xffffffa071100000/0x01000000`), and record a per-lifetime queried mask in EL2
state.  Query must require active current epoch, zero unused request fields,
exact class, page-aligned nonempty bounded range, and no existing owned leaf in
the proposed window.  MAP must accept a new-class VA only after its successful
query; all other ownership/backing/sync/rollback checks stay common.

- [ ] **Step 4: Run focused and full retained-root tests GREEN**

Run the retained-root, retained-MMIO, firmware-IO, backing and runtime-handoff
tests.  Expected: all pass with ABI 4, no ASan error, no leaked page and no
changed private prefix.

- [ ] **Step 5: Commit the broker ABI change**

Commit only the ABI, m1n1 broker and focused tests with message
`extend retained root with class arenas`, then append its 40-character hash to
`investigation/CHANGES.csv` as `implemented` with host-test evidence.

### Task 2: Relayout the existing production graph in-place

**Files:**
- Modify: `drivers/apple-agx/shared/include/apple_agx_render_shared_memory.h`
- Modify: `drivers/apple-agx/shared/src/apple_agx_render_shared_memory.c`
- Modify: `drivers/apple-agx/shared/include/apple_agx_initdata_memory.h`
- Modify: `drivers/apple-agx/shared/src/apple_agx_initdata_memory.c`
- Test: `drivers/apple-agx/shared/tests/apple_agx_render_shared_memory_test.c`
- Test: `drivers/apple-agx/shared/tests/apple_agx_initdata_memory_test.c`
- Test: `drivers/apple-agx/shared/tests/apple_agx_context0_broker_test.c`

**Interfaces:**
- Consumes: validated shared/timestamp arena descriptors.
- Produces: `AppleAgxInitdataMemoryApplyRenderArenas(Graph, SharedVa, SharedBytes, TimestampVa, TimestampBytes)`.

- [ ] **Step 1: Add deterministic relayout assertions**

Assert that objects 0--19 keep their exact prior mapping bases, objects 20--31
move only inside A040, objects 32--35 move only inside A071, low-14 offsets and
CPU/device backing stay unchanged, every mapping-inventory record follows its
object, relocations and queue pointers use the new addresses, and malformed,
overlapping, undersized, repeated, or post-map relayout is rejected without a
partial mutation.

- [ ] **Step 2: Run focused tests and confirm RED**

Run the render-shared, initdata-memory and context0-broker focused tests.
Expected: compile failure because the relayout interface does not exist.

- [ ] **Step 3: Implement atomic in-place relayout**

Validate the whole proposed layout into local arrays first.  Classify only
original A040 and A071 objects from the generated layout, assign aligned pages
sequentially inside the provided arena, then atomically update owner VAs and the
matching `UatMappings[].VirtualAddress` entries by `MappingObjects[]` identity.
Do not allocate/free memory or modify A000/A020 objects.

- [ ] **Step 4: Run focused and complete AppleAgx host suites GREEN**

Run the three focused binaries followed by the current complete AppleAgx test
suite.  Expected: all pass and the established count is not lower than 361.

- [ ] **Step 5: Commit the production graph integration**

Commit only the shared-memory/initdata implementation and tests with message
`place render state in broker class arenas`, then append the exact hash to
`investigation/CHANGES.csv` as `implemented`.

### Task 3: Wire arena query into the production activation window

**Files:**
- Modify: `drivers/apple-agx/render-admission/src/backend_platform_windows.c`
- Modify: `drivers/apple-agx/render-admission/include/apple_agx_admission.h` only if a durable scalar receipt needs storage
- Test: `tests/test_apple_agx_render_work_queue.py`
- Test: `tests/test_apple_agx_windows_package.py`

**Interfaces:**
- Consumes: ABI-v4 query and `AppleAgxInitdataMemoryApplyRenderArenas`.
- Produces: activation order `ACTIVATE -> QUERY shared/timestamp -> relayout -> rebind 159 relocations -> production MAP`.

- [ ] **Step 1: Add source-contract assertions**

Assert the exact activation ordering and fail-closed validation, and assert that
the ordinary `AppleAgxContext0BrokerMap` remains the only mapping path.

- [ ] **Step 2: Run contract tests and confirm RED**

Run the focused Python source-contract tests.  Expected: failure because arena
query and relayout are not wired between activation and broker map.

- [ ] **Step 3: Implement production wiring**

Add a request-capable retained command helper, query both arenas after successful
ACTIVATE, validate every response scalar, apply the graph relayout, rebind the
existing `QueueObjects`, reapply the existing relocation table, then call the
unchanged production broker map.  Any failure returns before the first MAP and
uses the existing reverse cleanup.

- [ ] **Step 4: Run relevant and full regression gates GREEN**

Run render work-queue, package, retained-root and complete AppleAgx suites.
Expected: all pass; no new capability or unrelated source diff.

- [ ] **Step 5: Commit Windows activation wiring**

Commit the wiring and tests with message `adopt retained class arenas before map`,
then append its exact hash to `investigation/CHANGES.csv` as `implemented`.

### Task 4: Freeze, build and hardware-test EXP575

**Files:**
- Create: `.local/experiments/EXP575-retained-class-arenas/manifest.json`
- Create: `.local/experiments/EXP575-retained-class-arenas/build.ps1`
- Create: `.local/experiments/EXP575-retained-class-arenas/launch.sh`
- Modify: `investigation/EXPERIMENTS.md`
- Modify: `investigation/GPU_CURRENT_STATE.md`
- Modify: `investigation/CHANGES.csv`

**Interfaces:**
- Consumes: committed source and current proven FRYZZING/ordinary/full-owner workflow.
- Produces: one exact EXP575 hardware verdict at the RetireStamp boundary.

- [ ] **Step 1: Preregister EXP575**

Append `WHY THIS HYPOTHESIS`, Windows contract, AGX/Asahi contract, translation,
remaining unknown, exact commits/dirty diff hashes, build and launch commands,
artifact/recovery hashes, pass/fail criteria, and evidence paths.  Record that
EXP573/574 are not repeated.

- [ ] **Step 2: Run pinned build/sign/hash gates**

Transfer the frozen source to `pauls@FRYZZING`, build KMD/UMD with WDK
10.0.26100.0 and current scripts, require tests, code analysis, Universal API,
Inf2Cat and signing success, retrieve the ZIP/manifest, and verify every SHA-256
locally before staging.

- [ ] **Step 3: Verify clean ordinary preflight and run once**

Require SSH, 8 CPUs, APPL0002 Code 28, zero AppleAgx package/service/module and
healthy input/xHCI/NVMe.  Stage only the hash-matched EXP575 package, launch the
current full-owner/synthetic-889 profile once, invoke the exact established
Windows producer once, and collect host log plus all crash-durable queue,
TA-progress, retirement, temporal, fault and health receipts.

- [ ] **Step 4: Classify and recover**

Compare exact EXP575 retirement scalars with EXP574.  Confirm only if a new
shared stamp/event/done/completion receipt passes the prior boundary; otherwise
reject or mark launch-inconclusive precisely.  Preserve evidence hashes, remove
only the exact package/state, and restore/verify ordinary 377/392 Code 28.

- [ ] **Step 5: Update durable state and continue causally**

Append the actual result to `EXPERIMENTS.md`, update compact current truth and
the related `CHANGES.csv` rows.  If the boundary moves, continue from the first
new primitive; if unchanged, reject allocator-class identity and inspect the
next source-derived difference without repeating EXP575.

