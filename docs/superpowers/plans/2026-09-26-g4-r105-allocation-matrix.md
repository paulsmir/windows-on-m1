# G4 R105 Allocation Matrix Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Resolve the ClassId1–3 allocation refusal with one qualification package and one hardware boot.

**Architecture:** A versioned harness-only private-data extension selects the KMD allocation-output combination. The KMD records the exact output in a device registry receipt; ordinary requests and production builds keep their existing path. Exact cleanup removes the experiment devnode before uninstalling its verified package.

**Tech Stack:** WDK26100 C, PowerShell, D3DKMT, Python host tests.

**Spec:** REVIEW R105 in `/Users/pavel/public_windows/.local/tandem/REVIEW.md`; `investigation/GPU_CURRENT_STATE.md` EXP819–821.

## Source and contract

- Inspected: `src/allocation_windows.c`, `src/render_win32_transport.c`, `include/render_win32_transport.h`, `one-shot/agx_r103_allocate_probe.c`, EXP821 `cleanup-current.ps1` and `hidden-cleanup.ps1`, WDK26100 field layout recorded in current state/R105.
- Hardware: class0 64 KiB succeeds; classes1–3 fail `0xC000000D` after KMD Create/Open status0. EXP816–821 exclude four individual output hypotheses.
- Ownership: KMD owns output fields and diagnostic echo; D3DKMT harness owns per-call selection; PnP cleanup owns exact package removal. No changes to m1n1, Mu, DMA, IRQ, or GPU runtime.
- Checkpoint: first GPUVA `STATUS_SUCCESS` row; if none, stop field trials and write KD plan. Recovery: hash-gated exact devnode removal, exact package uninstall, ordinary Code28 profile; GPU-hidden only if guest unreachable.

## Tasks

- [ ] Add RED host test for cleanup ordering and identity guard; implement script; run GREEN.
- [ ] Add RED actual KMD allocation-body tests for disabled default, each override, and class0 clone; implement versioned request, qualification-only application, and echo; run GREEN.
- [ ] Extend R103 harness to print NTSTATUS and matching KMD echo for class0, Cartesian matrix, and clone; compile it.
- [ ] Run targeted tests, full host suite once, R83 incremental build, and record exact hashes before hardware.
- [ ] Check SSH/proxy, preregister EXP822 with artifact/recovery hashes and `WHY THIS HYPOTHESIS`, then perform one boot and collect evidence.
- [ ] Roll back exact package; record verdict, compact state, implementation commit, and CHANGES.csv row.

## Review focus

- Production request and malformed override must retain ordinary validation and output.
- Echo must identify a single call and report final fields, including union bits.
- Class0 copy must preserve every output field except allocation handle and class ownership.
- Cleanup must fail before mutation if package identity or arm state is uncertain.
- A GPUVA PASS row must be confirmed against its echo before changing DWM path.
