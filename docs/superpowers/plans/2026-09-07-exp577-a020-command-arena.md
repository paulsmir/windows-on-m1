# EXP577 A020 Command Arena Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Test whether native A020 `cmdbuf` allocator identity for WorkCommand3D/TA is the missing post-Finalize queue-retirement contract.

**Architecture:** Retained-root ABI v4 gains one bounded read-only command arena at `0xffffffa021000000/0x01000000`. Production restores the EXP574 all-A000 layout for every other render-shared object and moves only objects 18/19 to `OriginalGpuVa + 0x01000000` before the unchanged relocation and broker-map paths.

**Tech Stack:** freestanding C, m1n1 EL2, WDDM KMD, WDK 10.0.26100.0, sanitizer host tests, FRYZZING ARM64 signing pipeline.

**Spec:** `docs/superpowers/specs/2026-09-07-retained-root-per-class-arenas.md` plus the user-approved A020 command-arena extension.

## Global Constraints

- No original native mapping or firmware-private page may be exposed, mutated, or freed.
- A020 MAP is impossible before a current-epoch read-only arena query.
- Only objects 18/19 change GPU VA relative to EXP574; objects 0--17 and 20--35 retain EXP574 addresses.
- Context63, HVC, firmware, queue logic, scheduler, IRQ, completion, scanout, Mu and ownership remain unchanged.

### Task 1: Broker command arena

- [ ] Add RED host assertions for query state/version/class/bounds, map-before-query denial, exact map/query/unmap, private/out-of-range denial and fresh-lifetime reset.
- [ ] Add `AGX_RR_ARENA_COMMAND` and exact A021 descriptor to ABI/m1n1; reuse all existing backing, handle, rollback, barrier and TLB checks.
- [ ] Run retained-root/MMIO/firmware-IO/backing tests GREEN and commit the m1n1 plus superproject pointer change.

### Task 2: Production WorkCommand placement

- [ ] Add RED assertions that effective VAs for objects18/19 equal original plus `0x01000000`, every other object VA is unchanged, mapping inventory follows exact object identity and repeated/malformed application is atomic fail-closed.
- [ ] Implement `AppleAgxInitdataMemoryApplyCommandArena` over the existing render-shared owner; allocate no second graph and reapply the existing relocation table.
- [ ] Wire `ACTIVATE -> QUERY command -> command relayout -> relocation rebind -> Context0BrokerMap`; remove A040/A071 production adoption without removing their validated broker API.
- [ ] Run focused and complete AppleAgx/retained regressions GREEN; commit and update CHANGES.csv.

### Task 3: EXP577 hardware discriminator

- [ ] Preregister exact hypothesis, source/platform revisions, hashes, expected receipts and recovery.
- [ ] Build/sign/version/hash with pinned FRYZZING WDK; keep full-owner m1n1/Mu exact except for the committed A020 broker extension.
- [ ] Verify ordinary Code28 baseline, stage exact package, natural full-owner bind, run one producer and collect durable evidence.
- [ ] PASS requires EXP574 physical TA plus movement beyond FinalizeTA/End into shared stamp/event/done/completion; otherwise classify exactly, clean the package and restore ordinary baseline.
