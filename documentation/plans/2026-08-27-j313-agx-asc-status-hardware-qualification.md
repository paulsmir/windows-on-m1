# J313 AGX Read-Only ASC Status Hardware Qualification

**Goal:** Prove in one fail-closed G2 boot that the Windows driver can map the
exact SGX aperture, validate its ASC subview, read exactly one 32-bit ASC CPU
status register at offset `0x48`, persist a bounded receipt and unmap without
changing GPU state.

**Architecture:** Reuse the exact EXP-131 mapping boundary and the typed ASC
transport. Replace only the qualification driver with the CI-signed
firmware-qualification package. Keep the EXP-124 G2 firmware and the EXP-123
recovery pair byte-for-byte unchanged. The candidate advertises no render node
or display source and deliberately returns `STATUS_NOT_SUPPORTED` at
StartDevice stage 7.

## Fixed identities

- Experiment: `EXP-20260827-134`; exactly one cold G2 execution and no retry.
- Root branch/source commit:
  `feature/j313-gpu-acceleration` /
  `ac1eae5ffc703008560a8838222edeea17e09ec0`.
- Firmware transport source SHA-256:
  `9dac6399e1dbfecb5b9b2d0b041d7aea0f238029bb4bf6eb6cf7e44cba746dc4`.
- WDK workflow: `33056857717`; all five ARM64 profiles passed.
- INF SHA-256:
  `a77de6a886246fd905e601399dbdcfaead0f7df4c18090b7faaa89ec00f5f979`.
- SYS SHA-256:
  `b18875a4206d465150a04195d79097e88f0b200c5fb4ea4fd2edf1eb5785388c`.
- Catalog SHA-256:
  `ae2b4aa948dd94d20c9a8515c63ac957203f0ad910f6bbe29976fd47184b150c`.
- Certificate SHA-256:
  `99f12a90e8289c8c9dfc443c9e8df01530e2f602fb1e7073a2204e9b9591ab1d`.
- Signature manifest SHA-256:
  `867ac353441dcdc6e584101ec5e3999d95790d9fc58128d6b864ce7e85946181`.
- Signer thumbprint: `58267BEC4B06A7A50B6517A8D339F8F2295DB774`.
- G2 manifest SHA-256:
  `02204a6e37a04a323eae05e24b6a35eb7a0c6327b9af98b39d714482d78a0c70`.
- G2 firmware SHA-256:
  `70b216c01f3d7acd77b2c24d0a3dc4fa0cccefec631031a413a301d271f6c064`.
- G2 SSDT SHA-256:
  `a6f8f4911030c23b61a2ed8c3a300d1ca438af74accc41e624918930ef55f65b`.
- Recovery manifest SHA-256:
  `143fd9aa07f9b224c316c5e23e3993991d7308fa178164beadc785e8dade03f9`.
- Recovery firmware SHA-256:
  `4c5e068f664d8ccc94823880de4226e3f7842e08841bc10fea19cbe9e05a519b`.
- Candidate and recovery m1n1 SHA-256:
  `2c39f7723475e6e74fa00b1a88e413ed7e5159a0da1bac5286b6c0442b7d52a9`.
- Evidence path:
  `investigation/artifacts/EXP-20260827-134-agx-asc-status/`; absent at
  preregistration.

## Mandatory preflight

- Exact clean EXP-123 recovery is running with `display=both`.
- Windows reports eight logical processors and Running AppleInput, stornvme
  and USBXHCI.
- APPL0002 has no bound package, service or loaded module; the pinned signer is
  absent.
- The preflight window contains zero stornvme Event 129 and zero critical
  System events.
- Every package, signer, G2 and recovery hash above is revalidated before any
  mutation. Identity drift rejects the experiment.

## Single authorized execution

1. Import only the pinned certificate, stage only the pinned package and record
   its exact `oemNN.inf`; clear all stale `Wom1*` receipts.
2. Shut down normally and cold-launch exactly the pinned G2 pair with
   `display=both` and `debug=monitor`.
3. Restart only `ACPI\APPL0002\0` once with the bounded lifecycle runner.
4. The driver may map SGX, bounds-check the ASC subview, perform exactly one
   32-bit read from ASC offset `0x48`, persist
   `Wom1AscCpuStatusReadStatus` and, only on success, `Wom1AscCpuStatus`, then
   immediately unmap.
5. Require deliberate StartDevice stage 7 / `0xC00000BB`, zero map, subview,
   read and unmap status receipts, eight CPUs, healthy platform services, zero
   Event 129 and zero critical events.
6. Shut down normally, boot the exact EXP-123 recovery pair, remove only the
   recorded `oemNN.inf`, service and signer without `/force`, reboot once and
   prove the original clean health state.

## Forbidden actions

No register write, CPU RUN transition, mailbox traffic, power transaction,
firmware start, RTKit exchange, interrupt connection, UAT publication, queue or
command submission, render, present or display ownership is permitted. No
retry, forced device/package deletion, broad certificate removal, stale
receipt acceptance or mixed candidate/recovery identity is permitted.

## Falsifiable result

Passing requires one fresh successful 32-bit read receipt at offset `0x48`, all
mapping and cleanup receipts, the deliberate stage-7 failure and every health
and rollback gate. A missing or duplicate read, nonzero status, unexpected
receipt, write, reset, Event 129, critical event, service regression, identity
drift or incomplete rollback rejects the experiment and authorizes no retry.

## Observed result

Rejected and closed without retry. Preflight passed with eight CPUs, healthy
AppleInput/NVMe/xHCI, no installed APPL0002 package or signer and zero Event
129. The one authorized cold candidate reached the exact 32-bit load from ASC
CPU status (`SGX + 0x2400000 + 0x48`, physical `0x206400048`) and immediately
raised a physical external abort. The guest VA and stage-2 SGX mapping were
valid; the abort therefore proves that mapping alone does not make the
power-gated ASC register readable. The qualification profile performed no
power transaction, so the GPU domain remained off.

The guest was reset, the exact EXP-123 recovery pair booted, and only the
recorded package and signer were removed without force. A later recovery boot
showed Event 129 reset activity, so no subsequent GPU candidate is authorized
until a fresh recovery preflight proves a zero-Event-129 window. The sanitized
verdict is
`investigation/artifacts/EXP-20260827-134-agx-asc-status/VERDICT.md`.
