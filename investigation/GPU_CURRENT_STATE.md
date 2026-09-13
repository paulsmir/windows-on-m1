# GPU current state — 2026-09-13

Worktree: integration/ad04-windows-compiler.
Read root investigation/GPU_CONTINUATION_PLAYBOOK.md as the operative playbook.
Do not read full EXPERIMENTS unless evidence needed for the current decision.

## Objective and closed architecture
Full Graphics hardware-accelerated Windows desktop remains NOT ACCEPTED:
standard hardware D3D device, real Asahi screen/context and dynamic draws,
physical completion, standard DXGI Present, accelerated DWM/interactive desktop,
1000 Presents, 100 window lifetime cycles, 30-minute stability and reset/re-entry.
Leave final accepted package installed. OpenGL/CS1.6 follow desktop acceptance.
Physical/patch-list WDDM is fixed; GPUVA migration is CLOSED/NO.
Real Asahi graph -> typed owner capture -> immutable request materialization ->
UMD composer/pfnRenderCb -> existing KMD Render/Patch/Submit -> AGX completion.
No native-ANS work; no merge/push. Hardware requires every playbook gate.

## Current verified boundary
Dirty continuation at 8d6ad14 was preserved, reviewed and normalized. Clean
normalization checkpoint f55c708bce8389e9d5323b3c1d44f69ad0bcd889.
Review: agent_tasks/AD04-BATCH-CHECKPOINT-REVIEW.md.
edd25d7 implements tested native batch adapter + PrepareDraw, fixing missing
completion-event handling after Render: stay Submitted, no replay/abort, retry
ordered marker and preserve both native/Windows holds across timeout. Five
adapter scenarios pass x64; ARM64 build/link passes. d75ea9b repairs runtime
probe architecture/hash/projection/evidence handling.

NEW: native PPP subgraph and actual dirty-state emission hooks implemented.
Result/source contract: agent_tasks/AD04-NATIVE-PPP-CAPTURE-RESULT.md and
agent_tasks/AD04-NATIVE-FINAL-CAPTURE-PLAN.md.
v3-only PppState role = exact Read from General; Encoder/USC class gates unchanged.
PPP pipeline/CF fields preserve low6/low2 and relative range/alignment. PPP sources
are copied into request storage and must be directed-reachable from real roots.
Actual agx_encode_state projection records VDM VS, nested PPP FS/CF and packed
PPP_STATE pointer. Completed source lengths/identities are reused, never guessed.

Final evidence: evidence/AD04-native-ppp-windows-20260913b/.
Source archive SHA256:
8c1cd094391a0204b72933b22e41312ca39e9c9c9bc5e5d4eb7c01dc9b797d7a.
x64 native compile/relocation execution and UMD owner/pipeline/PPP/adapter
execution PASS; ARM64 builds/links PASS, execution NOT_RUN. UMD MSBuild zero
warnings/errors; native compiler warnings recorded separately. Nine host tests PASS.
Native PPP fixture uses original PPP helpers and actual USC emitter with controlled
shader inputs. Whole agx_encode_state and full native draw execution NOT_PROVEN.

## Persistent encoder root verified
Result/contract: agent_tasks/AD04-ENCODER-ROOT-RESULT.md and
agent_tasks/AD04-ENCODER-ROOT-PLAN.md.
Caller-owned root has one Encoder reference/hold, detached Begin/Enter/Leave,
root-relative borrowed state scopes and once-only exact Finalize. Private capture
association prevents a second generic Encoder even while detached. Reentry checks
full identity/map/base/capacity. Child Finish cannot shorten the root.
Actual draw projection preflights native reserve before rollover alloc/jump and
propagates capture/state failure before subsequent draw writes.
Exact-span rule: designated v3 Read Encoder root only, length69+4n; no rounding,
no offset/class/pointer relaxation. Final object size admission stays unchanged.
Final evidence: evidence/AD04-native-encoder-root-20260913b/.
Source SHA256 34043fad8acb277b71082b27fbccb51e8532b0a7f55e6d1a5199c338543695cc.
x64 native/relocation/UMD execution PASS; ARM64 build/link PASS, execution NOT_RUN.
Nine host tests PASS; owner balance9 create/map/unlock/delete, backends0.
First Windows snapshot a failed old fixture count7; retained as failure evidence.
No actual native draw or production batch capsule proof.

## Lifetime and preserved software contracts
Identity = OwnerCookie + Generation + Token + Serial; existing ScreenBuffers are
authoritative, no second resource registry. Composer assigns dense request-local
indices, deduplicates exact identities and ORs Write uses. Seal owns command/list;
SubmissionHolds block map/unmap/destroy/detach/close. Capture keeps source BOs alive,
not immutable by itself; native producer must freeze source bytes until consumption.
One active transaction per context; Render cannot replay after entry. Ordered
completion retires both hold sets. Terminal source/capsule lifetime remains required.

Pinned native source agx_state SHA256:
5015b75863202a170f8d6015eb82a76a70ecd4b92204894fa3d1886b1baa94ba.
Prior accepted native BO/pool, USC emitter, exact v3 source spans, General read-only
tables, CPU construction resolver and nested emission contracts remain in place.
Relevant records: AD04-NATIVE-POOL-CAPTURE.md, AD04-NATIVE-PIPELINE-CAPTURE.md,
AD04-NATIVE-USC-V3.md under agent_tasks/. Full compiler/NIR control is preserved.

## Exact next causal target
Wire stable root/capture/adapter storage into the real Windows native batch/request
lifetime. The root API is now tested, but production RootBegin/Enter/Leave/Finalize
calls at initial encoder allocation/draw/flush are not connected. Preserve storage
through native call returns and ordered retirement; no stack-local persistent state.
Cover initial/viewport PPP and complete direct draw/termination in the one root.
Native agx_flush_render writes69 bytes without advancing current; final end is
current+sizeof(stop). The exact-span rule and pre-rollover guard are implemented.
Then close nested uniform/VBO/resource edges, attachment texture/PBE descriptors,
BG/partial/EOT pipelines and final scissor/depth-bias roots.
Windows-only finalization belongs at agx_flush_render/agx_flush_batch; do not
import drmSyncobj/virtio/shared-BO/Linux ioctl semantics. Only a complete graph
with frozen source bytes and stable capsule lifetime may enter adapter Seal.
Production provider and broader dynamic DMA/legacy overlay remain disabled.

## Unresolved runtime closure and machine state
Full state-emitter fixture retained under EnableNativeStateTest; 140 unresolved
runtime/compiler symbols in checkpoint review evidence. NOT_LINKED/NOT_EXECUTED.
Runtime compiler probe evidence: evidence/AD04-runtime-closure/cp-review-20260913b/.
Known ABI layout projections reused; first actual failure is Linux DRM/virtio
batch tail, exit1. Native focused build records batch_exit=1 separately; its
successful pool/PPP tests are NOT full runtime closure.
No installable candidate, signing/package gate or hardware verdict from this work.
Air state has NOT been checked; historical health is not current health.
Prior retained-root/firmware/AGX output/fence proofs stay closed; earlier details:
agent_tasks/AD04-PRE-COMPOSER-STATE.md. Ordinary GPU-visible recovery must retain
one inert APPL0002 and no AppleAgx package/service/module/signer/staged driver.
Before any physical request check Windows SSH and proxy/vUART/launcher yourself.
