# J313 AGX Inert MMIO Contract Qualification Plan

**Goal:** Prove in one fail-closed G2 boot that the corrected full WDDM
callback contract maps the exact SGX aperture, validates ASC as a contained
subview and immediately unmaps it without dereferencing either mapping.

**Architecture:** Keep the passed EXP-130 callback contract, EXP-124 G2 Mu,
NVMe-safe m1n1 and EXP-123 recovery unchanged. Replace only the compile-time
profile with the CI-signed `mmio-qualification` artifact from the same WDK run.
Power qualification remains false. The sole hardware action is map,
bounds-check and unmap; register access, firmware, RTKit, interrupts, UAT,
queues, commands, render, present and display ownership remain forbidden.

## Fixed identities

- Experiment: `EXP-20260827-131`; exactly one G2 execution and no retry.
- Root branch/head at preregistration:
  `feature/j313-gpu-acceleration` /
  `4cc8f674dac0a3936c189e8fff2ee89ec7c71fbe`.
- Driver source: `6609ab08a046edbe54b4795fda87700ac04e7412`.
- WDK run: `33026918148`; all four ARM64 jobs passed.
- SYS SHA-256:
  `5d3b2b8c9f20ac98d302259da593e41b41ecf01a9325f3c18052abb0c24581cb`.
- INF SHA-256:
  `9073d731f645575f58f792712b37f33d08b7eb7e06bf597a14da2d77e1fb819a`.
- Catalog SHA-256:
  `5b38ee37b3e0059de78ee8b0868a1a4fa2eef6522140bb1f40eb235c5a3be89b`.
- Certificate SHA-256:
  `fd44a56f4f271a8e5b7bb7323e8f1b1325ecdbfcf0a5a3cb8d3c616f3e89136f`.
- Signature manifest SHA-256:
  `c6f8be582c362d1898d5dee979e8206f2eeb472e773f08a6b5c421db826a7a31`.
- Signer: `EE24256D1F278177D0DD882E557BC4FF9FE075C4`.
- G2 manifest SHA-256:
  `02204a6e37a04a323eae05e24b6a35eb7a0c6327b9af98b39d714482d78a0c70`.
- Recovery manifest SHA-256:
  `143fd9aa07f9b224c316c5e23e3993991d7308fa178164beadc785e8dade03f9`.
- Candidate and recovery m1n1 SHA-256:
  `2c39f7723475e6e74fa00b1a88e413ed7e5159a0da1bac5286b6c0442b7d52a9`.
- Evidence path:
  `investigation/artifacts/EXP-20260827-131-agx-mmio-contract/`; absent at
  preregistration.

## Procedure

- [x] Require the clean final EXP-130 recovery state: eight CPUs, Running
  AppleInput/NVMe/xHCI, no APPL0002/package/service/module/signer, zero
  critical events and zero Event 129.
- [x] Revalidate package hashes and signer; import and stage only this package;
  record its exact `oemNN.inf`; prove APPL0002 absent; shut down normally.
- [x] Launch exactly one EXP-124 G2 candidate with `display=both`,
  `debug=monitor`, no power-broker flag and the pinned Mu/m1n1 pair.
- [x] Collect fresh lifecycle, translated-resource, map, subview and unmap
  receipts. Require StartDevice stage 7 / `0xC00000BB` after successful zero-
  status map, subview and unmap receipts.
- [x] Audit logs and package sources for zero pointer dereference, register
  access, firmware, RTKit, interrupt, UAT, queue, command, render, present or
  display ownership. Require eight CPUs, healthy input/storage/xHCI, zero
  critical events and zero Event 129.
- [x] Shut down normally; boot exact EXP-123 recovery; remove only the recorded
  package and signer without `/force`; cleanup reboot and prove final zero
  state and quiet health window.
- [x] Publish only a sanitized verdict and close without retry.

## Falsifiable result

Passing requires all three fresh map/subview/unmap status receipts to be zero,
the exact generated SGX/ASC ranges, the deliberate stage-7 fail-closed status
and every platform-health gate. Missing or stale receipts, Problem 31 before
StartDevice, pointer access, any later GPU ownership action, identity drift,
Event 129, critical event, reset, forced deletion or incomplete rollback
rejects the experiment and authorizes no retry.

## Observed result

Passed and closed without retry. Fresh receipts reported the exact SGX aperture
at `0x204000000` with length `0x4000000`, the ASC subview at offset
`0x2400000` with length `0x6c000`, and zero status for map, subview validation
and unmap. StartDevice then deliberately failed closed at stage 7 with
`0xC00000BB`.

The candidate retained eight logical processors and Running AppleInput,
stornvme and USBXHCI with zero Event 129 and zero critical System events. The
source and hypervisor audit found no pointer dereference, GPU-register access,
power transaction, firmware, RTKit, interrupt, UAT, queue, command, render,
present or display ownership. Normal shutdown and exact EXP-123 recovery
removed only the recorded package and signer without `/force`; the cleanup
boot proved the original zero state and quiet health window.
