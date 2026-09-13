# GPU current state — 2026-09-13

Worktree: integration/ad04-windows-compiler.
Composer checkpoint: 452ce35; retirement review correction: f635789.
Native state/pool Windows compile checkpoint: c4022de.
Native BO owner/capture connection: 863bae9.
Native USC versioned relocation fields: 00c5bdb.
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
Next: Windows native BO backend must satisfy the original pool API and capture
all native graph edges using real owner identities before composer dispatch.
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

## Preserved proofs / machine state
Full Asahi/NIR compiler x64 execution matches control; ARM64 cross-build proved.
Prior hardware-proven retained-root, firmware, AGX output/completion/fences remain
closed absent contradictory evidence. No new hardware proof comes from this slice.
Current live Air state has not been checked. Historical ordinary377/392 health
must not be described as current. Check both SSH and proxy before any Air request.
Detailed earlier state: agent_tasks/AD04-PRE-COMPOSER-STATE.md.
