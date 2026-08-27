# J313 AGX Read-Only ASC Status Qualification Plan

**Goal:** Extend the passed inert SGX mapping boundary by reading exactly the
32-bit ASC CPU status register at offset `0x48`, persisting a bounded receipt,
and releasing the mapping immediately.

**Architecture:** Reuse the exact EXP-131 `DxgkCbMapMemory` mapping and checked
ASC subview.  The false-by-default firmware-qualification profile initializes
the typed ASC transport, calls only `AppleAgxAscReadCpuStatus`, records its
status and value, and unmaps.  The normal, lifecycle, power and MMIO profiles
remain unchanged.  The adapter still returns `STATUS_NOT_SUPPORTED` and
advertises no render node or display source.

## Implementation gate

- [x] Add a failing package test before connecting the read.
- [x] Compile the mapping helper for either inert-MMIO or firmware
  qualification without combining the profiles.
- [x] Map the exact SGX aperture and use only the bounds-checked ASC subview.
- [x] Read only `J313_AGX_G2_ASC_CPU_STATUS_OFFSET` through the typed 32-bit
  callback.
- [x] Persist only `Wom1AscCpuStatusReadStatus` and, on success,
  `Wom1AscCpuStatus`.
- [x] Release the mapping on both success and failure and fail closed.
- [x] Keep CPU RUN writes, mailbox traffic, power changes, firmware startup,
  interrupts, UAT, queues, render, present and display ownership unreachable.
- [x] Pass the complete host test suite.
- [ ] Pass all ARM64 WDK build and signing profiles for the exact source head.

## Hardware gate

Hardware execution is not authorized by this implementation plan.  Before the
first register dereference, create a separate experiment record that pins the
exact source commit, WDK run, signed package and certificate hashes, G2 and
recovery manifests, one `display=both` cold execution, expected receipts,
platform-health gates and non-force recovery procedure.

One cold attempt may perform only:

1. map SGX;
2. validate the ASC subview;
3. read ASC CPU status once;
4. persist the sanitized receipt;
5. unmap SGX;
6. fail closed at StartDevice stage 7.

Any write, missing/stale receipt, mapping leak, Event 129, critical event,
platform-service regression, identity drift, forced cleanup or incomplete
rollback rejects the experiment and authorizes no retry.
