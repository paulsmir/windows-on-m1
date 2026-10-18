# Persistent encoder root result — 2026-09-13

Source contract: AD04-ENCODER-ROOT-PLAN.md. Started from clean ce1e81875a43a4033007de3d54c264a9a2c45dd1. Source-first review used pinned native agx_batch initial allocation, agx_state initial/state/direct-draw and reserve sites, and agx_pipe finalization. No m1n1/Mu/Windows DDI/capability/power/IRQ/DMA change; physical patch-list architecture preserved.

## Implemented
Stable caller-owned EncoderRoot owns one capture reference/BO hold. A private capture association preserves that root across detached native calls and prevents generic creation of a second Encoder even while detached. Root Begin is detached; Enter/Leave bracket native operations. Full live/captured owner identity, BO/map/base/capacity, request identity and stable object identity are checked. Borrowed state scopes use the same reference and root-relative relocation offsets. Child Finish updates completed extent without shrinking the root; Finalize requires no open child, an exact bounded end and records final length once. Existing generic overlap policy remains intact.

The actual agx_encode_state projection now borrows an entered root. Actual agx_draw_vbo projection preflights the exact native estimate plus AGX_VDM_STREAM_LINK_LENGTH+0x800 before the original native ensure/allocation/jump; active capture without a matching entered root fails closed. A second caller guard after state emission rejects a changed capture, failed backend or aborted/non-recording capture before restart/index/direct-draw writes. No-capture behavior is preserved.

The final source span exception is narrowly limited to v3 Draw, exact Read, the designated Draw.EncoderReference, Encoder allocation class and length 69+4n. Offsets/pointers/fields remain aligned. Other native exact-span roles retain their established behavior. The source tail is 5+64 bytes; no rounding or capacity padding was added. The existing materializer final-object limit still applies.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Per-state overlap/duplicate encoder ownership, child root truncation, incorrect relative field offsets, stale detached reentry, lingering stack scope pointers, duplicate finalize, rollover writes after rejection, and rounding/rejection of the exact native tail.

## Fresh evidence
Final archive: investigation/evidence/AD04-native-encoder-root-20260913b/source.tar.gz.
SHA256 34043fad8acb277b71082b27fbccb51e8532b0a7f55e6d1a5199c338543695cc.
Parent compared every archived source file to the final worktree before commit.
Windows x64 native/relocation compile and execution PASS; UMD test build/link/execution PASS. EXE SHA256 ca54c12874e34e8ef0b0b9aba43bef75a3f3552693ac5dd4e2cd50c9ff45ce3e.
Windows ARM64 native/relocation and UMD build/link PASS; execution NOT_RUN. EXE SHA256 d953cd06c9b91bd9072bc4bba75034687bea2df906ece01890c894c7583ab1e5.

Focused owner tests exercise nested real PPP/USC with nonzero borrowed offsets, one Encoder reference/hold, detached deactivate/reactivate/reentry, owner/map/base/capacity/copy mismatch, open child, generic different-BO rejection while entered and detached, exact final105 bytes and duplicate finalization. Eight reserve cases cover exact fit, +1 overrun, arithmetic overflow, changed end/current, missing BO/root and stale stored identity. Rejections preserve source bytes and holds until explicit abort. UMD output reports zero composer/adapter/pool/PPP/root-reserve errors and creates/maps/unlocks/deletes all9 with backends0.

The first Windows snapshot a built but executed exit1 solely because owner fixture expected the old seven allocations; two dedicated root/reserve BOs were new. Source review identified those two calls; expected count corrected to nine with a balance receipt. Snapshot a remains preserved, not reinterpreted as passing.

Span test reproduced prior Alignment rejection (evidence/AD04-encoder-root/span-red.log). Final tests reject tiny/arbitrary lengths, wrong root designation, legacy versions, non-read access, unaligned offset and General class. Materializer preserves exact69 bytes at two placements. Final host suite: nine tests PASS; evidence/AD04-encoder-root/host-final.log and host-command.json. Relevant tests use ASan/UBSan. Native compiler warnings remain recorded separately; full runtime batch_exit remains1 at the Linux tail.

Read-only review found and fixed generic second-Encoder creation, incomplete identity comparison and overly broad span admission. Corrected source and focused negative cases were re-reviewed before the final Windows gate.

## Remaining production boundary
This is a verified persistent root component and source-projection guards, not a completed producer capsule. There is no production RootBegin/Enter/Leave/Finalize lifecycle at native batch init/draw/flush yet. Next must attach stable storage to the actual Windows native batch/request lifetime, cover initial/viewport PPP and full direct draw/tail in that root, and release only through pre-submit abort or ordered adapter retirement. Whole agx_encode_state linkage/execution, uniform/resource/attachment/BG-EOT graph and Windows-only flush finalization remain unresolved. Provider stays disabled and no hardware candidate is declared.

All tests in this entry are offline. No installation/launch/hardware result; no full experiment ledger read. Eventual physical completion discriminator still requires complete graph/runtime closure and preregistered package/hash/health/recovery gates.
