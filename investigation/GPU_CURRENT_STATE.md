# GPU current state

Updated: 2026-08-28T00:12:00Z

## Stable recovery

- Immutable EXP-123 Mu:
  `4c5e068f664d8ccc94823880de4226e3f7842e08841bc10fea19cbe9e05a519b`.
- Immutable EXP-123 m1n1:
  `3b81d82176b9853228b39eb3bb56ceff018cd0542248e872dd1bc1304c32b82e`.
- Recovery preserves eight CPUs, native input, internal display, NVMe, xHCI,
  and SSH. The post-EXP-157 cleanup boot has zero APPL0002 devices, zero
  AppleAgx Driver Store packages, zero Event 129, and zero critical event.

## Repository identity

- Root branch: `feature/j313-gpu-acceleration`.
- Root state commit: `d28bf2c` (source package commit `6ac19e9458b5d7786e2685fe7202f9e48eb0cf24`).
- m1n1 commit: `8371e3674ba0944c4a32068f0ba659cbb0e57e77`.
- Mu commit: `5acdb4a7459d6de20bccea5cc1cf14c9f9dea06b`.
- Preserve the recorded nested metadata dirt; never stage it implicitly.

## Last tested GPU package

- EXP-156 `oem18.inf`, version `23.6.21.184`, source `6ac19e9`.
- SYS: `423b39307b5a56ab4cdb77866ca733d4f9cfa629a3d3cca63faa94239f076b2f`.
- INF: `6d267f09f51e505ac869d9ee0a7e0d566dc4e20b9e4629b32085f8a18cc375cc`.
- CAT: `36c525a10d4a323a6fc4f8088b22e5f23741ee68d45769b85fe8693d057b063b`.
- Compile/runtime contract: WDDM 3.0, `0x510` bytes, Version `0xF003`.
- It is no longer installed or staged on the Air. The next experiment must
  install its own hash-verified package fresh; do not reuse a prior package.

## Proven lifecycle boundary

- Current: `DriverEntry OK -> DxgkInitialize OK -> AddDevice OK -> StartDevice absent`.
- Last package that reached StartDevice: EXP-138/140 SYS
  `841dc5cb713ea3a61731a8b915ec0827c18add102f3de31da515fd3f77d4300a`.
- That reference reached StartDevice stage 6, powered/mapped AGX, and timed out
  before RTKit HELLO. It does not prove later GPU stages.

## Last experiment

- EXP-157 verdict: aborted before its PnP transaction; the admission-sequence
  hypothesis was not tested and must not be called rejected or confirmed.
- Exact G2 natural enumeration reproduced DriverEntry, DxgkInitialize, and
  AddDevice success with no StartDevice receipt, Problem 31, and exact
  `oem18.inf`. Eight CPUs and all required services remained present.
- The prerequisite health gate failed first: four fresh stornvme Event 129
  resets occurred at ten-second intervals. No helper, PnP mutation, retry, or
  GPU hardware-owning operation followed.

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

- Offline: do not repeat EXP-138/EXP-156 admission diffs. Use only existing
  recovery/G2 evidence to determine whether a health-clean admission test can
  be preregistered without changing the GPU package or admission variable.
- Hardware: none currently allowed. Do not retry EXP-157. A future run requires
  a new preregistration with a falsifiable reason that removes or explicitly
  isolates the recurring Event 129 prerequisite failure.
