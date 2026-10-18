# Native PPP capture result — 2026-09-13

Implemented after normalized clean checkpoint f55c708bce8389e9d5323b3c1d44f69ad0bcd889. Governing source-first contract: AD04-NATIVE-FINAL-CAPTURE-PLAN.md. Physical/patch-list architecture and all production provider gates remain unchanged. No hardware run.

## Implemented boundary
Private v3 RolePppState identifies an exact read-only General-backed native PPP suballocation. Encoder and USC still require Encoder allocations. Existing Encoder-to-Encoder PPP behavior is preserved; only v3 adds Encoder-to-PppState. Dedicated PPP pipeline/CF relocation kinds preserve the native low six/two bits and validate relative range/alignment. Materializer copies PPP source into request-owned storage. New PPP references require directed reachability from actual draw roots; legacy endpoint checks are preserved for old roles.

The original pinned agx_encode_state projection now records VDM VS pipeline references, opens an exact nested PPP emission, records fragment pipeline/CF linkage and records the final packed PPP_STATE pointer after child finish. RecordCaptured reuses a completed source reference and its exact length/identity; it rejects unknown starts and unfinished ancestors instead of guessing spans. PPP size and block type are checked against the native packed record. Original native source is unchanged; derived compilation retains existing notices and pinned-source verification.

FULL GRAPHICS ownership remains: native source capture owns BO references; UMD composer owns dense Windows allocation indices, Render and ordered retirement; KMD owns validation/materialization/patching/submission/interrupts. No Windows DDI/capability, firmware, m1n1/Mu, power, DMA or GPUVA contract changed. The source-defined translation is native General-pool PPP -> typed immutable source -> copied PPP fields -> existing physical request placement.

## Verification
WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Previously unrepresentable General-pool PPP and relative CF addresses; wrong flag masks or alignment corrupt pointers, and disconnected PPP must not be accepted as part of a root graph.

Fresh final Windows source archive: investigation/evidence/AD04-native-ppp-windows-20260913b/source.tar.gz, SHA256 8c1cd094391a0204b72933b22e41312ca39e9c9c9bc5e5d4eb7c01dc9b797d7a. Parent verified every archived source file against the final worktree before commit.

- x64 native compile + relocation/native-pack materializer execution PASS; UMD contract build/link/execution PASS. UMD EXE SHA256 98a7e4d1c75408e02d5a695ab83c1f8602b3a714c566bb1d1d3f77ff15abd022.
- ARM64 native/relocation and UMD build/link PASS; execution NOT_RUN. UMD EXE SHA256 4bae637ea96e6a281b63bb0160a50961038a673ad7db3d8668da25eabb849180.
- MSBuild UMD test reports zero warnings/errors on both architectures. Native clang diagnostics are separately preserved; this is not a warning-free full compiler/runtime claim.
- x64 native PPP fixture runs six cases through original native PPP helpers and actual USC construction: valid nested graph, unknown USC start, mismatched CF address, wrong PPP size, wrong source class and wrong packed block type. Native owner/pipeline fixture errors=0; composer and adapter failures=0.
- Capture/materializer tests prove two placements, native PPP pack/unpack, low flags, four-but-not-64 aligned CF target, source and earlier-output stability, legacy rejection, missing root edge and disconnected extra-Encoder-to-PPP rejection, below-base/overflow/misalignment rejection.
- Existing reference/class test is now a separate assertion-enabled Windows UMD test TU. PPP Read+General+v3 is accepted; Write/Execute/Encoder-class/legacy versions are rejected.
- Final host command: python3 -m unittest tests.test_apple_agx_win32_abi tests.test_apple_agx_reloc_capture tests.test_apple_agx_render_win32_transport tests.test_apple_agx_dynamic_job tests.test_apple_agx_mesa_win32_transport tests.test_apple_agx_native_bo tests.test_apple_agx_native_device tests.test_change_ledger. Nine tests PASS.
- Source-local pre-code RED observation is in evidence/AD04-native-ppp-capture/pre-fix-red.log. Parent additionally preserved raw isolated replay against f55c708 capture implementation: red-replay.json/log records a successful compile and expected assertion failure at RolePppState admission, without changing the worktree.
- Astra source review covered native hooks, PPP class and rooted traversal; Terra implemented ABI/materializer and negative tests. No unresolved source-review defect remains for this scoped unit.

## Still required before complete native finalization
Whole agx_encode_state execution remains NOT_LINKED under EnableNativeStateTest; the original Linux batch tail still has batch_exit=1. The focused gate intentionally reports this separately. No full native draw or hardware result comes from this test.

Initial/viewport PPP, the full VDM draw/termination interval, nested uniform/resource pointers, attachment descriptors and BG/partial/EOT pipelines remain unclosed. The adapter stays inactive in production. Source holds alone are not byte immutability; complete producer serialization/terminal lifetime must still be established.

Next coherent unit is one persistent request/batch encoder root and temporary borrowed emission subspans. Current separate state Encoder references cannot be covered later by another whole-encoder reference because exact overlap rejection is intentional. Store the root outside stack frames across draw/flush; borrow with root-relative relocation offsets and never shorten it at child Finish. Start after initial encoder allocation (agx_batch.c:119); cover initial state, dirty state and direct draw; finalize at native agx_flush_render current + sizeof(stop). The native stop is 69 bytes and does not advance current, so a separate v3 Read Encoder exact-byte-span rule must be source-tested, not padded. Reject rollover before allocation/jump and stop caller emission; finalization/cleanup must retain root, capture and adapter storage through ordered retirement.

Smallest eventual hardware checkpoint: one completely captured Windows-native request reaches physical completion/fence with preregistered output; ordinary GPU-visible exact-package rollback remains recovery. This unit has no remaining hardware uncertainty and does not justify a hardware run by itself.
