# J313 AGX Pre-map Diagnostic Qualification Plan

**Goal:** In one fail-closed G2 boot, obtain fresh lifecycle and translated-
resource receipts from the MMIO qualification profile and identify the exact
boundary that returns `STATUS_DEVICE_CONFIGURATION_ERROR` before the inert
SGX mapping sequence.

**Architecture:** Keep the EXP-124 G2 firmware, AGX SSDT, NVMe-safe m1n1 and
EXP-123 recovery byte-for-byte unchanged. Stage only the CI-signed MMIO
qualification AppleAgx package from run `33024515164`. The sole code change
from rejected EXP-128 is common lifecycle diagnostics for both qualification
profiles. Power qualification remains false. If StartDevice reaches the MMIO
boundary, it may map SGX once, validate ASC as a contained subview and unmap
immediately; it still returns `STATUS_NOT_SUPPORTED`. Register access,
firmware, RTKit, interrupts, UAT, queues, commands, render, present and display
ownership remain forbidden.

## Fixed identities

- Experiment: `EXP-20260827-129`; exactly one G2 boot and no retry.
- Root branch/head: `feature/j313-gpu-acceleration` /
  `ed2a385b806b9859a7898c82ea5a307ed59c13fb`.
- Diagnostic implementation:
  `451b276ded24fd01239fdec853a2a23a14852e92`.
- WDK run: `33024515164`; default, power-qualification and
  mmio-qualification ARM64 jobs passed.
- Driver SYS SHA-256:
  `13b1ee676c45c9a5d8cc49a972d63439188c77d39f38f7eb06f98e4a18e7230b`.
- INF SHA-256:
  `b7c0714443cf45bb3125468cda6d7bc5d70d31a3547cf688077dbebe1bf0d816`.
- Catalog SHA-256:
  `6e199757e3fb79ff06d077b4a9d71e470d24c9adc67c97dd0567d02f56eca823`.
- Certificate SHA-256:
  `29aebbe3dc260e143a616305cbd72c548a97a1e5c9c8a30117e497c8e0375685`.
- Signature manifest SHA-256:
  `d86e8bbd00977373fdfbabe7aff1efead22ffbbd6b393def125f75e553f986b6`.
- Signer: `74CA42EA1DFE978EFFF4070049219DD5B0790867`.
- G2 manifest SHA-256:
  `02204a6e37a04a323eae05e24b6a35eb7a0c6327b9af98b39d714482d78a0c70`.
- Recovery manifest SHA-256:
  `143fd9aa07f9b224c316c5e23e3993991d7308fa178164beadc785e8dade03f9`.
- Candidate and recovery m1n1 SHA-256:
  `2c39f7723475e6e74fa00b1a88e413ed7e5159a0da1bac5286b6c0442b7d52a9`.
- Evidence path:
  `investigation/artifacts/EXP-20260827-129-agx-pre-map-diagnostics/`; it was
  absent at preregistration.

## Procedure

- [x] Revalidate every fixed identity and require a responsive exact recovery
      baseline with eight CPUs, Running AppleInput/NVMe/xHCI, no present
      APPL0002/package/service/module/signer, zero critical events and zero
      Event 129 through the quiet window.
- [x] Record then clear only the enumerated `Wom1*` qualification receipts so
      absence after boot is current evidence rather than stale state.
- [x] Stage only the exact package and record its generated `oemNN.inf`.
- [x] Shut down normally and launch one exact G2 candidate with display
      `both`, monitor logging and no power-qualification environment flag.
- [x] Collect DriverEntry, AddDevice, StartDevice, translated-resource and
      optional map/subview/unmap receipts with registry key timestamps.
- [x] Require no forbidden GPU action, responsive eight-core Windows, working
      input/NVMe/xHCI, zero critical events and zero Event 129.
- [x] Shut down normally, boot exact recovery, remove only the recorded
      package and signer without `/force`, then require a clean quiet window.
- [x] Publish only a sanitized verdict; retain raw device-state evidence
      locally. Close without retry.

## Falsifiable result

The experiment passes its diagnostic objective only if fresh receipts identify
one exact current boundary and status without relying on any EXP-128 value.
Reaching the inert map/subview/unmap receipts is informative but not required
for this diagnostic objective. Any identity mismatch, stale or ambiguous
receipt, forbidden GPU operation, storage reset, critical event, forced
recovery, unresponsive guest or second G2 boot rejects the experiment and
authorizes no retry.

## Result

Rejected and closed without retry. Fresh receipts prove successful
`DxgkInitialize` and AddDevice, with no StartDevice or MMIO receipt. Windows
reported Problem 31 / `0xC0000182`; the candidate also recorded two stornvme
Event 129 resets and failed the storage health gate. Exact non-force rollback
completed and the final recovery quiet window was clean. The sanitized result
is published in
`investigation/artifacts/EXP-20260827-129-agx-pre-map-diagnostics/VERDICT.md`.
