# GPU current state

Updated: 2026-08-28T01:01:00Z

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
- Root state commit: `aedf6322ccc36219ffcd300ddeefad64585bdbde`.
- m1n1 commit: `8371e3674ba0944c4a32068f0ba659cbb0e57e77`.
- Mu commit: `5acdb4a7459d6de20bccea5cc1cf14c9f9dea06b`.
- Preserve the recorded nested metadata dirt; never stage it implicitly.

## Accepted next GPU package

- Clean admission source: `b13e9c32c06c21fbd522d33717a2d0078e4a077c`.
- SYS: `ebe690ac55f861c4b881ead21527348c0a27846970c23cade603004cedebe0a4`.
- INF: `3191a342e298a7587eae4eb68391c83b94fb24a14f565ae0c1ea8673186202d3`.
- CAT: `761ff3c26297f9679a4426f23eda3eb3dc27031ff916b0b35b73d42630da502e`.
- CER: `09d220fef9268478e6512effc6649ce511a2fd88aef8f506789e31474004a48d`.
- Signer: `D6EC654F91AA15EF78EA7026051C93BFDE460E0F`.
- Official run: `33130376006`; ARM64 and WDDM 3.0 `0x510`/`0xF003`
  were verified from the fresh artifact. It is not installed or staged.

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

- Offline: accept the complete `release-agx-g2` artifact from corrected run
  `33131387114`, then verify its manifest and exact hashes.
- Hardware: first qualify that recovery driver-clean with one inert APPL0002.
  If healthy, install only the accepted admission package in that same guest
  and test natural StartDevice admission. Do not retry EXP-157.
