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

## Active hypothesis and path decision

`WHY CLEAN RECONSTRUCTION:` One bounded EXP-138/EXP-156 pass found matching
DriverEntry declaration shapes, WDDM 3.0 contract, imports, callback assignments,
and normalized INF contracts. Its only strong procedural distinction was live
admission sequence; EXP-157 could not test it because the health prerequisite
failed, and an exact retry has no new falsifiable reason. More historical
comparison would now increase ambiguity rather than reduce the causal set.

Active hypothesis: a separate minimal, fail-closed WDDM 3.x admission driver
will distinguish accumulated AppleAgx initialization/callback groups from the
WDDM/PnP/package/environment contract. It must stop at StartDevice and contain
no GPU hardware access.

## Next actions

- Offline: define and test the smallest separate WDDM 3.x admission package
  using only the proven invariants above. Add existing callback groups only
  after the minimal path is admitted.
- Hardware: none currently allowed. First require RED/GREEN source tests,
  official ARM64 WDK build, machine-code/ABI inspection, exact hashes, and a
  preregistration containing `WHY THIS HYPOTHESIS:` with one to three evidence
  items. Do not retry EXP-157.
