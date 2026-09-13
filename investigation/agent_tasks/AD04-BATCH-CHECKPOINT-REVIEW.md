# Native batch continuation checkpoint review — 2026-09-13

Starting HEAD: 8d6ad14acf6c8e1b89951b6b9ea26175d31e5ff3. User explicitly accepted all existing dirty changes as the candidate checkpoint; no reset, checkout, stash or deletion was performed. Root GPU_CONTINUATION_PLAYBOOK.md supplied the missing handoff. Current user desktop acceptance takes precedence over its older OpenGL/CS1.6 final-acceptance paragraph.

## Source-first contract and scope
Inspected adapter/capture changes, umd_draw_composer.c, umd_win32_screen.c contracts and Windows fixtures, native-state/runtime build scripts and test project. Actual pinned Mesa agx_state.c SHA256 5015b75863202a170f8d6015eb82a76a70ecd4b92204894fa3d1886b1baa94ba; agx_pipe.c finalization and agx_batch.c submission identify the next producer boundary. No platform behavior changed; m1n1/Mu/interrupt/DMA/power contracts remain preserved, no live-machine-dependent claim made.

FULL GRAPHICS: physical/patch-list WDDM, typed identity capture, immutable request-local command materialization and exactly one pfnRenderCb. Native capture owns source BO lifetime; composer owns Windows submission holds, runtime buffer adoption and completion marker; existing KMD owns placement, patching, hardware submission and interrupts. No GPUVA or Linux syncobj/ioctl transport introduced. Complete final capture is required before provider activation.

All initial dirty files relate to this GPU task. Adapter/capture additions are coherent but success-only tests missed a defect: post-Render missing event left adapter Sealed, blocked Retire and allowed a retry to abort native holds. The fix preserves Submitted even without an initial fence, retries the existing ordered marker at Retire, binds capture to that same fence, and only releases after completion. WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? Premature source release or replay after Render entry and before ordered completion.

## Fresh deterministic evidence
Evidence directory: investigation/evidence/AD04-batch-checkpoint-review/.
- red-source.tar.gz preserved original buggy adapter with new regression tests.
- adapter-red/result.json: x64 buildExit=0, execution=16. The new missing-event lifecycle assertions reproduce the defect.
- green2-source.tar.gz SHA256 c21cabdd4a765fc5032fa79d233c8a4672f910d03af5b533115a0560ef893fe4.
- green2-x64/result.json: build_exit=0, execution=0; EXE SHA256 8d4769651b723081691c85eb3633cecd97e6b9de4ff477ddc1191f370bc057f3.
- green2-arm64/result.json: build_exit=0, execution=NOT_RUN; EXE SHA256 5d7341de38063a6932dad127b782eb94e79dadd4f47e7c9c3ab5cfdeafccfbf8.
- Both Windows MSBuild gates report zero warnings/errors. x64 output: UMD composer=0 failures, adapter=0 failures, native pool=0 errors, actual native pipeline=0 errors. Adapter scenarios: success, Render error, replacement-buffer error, missing event/retry/timeout/completion, initial timeout. Both ownership sets and exactly one Render are asserted.
- python3 -m unittest tests.test_apple_agx_reloc_capture tests.test_apple_agx_native_bo tests.test_apple_agx_native_device tests.test_apple_agx_relocation tests.test_change_ledger: 6 tests PASS. Native BO fixture had a pre-existing omitted optional SubmitDraw initializer causing -Werror; explicit NULL restores the existing lifecycle test without enabling a provider.

## Unfinished work explicitly isolated
The new dirty-zero native-state fixture roots the real agx_encode_state runtime dependency closure. red-results/build.log and green-x64/build.log record 140 unresolved symbols; it did not execute. Preserve this fixture under EnableNativeStateTest (and build-native-asahi-state.py --native-state-test), requiring EnableNativePoolTest. The normal pool/pipeline test does not imply that the full state emitter links. No substitute functions or stubs were added.

Adapter is test-linked, not connected to production native flush. Complete final VDM/PPP/attachments, nested uniform/resource edges, source immutability and terminal teardown remain required. Initial state precedes agx_encode_state; actual finalization is agx_pipe.c agx_flush_render/agx_flush_batch, including VDM stop, BG/EOT and final scissor/depth-bias uploads. General-backed PPP and CF binding relocation need a narrow typed contract before emission hookup. Runtime-closure runner is a separate compiler probe; an observed Linux dependency failure is not a successful runtime closure.

No hardware candidate, install, launch or hardware verdict. Smallest future hardware checkpoint is one complete native request reaching physical completion after all graph/build/sign/hash gates; ordinary GPU-visible exact-package cleanup remains recovery. No experiment ledger archaeology was needed.
