# GPU current state — 2026-09-13

Worktree: integration/ad04-windows-compiler.
Implementation checkpoint: 452ce35df96374bde1e3574fe889363659469e8b.
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

## First remaining integration
Use actual native-capture identity sidecar with Seal, dispatch on the supported
runtime thread, and retire via existing completion protocol.
AGX_WIN32_DRAW_REQUEST alone has no Token/Serial/Owner sidecar; do not infer it
from persistent slots or old AllocationIndex values.
Actual native agx_bo association/full typed address capture, source-byte lifetime,
shared runtime-buffer serialization and terminal teardown after unrecoverable
completion must be closed before enabling the native provider. This is an
implementation phase, not a fundamental blocker.

## Preserved proofs / machine state
Full Asahi/NIR compiler x64 execution matches control; ARM64 cross-build proved.
Prior hardware-proven retained-root, firmware, AGX output/completion/fences remain
closed absent contradictory evidence. No new hardware proof comes from this slice.
Current live Air state has not been checked. Historical ordinary377/392 health
must not be described as current. Check both SSH and proxy before any Air request.
Detailed earlier state: agent_tasks/AD04-PRE-COMPOSER-STATE.md.
