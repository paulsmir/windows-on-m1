# GPU current state — 2026-09-13

Worktree: integration/ad04-windows-compiler.
Composer checkpoint: 452ce35; retirement review correction: f635789.
Native state/pool Windows compile checkpoint: c4022de.
Native BO owner/capture connection: 863bae9.
Native USC versioned relocation fields: 00c5bdb.
Construction address -> live Windows owner/capture: a7c9e6d.
Actual native pipeline emitter capture: f287e58.
Native v3 General table source gate: e512912.
Native v3 exact source spans: 02b8dbf.
Native CPU-to-construction source bridge: 1821585.
Nested native emission scope: 04e9340.
Initial native encoder Windows intent: 5ecf767.
Active native state emission hook: 69f25be.
No merge/push or Air action in this implementation phase.

## Objective and operating contract
Full Graphics remains the acceptance target: standard hardware D3D device,
real Mesa/Asahi screen/context and dynamic draws, physical AGX completion,
standard DXGI Present, accelerated DWM/interactive desktop, 1000 Presents,
100 window lifetime cycles, 30 minutes stability, reset/re-entry, final package
left installed. Software controls and internal helpers are not acceptance.
GPUVA migration on current classic WDDM remains CLOSED/NO.
Continue physical/patch-list + complete typed relocation.
Current focus is the request-scoped UMD draw composer and its native producer.
No WGL/softpipe diversion. Tier-A decides reversible contracts; deterministic
tests/builds execute directly in this integration worktree. Native-ANS untouched.

## Reviewed continuation checkpoint (2026-09-13)
User accepted dirty HEAD8d6ad14 checkpoint for review; no reset/stash/checkout.
Review: agent_tasks/AD04-BATCH-CHECKPOINT-REVIEW.md.
Native batch adapter + PrepareDraw now have fresh Windows x64 execution PASS
and ARM64 build/link PASS, with native pool and actual pipeline controlled tests.
Source archive: evidence/AD04-batch-checkpoint-review/green2-source.tar.gz;
SHA256 c21cabdd4a765fc5032fa79d233c8a4672f910d03af5b533115a0560ef893fe4.
Missing-event-after-Render defect reproduced (x64 16 assertions) then fixed:
adapter stays Submitted, prohibits replay/abort, retries the ordered marker,
retains both ownership sets across timeout, retires on the same fence.
Five adapter scenarios pass; no production provider activation or hardware proof.
The new dirty-zero agx_encode_state fixture exposed 140 unresolved runtime
symbols. Preserved under explicit EnableNativeStateTest; NOT_LINKED/NOT_EXECUTED.
Pool/pipeline gate success must not be described as full state-emitter execution.
Next causal target: typed PPP General-pool and CF-binding source contract, then
complete initial/state/draw/final VDM and attachment/uniform/BG-EOT graph capture,
then Windows replacement at real agx_flush_render/agx_flush_batch finalization.
The adapter must never consume a partial graph. Runtime-closure probe is separate
and may report expected Linux-tail failure pending the Windows implementation.
Root investigation/GPU_CONTINUATION_PLAYBOOK.md is the operative playbook;
current user instruction puts accelerated desktop acceptance before OpenGL/CS1.6.

## New implemented boundary
452ce35 implements AdmissionUmdDrawSeal/Dispatch/Abort/Retire, compiled into UMD.
Identity: OwnerCookie + Generation + Token + Serial. The existing ScreenBuffers
are authoritative; no second resource registry.
Dense allocation indices are assigned per request, with exact identity dedup
and OR of typed Write uses. Existing wire reference/relocation indices remain.
Seal owns command bytes, list and identities; owner resolution occurs under
ScreenBufferLock. Separate SubmissionHolds block map/unmap/destroy/detach/close.
CPU source holds are not residency. Native producers must keep referenced source
bytes unchanged until consumption/completion; metadata seal alone does not copy
all BO contents.

Dispatch calls pfnRenderCb exactly once and adopts returned buffers. Accepted
Render with later buffer/event failure cannot replay. Retirement may retry an
event enqueue, waits for the in-order completion marker, then releases holds.
Timeout preserves them. One active transaction per context in this first slice.
No exported DDI or optional winsys SubmitDraw provider was enabled.
f635789 corrects missing-event stale S_OK and preserves holds after any failure
returned after entry into Render until ordered synchronization proves quiescence.
Review: agent_tasks/AD04-SUBMISSION-RETIREMENT-REVIEW.md.

## Fresh verification
Evidence: evidence/AD04-umd-draw-composer/
Contract and scope: agent_tasks/AD04-DRAW-COMPOSER-RESULT.md.
Source archive008 SHA256:
c72062c45ddd6153826bb9a8063953ade15ca5eb02cb8267cf68c9f83a99297d.
WDK26100/MSVC14.44 x64 test build/link/execution PASS (20 composer scenarios).
Test includes actual UMD owner functions and KMD reference validator/materializer;
nine references dedup to eight handles, two sequential reordered requests and
new fences, two materializer placements, errors/reentry/holds all exercised.
ARM64 test build/link PASS; ARM64 execution NOT_RUN.
Optional x64 Mesa factory test PASS.
ARM64 UMD DLL build/link succeeds with code analysis: 27 WDK header SAL warnings,
not warning-free analysis. No signing/installation/hardware verdict.
Five relevant host suites GREEN.

## Native producer compilation
c4022de compiles original pinned agx_state.c and pool.c on Windows x64/ARM64.
Derived headers retain native state layouts; source reference unchanged.
Epilog key 256-pattern native/Windows execution proves size/alignment4 and
byte-exact flags; generated helper wrappers transmit the required zero bytes.
Evidence/limits: agent_tasks/AD04-NATIVE-STATE-COMPILE.md and
evidence/AD04-native-asahi-state/. Native draw execution has NOT occurred.
Windows native BO backend now satisfies the original pool API; complete native
graph capture remains required before runtime composer dispatch.
863bae9 now executes original pool.c through the Windows UMD owner in controlled
runtime tests. Actual native BOs receive owner-generated Token/Serial identities;
two real subranges enter typed capture with one request-local allocation index.
Capture survives pool cleanup; native collection respects submission holds and
deallocation failure. Provisional allocation rollback retains explicit ownership.
x64 execution PASS and ARM64 build/link PASS. These use controlled runtime
callbacks, not an actual hardware D3D device or native draw submission.
Details: agent_tasks/AD04-NATIVE-POOL-CAPTURE.md.

## First remaining integration
Use actual native-capture identity sidecar with Seal, dispatch on the supported
runtime thread, and retire via existing completion protocol.
AGX_WIN32_DRAW_REQUEST alone has no Token/Serial/Owner sidecar; do not infer it
from persistent slots or old AllocationIndex values.
Complete native state typed address capture, source-byte lifetime,
shared runtime-buffer serialization and terminal teardown after unrecoverable
completion must be closed before enabling the native provider. This is an
implementation phase, not a fundamental blocker.
00c5bdb closes USC Preshader Code bits32..63 and Texture/Sampler buffer
bits27..62 shr(3) via explicit v3 kinds. Native-pack/KMD materializer tests cover
two placements and all table counts; Windows x64 executes, ARM64 build/links.
Details: agent_tasks/AD04-NATIVE-USC-V3.md. This does not enable the legacy
v1-only overlay or widen dynamic DMA validation. Next hook the actual
agx_build_pipeline emitter with owner-tracked source references. Mixed native
Batch pool roles/source allocation classes and full native graph/overlay remain
explicit integration work, not hardware proof.
a7c9e6d resolves construction subranges through the existing Windows NativeBo
registry and construction allocator before CaptureReference/RetainExact. Wrong
owner/generation, overflow, unknown/stale/retired ranges fail closed. Windows
x64 native pool/owner execution PASS, ARM64 build/link PASS. Caller-serialized
lookup is not residency. Plan: docs/superpowers/plans/2026-09-13-native-pipeline-capture.md.
f287e58 executes original agx_build_pipeline with Windows-owned native BO/pools
and typed USC capture: unlinked/linked, texture/sampler/custom-border/push/rodata,
exact ranges/identities, rejected untracked table and linked-only scratch.
Shared generated function body is used by native agx_state and focused test TU.
Windows x64 execution PASS; ARM64 build/link PASS; eight host suites PASS.
Controlled shader/descriptor inputs and external runtime callbacks: NOT an
actual NIR draw, pfnRenderCb, physical rendering or hardware-ready candidate.
Details: agent_tasks/AD04-NATIVE-PIPELINE-CAPTURE.md.
NEXT: implement accepted v3-only General source-class alternative for exact-Read
Descriptor/Scissor/DepthBias refs; preserve shader/USC/encoder class restrictions.
Decision: agent_tasks/AD04-NATIVE-SOURCE-CLASS-DECISION.md. Separately resolve the
legacy reference-length multiple4 rule (actual USC38/62 and rodata2 are exposed);
do not silently pad/reclassify or bundle that independent invariant.
e512912 closes the narrowly versioned General table-source gate with x64 execution
and ARM64 build/link evidence. The current first invariant is now only byte-range
representability: native USC streams can be 38/62 bytes and final rodata can be
2 bytes, while transport universally requires Reference.Bytes multiple4.
Source-first decision required before implementation; no synthetic padding or
spurious source range expansion is accepted.
02b8dbf closes that v3 exact-span admission issue for read-only Constant,
ShaderRodata and UscPipeline only; offset/pointer alignment rules stay strict.
Windows x64 execution PASS and ARM64 build/link PASS. Next: complete capture of
native encoder, scissor/depth-bias, vertex/index/render-target and nested resource
edges, then connect a complete request to the existing composer. No hardware
candidate is ready while that graph/source lifetime/retirement work is incomplete.
1821585 closes the validated CPU range resolver needed for actual VDM/PPP source
intervals. Next coherent slice: typed state/encoder emission capture from actual
native draw path, active from state generation through finalization, while
explicitly rejecting General-backed encoder rollover and unsupported resource
graph modes. No runtime dispatch or hardware candidate before its complete
source/lifetime contract.
04e9340 permits a source-defined parent state/VDM interval to surround nested
USC construction without losing capture ownership. x64 Windows execution PASS.
Next still requires the actual state/VDM emitter hook, explicit initial encoder
intent, and separate fail-closed rollover handling before a full request exists.

## Preserved proofs / machine state
Full Asahi/NIR compiler x64 execution matches control; ARM64 cross-build proved.
Prior hardware-proven retained-root, firmware, AGX output/completion/fences remain
closed absent contradictory evidence. No new hardware proof comes from this slice.
Current live Air state has not been checked. Historical ordinary377/392 health
must not be described as current. Check both SSH and proxy before any Air request.
Detailed earlier state: agent_tasks/AD04-PRE-COMPOSER-STATE.md.
