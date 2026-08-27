# J313 AGX G2 Windows UAT Memory Owner Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Connect the existing Windows DMA-visible allocator to the pure UAT table builder through a disconnected, deterministic context-zero ownership layer.

**Architecture:** A freestanding owner adapts `APPLE_AGX_MEMORY_IO` into `APPLE_AGX_UAT_ALLOCATOR`, retains every underlying allocation handle, zeroes each 16-KiB table page before publication, and releases only matching owned pages. The owner is compiled into the ARM64 WDK package but remains unreachable from `AppleAgxDdiStartDevice`, so this increment cannot access GPU hardware or change the stable driver path.

**Tech Stack:** freestanding C11, sanitizer-backed host tests, Python `unittest`, ARM64 WDK project metadata.

**Spec:** `documentation/design/2026-08-27-j313-agx-g2-windows-uat-initdata.md`

## Global Constraints

- UAT input addresses are 39-bit canonical halves; output addresses are 40-bit; pages are exactly `0x4000` bytes.
- Context zero is firmware-private and this increment creates no render context.
- Allocation inventory is exact, teardown is reverse-order and idempotent, and a failed release preserves ownership for retry.
- No adapter callback invokes this layer; no MMIO, mailbox, interrupt, firmware-start, USB, Python or EL2 command path is added.
- The stable Windows package remains behaviorally unchanged.

---

### Task 1: Freestanding UAT memory owner

**Files:**
- Create: `drivers/apple-agx/shared/include/apple_agx_uat_memory.h`
- Create: `drivers/apple-agx/shared/src/apple_agx_uat_memory.c`
- Create: `drivers/apple-agx/shared/tests/apple_agx_uat_memory_test.c`
- Create: `tests/test_apple_agx_uat_memory.py`

**Interfaces:**
- Consumes: `APPLE_AGX_MEMORY_IO`, `APPLE_AGX_MEMORY_OBJECT`, `APPLE_AGX_UAT_ALLOCATOR`, and `APPLE_AGX_UAT_PAGE`.
- Produces: `AppleAgxUatMemoryOwnerInitialize`, `AppleAgxUatMemoryOwnerGetAllocator`, and `AppleAgxUatMemoryOwnerDestroy`.

- [x] Write a sanitizer-backed failing test that proves two root pages are zeroed, owned and exposed with 16-KiB-aligned device addresses.
- [x] Run the focused test and verify it fails because the owner interface is absent.
- [x] Add failing cases for capacity exhaustion, allocation failure, exact-match release, reverse teardown, idempotent destroy and failed-release retry.
- [x] Implement the smallest freestanding owner that passes those cases without hardware dependencies.
- [x] Run the focused test under AddressSanitizer and UndefinedBehaviorSanitizer.

### Task 2: Disconnected WDK compile boundary

**Files:**
- Modify: `drivers/apple-agx/windows/AppleAgx.vcxproj`
- Modify: `tests/test_apple_agx_windows_package.py`

**Interfaces:**
- Consumes: the owner implementation from Task 1.
- Produces: an ARM64-project compile contract with no `StartDevice` call site.

- [x] Add a failing package test requiring the new header/source in the project and forbidding owner calls from `adapter.c`.
- [x] Run the package test and verify the expected failure.
- [x] Add only the source/header project entries; do not modify adapter state or callbacks.
- [x] Run the focused package tests and verify they pass.

### Task 3: Repository verification and durable record

**Files:**
- Modify: `investigation/CHANGES.csv`
- Modify: this plan after verification.

**Interfaces:**
- Consumes: verified code commit hash.
- Produces: a reproducible ledger entry naming the exact tests and safety boundary.

- [x] Run the focused UAT owner and Windows package tests.
- [x] Run `./proxyenv/bin/python -m unittest discover -s tests` and require a completely green suite.
- [x] Commit the implementation without unrelated submodule changes.
- [x] Add a `CHANGES.csv` row referencing the exact implementation commit and mark completed plan steps.
- [ ] Commit and push the documentation record to `origin/feature/j313-gpu-acceleration`.
