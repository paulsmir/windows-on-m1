# J313 AGX MMIO Mapping Hardware Qualification Plan

**Goal:** Prove in one fail-closed G2 boot that Windows can map the complete
SGX aperture, derive the contained ASC subview without a second mapping, and
unmap the aperture cleanly without accessing a GPU register.

**Architecture:** Keep the EXP-124 G2 firmware, AGX SSDT, NVMe-safe m1n1 and
EXP-123 recovery byte-for-byte unchanged. Stage only the CI-signed
`mmio-qualification` AppleAgx package from run `33022226675`. The power
qualification property remains false. StartDevice maps SGX once through
`DxgkCbMapMemory`, validates ASC as a checked subview, immediately unmaps it,
persists receipts, and still returns `STATUS_NOT_SUPPORTED`. Firmware, RTKit,
register access, interrupt connection, UAT activation, queues, render and
display ownership remain forbidden.

## Fixed identities

- Experiment: `EXP-20260827-128`; exactly one G2 boot and no retry.
- Root branch/head: `feature/j313-gpu-acceleration` /
  `4d40aee5cdb9f2f5d813956665fba6ff22743087`.
- Receipt implementation:
  `c573a3b49e029f423630f72876b87029f117f729`.
- WDK run: `33022226675`; default, power-qualification and
  mmio-qualification ARM64 jobs passed.
- Package manifest SHA-256:
  `cfabbee1d50d1c54765e43ffe533b9a9780f6afec0fda964b7aa10a4ec17b934`.
- Driver SYS SHA-256:
  `d1dd6783a0c30bdf639f6d01a5a6c800fe89699740ba245f634656a7734f732d`.
- INF SHA-256:
  `db5e09d26ca52311156473db0e931203a9d77dfecf5af17ec9acc39dccaab157`.
- Catalog SHA-256:
  `4032e47cfacc72eaef31d98d67233cb093865998581bcd9cbb8fd482d4d71a1f`.
- Certificate SHA-256:
  `9f70513f96edccbfef8d833d17670fa01124e17239fe44592e9eab007e4002ae`.
- Signer: `A40D8EC7010BB5D4E14792C360737F79F79D0151`.
- G2 manifest SHA-256:
  `02204a6e37a04a323eae05e24b6a35eb7a0c6327b9af98b39d714482d78a0c70`.
- Recovery manifest SHA-256:
  `143fd9aa07f9b224c316c5e23e3993991d7308fa178164beadc785e8dade03f9`.
- Candidate and recovery m1n1 SHA-256:
  `2c39f7723475e6e74fa00b1a88e413ed7e5159a0da1bac5286b6c0442b7d52a9`.
- Evidence path:
  `investigation/artifacts/EXP-20260827-128-agx-mmio-mapping/`; it was absent
  at preregistration.

## Expected receipts

- `Wom1MmioMapStatus = 0`.
- SGX start high/low `0x00000002:0x04000000`, length `0x04000000`.
- `Wom1MmioSubviewStatus = 0`, ASC offset `0x02400000`, length `0x0006c000`.
- `Wom1MmioUnmapStatus = 0`.
- Final device state: Problem 43 and fail-closed `STATUS_NOT_SUPPORTED`.

## Procedure

- [x] Revalidate every package, firmware and recovery hash; require the
      evidence path absent.
- [x] Require a responsive recovery baseline with eight CPUs, no present
      APPL0002/AppleAgx package or signer, Running AppleInput, stornvme and
      USBXHCI, zero critical events and zero Event 129 in the quiet window.
- [x] Stage only the exact package and record its new `oemNN.inf` identity.
- [x] Shut down normally and launch one exact G2 candidate with display
      `both`, monitor logging and no power-broker qualification flag.
- [x] Collect the exact map, subview and unmap receipts and require the final
      fail-closed state.
- [x] Require zero GPU register access, firmware, RTKit, interrupt connection,
      active UAT, queue, command, render, present or display ownership.
- [x] Require responsive Windows, eight CPUs, working NVMe/USB/internal input,
      zero Event 129 and zero critical events.
- [x] Shut down normally, boot exact recovery, remove only the recorded
      package and exact signer without `/force`, and perform the cleanup boot.
- [x] Require clean recovery, hash the bounded evidence, record one verdict
      and close the experiment without retry.

## Falsifiable result

The hypothesis passes only if all three zero-status receipts and exact ranges
are present, Windows remains responsive, storage/input/xHCI remain healthy,
there are no Event 129 or critical events, and normal shutdown plus exact
rollback succeed. Any map/subview/unmap failure, missing or mismatched receipt,
register read or write, firmware start, new PnP problem outside the expected
fail-closed APPL0002 state, storage reset, forced recovery, identity mismatch
or second G2 boot rejects the experiment and authorizes no retry.

## Result

Rejected without retry. The exact package bound as `oem17.inf`, but the sole
G2 boot stopped with PnP Problem 31 and ProblemStatus `0xC0000182` before any
fresh MMIO receipt was written. Windows otherwise remained responsive with
eight CPUs, working input/NVMe/xHCI, zero critical events and zero Event 129.
No forbidden GPU action occurred.

Recovery removed only the recorded package and signer without `/force`. Its
first boot recorded six Event 129 resets and failed the health gate; the
required cleanup boot ended clean with zero Event 129. EXP-128 is closed and
must not be retried. The next step is offline diagnostic parity between power
and MMIO qualification profiles, not another hardware boot.
