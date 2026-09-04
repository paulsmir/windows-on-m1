# GPU current state

Updated: 2026-08-28T02:01:11Z

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
- Root state commit: `7b01449d5bdd611b1adef927eb886c9caaf3abca`.
- m1n1 commit: `4108e79c69bac112ffbebf452fccf352c93c1dd2`.
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
- Accepted assisted G2 m1n1 / Mu SHA-256:
  `23749d4c3b9a93c637d367613a99109aea9b6d90394559ae9a2e683d4fb8bf02` /
  `34c0b278b688348b79991d30e2f8c3f0a1e8305179b7c4b6ea298473e422e7f9`.
  Its debug/full, display/both, `agx-g2` manifest and all artifact hashes pass.
  Only `m1n1.macho` and `J313_EFI.fd` are authorized; `boot.bin` is not used.

## Proven lifecycle boundary

- Current: `DriverEntry OK -> DxgkInitialize OK -> AddDevice OK -> StartDevice absent`.
- Last package that reached StartDevice: EXP-138/140 SYS
  `841dc5cb713ea3a61731a8b915ec0827c18add102f3de31da515fd3f77d4300a`.
- That reference reached StartDevice stage 6, powered/mapped AGX, and timed out
  before RTKit HELLO. It does not prove later GPU stages.

## Last experiment

- EXP-158 used the exact assisted G2 pair; no standalone image was used.
- Driver-clean preflight passed with one inert APPL0002, eight CPUs, healthy
  services, and zero Event 129/WHEA/BugCheck/critical event.
- Exact admission `oem18.inf` installed, but APPL0002 stopped at Problem 37 and
  `0xC0000059` (`STATUS_REVISION_MISMATCH`) with no lifecycle receipt. The clean
  declaration was rejected in `DxgkInitialize`, before AddDevice.
- Cleanup restored one inert APPL0002 Problem 28 with zero GPU package/service/
  certificate. The same healthy GPU-visible assisted guest remains running.

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

Active hypothesis: the first clean declaration omitted the base mandatory
interrupt/DPC/child/power/reset callbacks. Exact `STATUS_REVISION_MISMATCH`, the
accepted full table, and Microsoft's official DxgkInitialize example make this
the strongest causal explanation. Inert/fail-closed stubs for only that group
should permit DxgkInitialize while retaining zero GPU hardware access.

## Next actions

- Build and independently accept the admission package containing only the
  tested base callback group.
- Preregister one package-only EXP-159. Reuse the currently running healthy
  GPU-visible assisted guest; do not reboot and do not use standalone. Install
  the exact new package once, collect admission receipts and health evidence,
  then remove that exact package. Do not retry EXP-157 or EXP-158.
