# Asahi typed relocation adapter implementation plan

> Execute inline with executing-plans; user explicitly declines routine agents
> and approval stops. Existing isolated integration worktree is the target.

Goal: move native Asahi pointer emission into the existing physical WDDM
transport/materializer while retaining exact resource provenance and lifetime.
Architecture: capture typed references, not address-shaped byte scans. Reuse
AgxWin32TransportBuildDraw and AppleAgxDynamicJobMaterialize unchanged initially.
Tech: C11, pinned Mesa pack headers, existing shared Win32 ABI, clang host tests,
Windows x64/ARM64 compiler gates. GPUVA migration remains CLOSED/NO.
Spec basis: investigation/agent_tasks/AD04-VIDMM-INPUT-DOMAIN-GATE.md.

ARCHITECT_DECISION: a per-device/per-request capture owns borrowed BO references
through exact fence retirement, but never claims to pin VidMm placement. It
emits the existing pointer-free wire references/relocations, revalidating owner,
device generation and allocation serial before sealing. It cannot alter cached
object images; the existing KMD materializer makes per-submit copies and resolves
the actual placement. This adds no wire ABI or capability change.
Alternatives rejected: a second materializer duplicates proven patch semantics;
integer scanning loses pointer provenance; persistent GPUVA violates the closed gate.

Sources: agx_win32_transport.c BuildDraw; shared Win32 ABI and validation;
apple_agx_dynamic_job.c typed patch/materialize; pinned agx_state.c pipeline
emission and generated AGX_USC_SHADER/UNIFORM/PPP pack functions.

1. Implement agx_win32_reloc_capture.h/c in mesa/winsys. Begin(owner,generation,
   request,ops), reference registration with Retain/Query/Release, typed field
   capture, Seal(Draw), Submitted(fence), Retire(exact owner/request/fence), Abort
   only before submission. No raw GPUVA/PA fields or claims of residency.
2. RED/GREEN executable composition test through existing BuildDraw/Validate and
   DynamicJobMaterialize: two placements, expected field bytes, unchanged
   non-address bytes, immutable first image, stale/foreign/range/overlap/capacity
   rejection, no early resource release, no stale retirement of the next request.
3. Use native pack functions for the address-bearing fields so expected source
   layout is not a second test encoder. Source hashes and pinned include paths
   accompany compiler controls; preserve all raw failure evidence.
4. Wire capture into native BO/pool/pipeline ownership as the next causal boundary,
   then real agx_screen/agx_context and the device-scoped D3D frontend. At every
   newly observed mismatch choose a reversible source-backed correction locally.
   Do not expose unimplemented caps or call project pipe wrapper a real Asahi backend.
5. Build/package only after the integrated path is executable and relevant tests
   pass. Hardware requires one falsifiable preregistered candidate and current
   baseline verification, not a repeat of prior admission EXPs. Standing hardware
   authorization applies; final Full Graphics acceptance remains unchanged.

Self-review: lifetime callbacks hold the driver BO object, not OS residency;
complete typed pointer-graph capture at actual emitters remains necessary before
claiming native draw coverage. Helper tests alone are not the final mission.
