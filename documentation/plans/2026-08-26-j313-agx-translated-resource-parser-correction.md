# J313 AGX Translated Resource Parser Correction Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task.

**Goal:** Correct AppleAgx translated-resource validation using the exact
EXP-126 representation while preserving the fail-closed GPU boundary.

**Architecture:** Continue consuming only
`DXGK_DEVICE_INFO.TranslatedResourceList`. Require one list with exactly two
known MMIO ranges, two system-private descriptors and nine exclusive,
level-sensitive interrupts. Treat interrupt vectors as translated system
vectors: require them to be nonzero and unique with nonzero affinity, but never
compare them with ACPI firmware GSIs. Never inspect the system-private payload.

**Tech Stack:** ARM64 WDDM display miniport, WDK, Python contract tests,
Microsoft WDM translated-resource contracts.

**Spec:** EXP-20260826-126 in `investigation/EXPERIMENTS.md`; Microsoft Learn
`CM_PARTIAL_RESOURCE_DESCRIPTOR`, `Raw and Translated Resources`, and
`IRP_MN_START_DEVICE`.

## Task 1: Lock the measured representation in a failing test

**Files:**
- Modify: `tests/test_apple_agx_windows_package.py`

- [x] Require explicit `CmResourceTypeDevicePrivate` handling.
- [x] Require translated-vector uniqueness and nonzero affinity.
- [x] Forbid the old generated `GuestIntId` comparison and private-payload use.
- [x] Run the focused test and observe the expected failure on the old parser.

## Task 2: Correct only translated-resource validation

**Files:**
- Modify: `drivers/apple-agx/windows/src/resources.c`

- [x] Require exactly 13 descriptors in one full list.
- [x] Retain exact base, size and exclusive ownership for both MMIO ranges.
- [x] Count exactly two exclusive system-private descriptors without reading
      `u.DevicePrivate` or its flags.
- [x] Count exactly nine exclusive, level-sensitive translated interrupts.
- [x] Reject zero, duplicate or excess translated vectors and zero affinity.
- [x] Keep MMIO mapping, interrupt connection, firmware, RTKit, UAT, queues,
      render, present and display ownership unchanged and unavailable.

## Task 3: Verify and hand off to a separate hardware experiment

- [x] Focused AppleAgx package tests pass 22/22.
- [x] Canonical full suite passes 663/663.
- [x] `git diff --check` passes.
- [ ] ARM64 WDK default and power-qualification builds pass in CI.
- [ ] A separately preregistered one-shot experiment verifies StartDevice
      advances beyond resource validation and still fails closed.

## Falsifiable Result

The software correction is acceptable only if all tests pass and the diff
contains no GPU access path. Hardware success later means stage 3 records
success and execution advances to the existing state/broker/fail-closed
boundaries. Any missing descriptor, extra descriptor, duplicate vector, zero
affinity, package identity mismatch, storage reset, critical event or forbidden
GPU action rejects the hardware run.
