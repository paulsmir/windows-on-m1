# GPU current state

Updated: 2026-08-27T23:43:22Z

## Stable recovery

- Immutable EXP-123 Mu:
  `4c5e068f664d8ccc94823880de4226e3f7842e08841bc10fea19cbe9e05a519b`.
- Immutable EXP-123 m1n1:
  `3b81d82176b9853228b39eb3bb56ceff018cd0542248e872dd1bc1304c32b82e`.
- Recovery preserves eight CPUs, native input, internal display, NVMe, xHCI,
  and SSH. The current recovery boot had one boot-window Event 129, no WHEA,
  and no critical event; do not call that window clean.

## Repository identity

- Root branch: `feature/j313-gpu-acceleration`.
- Root state commit: `d28bf2c` (source package commit `6ac19e9458b5d7786e2685fe7202f9e48eb0cf24`).
- m1n1 commit: `8371e3674ba0944c4a32068f0ba659cbb0e57e77`.
- Mu commit: `5acdb4a7459d6de20bccea5cc1cf14c9f9dea06b`.
- Preserve the recorded nested metadata dirt; never stage it implicitly.

## Current GPU package

- EXP-156 `oem18.inf`, version `23.6.21.184`, source `6ac19e9`.
- SYS: `423b39307b5a56ab4cdb77866ca733d4f9cfa629a3d3cca63faa94239f076b2f`.
- INF: `6d267f09f51e505ac869d9ee0a7e0d566dc4e20b9e4629b32085f8a18cc375cc`.
- CAT: `36c525a10d4a323a6fc4f8088b22e5f23741ee68d45769b85fe8693d057b063b`.
- Compile/runtime contract: WDDM 3.0, `0x510` bytes, Version `0xF003`.

## Proven lifecycle boundary

- Current: `DriverEntry OK -> DxgkInitialize OK -> AddDevice OK -> StartDevice absent`.
- Last package that reached StartDevice: EXP-138/140 SYS
  `841dc5cb713ea3a61731a8b915ec0827c18add102f3de31da515fd3f77d4300a`.
- That reference reached StartDevice stage 6, powered/mapped AGX, and timed out
  before RTKit HELLO. It does not prove later GPU stages.

## Last experiment

- EXP-156 verdict: rejected after one cold natural-enumeration run.
- Exact package and signer matched. DriverEntry, DxgkInitialize, and AddDevice
  succeeded; StartDevice was absent; SetupAPI recorded Problem 31 and
  `STATUS_DEVICE_CONFIGURATION_ERROR` (`0xC0000182`).
- No GPU hardware-owning receipt was produced. One Event 129 also failed the
  independent platform-health gate.

## Rejected causes

- Dirty or retained phantom devnode alone (EXP-153).
- Same-boot restart alone for the EXP-154 package (EXP-155).
- WDDM table size alone: `0x608` was insufficient (EXP-154).
- Matched WDDM 3.0 `0x510`/`0xF003` alone (EXP-156).
- Exact offline comparison found equal 180-instruction DriverEntry shapes, 32
  declaration stores with identical offsets/opcodes, `0x510`, `0xF003`, import
  sets, and normalized INF contracts in EXP-138 and EXP-156.

## Active hypothesis

The strongest remaining distinction at the current boundary is admission
sequence, not downstream GPU code: EXP-138 reached StartDevice during a fresh
live remove/delete/scan/add-install transaction, while EXP-156 failed during
natural cold enumeration. The profile-dependent opaque adapter allocation size
is real but weakly causal because Dxgkrnl does not inspect the miniport context.

## Next actions

- Offline: complete; package declaration and INF admission contracts match.
- Hardware: EXP-157 only. Boot the exact G2 pair once, then apply the exact
  single fresh live add/install sequence to the unchanged EXP-156 package. No
  retry and no GPU hardware operation are allowed.
