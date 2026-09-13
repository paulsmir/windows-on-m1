# GPU current state — 2026-09-13

Integration worktree: .worktrees/integration-ad04-windows-compiler.
Read root investigation/GPU_CONTINUATION_PLAYBOOK.md after this file.
Do not read full EXPERIMENTS without a specific referenced evidence need.

## Mission and closed architecture
Full Graphics hardware-accelerated Windows desktop NOT ACCEPTED. Acceptance:
standard hardware D3D device, real dynamic Asahi draws and physical completion,
standard DXGI Present, accelerated DWM/interactive desktop,1000 Presents,
100 window lifetime cycles,30-minute stability and reset/re-entry.
OpenGL/CS1.6 come after desktop acceptance. Leave final accepted package installed.
Physical/patch-list WDDM is fixed; GPUVA migration CLOSED/NO.
Real Asahi graph -> typed capture -> immutable request materialization -> existing
UMD composer/pfnRenderCb -> existing KMD Render/Patch/Submit -> AGX completion.
No native ANS work, no merge/push, no platform/recovery redesign.

## Verified qualification implementation
Base native lifecycle checkpoint36ae38d (implementation9829689). The next coherent
slice adds a shared production scene, actualKMT bridge/client, reference-counted
residency in existing ScreenBuffers, fresh-output correlation and native KMD
completion/rawreadback receipts. The installed UMD pipeline mask remains0.
Source contract: agent_tasks/AD04-NATIVE-KMT-CALLBACK-CONTRACT.md.
Final evidence: AD04-runtime-closure/native-kmt-final-20260914i-x64 and
native-kmt-final-20260914j-arm64. x64 actual2scenes/2placements, pending/immediate
markers, residency accounting, stale-receipt rejection, cleanup/recreate PASS.
Both contracttest and realKMT client build/link PASS onARM64 (execution NOT_RUN).
i sourceSHA be10756af046c0dacf6b6288b81f9f86340c410157e3bb7dd21c9f95d039304c;
i clientSHA e7e6cec8a17359fd6e7c005539d49924cf26de959f11bc52f16de4eaa640a328.
j sourceSHA 8aa7baceed972d8e25686bda287ed90f210cb2b325ba0a28ceaf3701e0120d16;
j clientSHA 4ebcf1b951292483372844900166e29512e36f19e07826aae2b874557065a69e.
Clients above use expectedbuild0 and refuse qualification; full native callgraph
is linked (12MB), build expectation passed at runtime from the compiled caller.
Native scene compilation/BOupload stays synchronous on the owning thread.
ARM64 imports realKMT APIs andVCRUNTIME140/UCRT; target availability unverified.
PE/source review: evidence/AD04-native-client-pe-20260914j.
16hosttests + qualification-enabled KMD ClCompile/codeanalysis99objects PASS:
evidence/AD04-native-qualification-host-20260914b and
AD04-native-qualification-kmd-20260914a. KMDsourceSHA
 a772df95611046914d45accc45af71bf7f58dbf7f6b2c7cac15ff6d76c7a910e.
Only later package-script pinning changed outside compiledsources; actualWindows
PowerShell parser PASS, two pinnedMSBuild invocations:
evidence/AD04-package-script-parse-20260914c.
No SYS/package/sign/install/hardware run yet; next is preregistered package683
VisibleAgxQualification build (sameprofile asEXP682), then livebaseline and one
nativeKMT request. Askfor no routineconfirmation. AirSSH address clarification is
pending; both expectedUSBendpoints were observed, no active launcher observed.

## Verified native lifecycle slice
The real native screen/context/resource/NIR shader/state/clear/draw/flush path
now executes through stable batch-owned root/capture/adapter storage and the
existing KMD validator, bounded placement, materializer, DMA patch and native
BG/partial/EOT root routing, then ordered retirement. Existing UmdContractTest
runs this producer; manually assembled capture is not its source.
Contract/source review: agent_tasks/AD04-NATIVE-LIFECYCLE-INTEGRATION.md.
Implementation index: CHG-20260913-NATIVE-BATCH-LIFECYCLE in CHANGES.csv.

Supported minimum: one nonindexed3-vertex triangle, first0/one instance, one
uncompressed BGRA8 single-layer/mip/sample RT; no depth/bias, geometry/tessellation,
compute/indirect/query/streamout/application texture/UBO/SSBO or scratch/spill.
Unsupported operations reject before submission. No false feature-level claim.
Actual census:30references,126relocations,9Windows allocations,137encoder bytes,
16384RT bytes,29copied objects,36496source bytes,44952DMA bytes. Both actual KMD
placement variants pass. v4 bounds64refs/128edges/8192command bytes; legacy limits
remain versioned. No second allocator/composer or GPU mapping was introduced.

Lifetime tests prove pending-marker holds, wrong-fence rejection, ordered release,
unsupported next topology without a new batch, retry after two deallocation
failures, and a second real screen/context on the same backend. Detach retains
configuration on failure and clears it only after successful owner leave.
Capture owns source BO holds; composer owns Windows submission holds. Render
entry forbids replay/abort. Pools/framebuffer/capsule survive until retirement.

## Final evidence and precise limits
Fresh x64 full native archive/build/link/execution PASS:
evidence/AD04-runtime-closure/native-lifecycle-final-20260913aa-x64/.
Archive SHA2564c18095f5cb47f09cd0121b37fc75f14b6e77915d7ce06dcd7611d490870d9eb.
EXE SHA25650ac86a0c655685f66badb5ac1548235468298e1f035932fa3776aa3bc5ac6e3.
All462source files match that snapshot; archive AppleDouble metadata is counted
separately in final-source-match.json. No source changed after these final gates.
Fresh ARM64 full native archive/build/link PASS, execution NOT_RUN:
evidence/AD04-runtime-closure/native-lifecycle-final-20260913ab-arm64/.
Archive SHA256493b43a73a016c8b59a70def172c754e8b5a1814d1972090c98661283a735995.
EXE SHA2563376416d21c1f82040241603224f7ee8782164f364576a1b95f4c870e0d33384.
UMD MSBuild both0warnings/errors; native compiler logs preserved separately.
Ten focused ASan/UBSan hosttests PASS: evidence/AD04-final-checkpoint-host-20260913a.
Pinned26100 ARM64 KMD ClCompile + code analysis PASS:99objects,0warnings/errors;
evidence/AD04-final-checkpoint-kmd-20260913a. Subsequent native detach/test changes
do not change those KMD sources. No SYS/package/sign/install/hardware was run.

Retained causal failures m..x are scoped under AD04-runtime-closure, summarized
in the integration contract. Key fixes: disabled timestamp conversion; unsigned
packed native blend key; absent texture/query roots; tagged32-bit USC restoration;
explicit native empty-tile flag translation; exact even-byte v4 shader spans.
AD04-native-blend-abi disproves NIR layout mismatch. No NIR assertion disabled.
No-query scratch slots are unused by admitted VS/FS and canonicalized only at
root upload; global Asahi scratch semantics remain unchanged.

## Exact next target
Do not stop at this offline proof. Prepare a truthful native hardware qualification
in the existing AppleAgxD3dKmRender project using real KMT allocation/context/
Render/ordered CPU-event callbacks and this same owner/composer/native lifecycle.
No mock KMD, SetEvent completion, manually built capture or teardown injection on
hardware. This is KMT qualification, not runtime-supplied Direct3D pKTCallbacks.
Source parameters must derive from verified J313/device facts, not the offline
fixture. Preregister the actual native graph and exact package after remaining
client/package/sign/hash and live-baseline gates; no hardware readiness yet.

Continue production integration in parallel with that boundary:
agent_tasks/AD04-NATIVE-RUNTIME-ACTIVATION-NEXT.md. Existing
AgxD3d10WindowsCreateDevice still uses a map-only pipe. Replace that factory owner
with real native screen/context after runtime callback initialization; defer Mesa
frontend screen creation to CreateDevice and link this same closure into UMD.
Installed pipeline mask remains0 until the selected DDI/FL contract is truthful.
Standard Present/shared resources/redirection/DWM and desktop stability remain open.

## Preserved hardware/recovery boundary
Air health NOT checked this turn; historical health is not current health.
Retained-root/firmware/AGX output/fence proofs remain closed; compact reference:
agent_tasks/AD04-PRE-COMPOSER-STATE.md. Ordinary377/392 GPU-visible recovery must
retain one inert APPL0002 and no AppleAgx package/service/module/signer/staged driver.
Before physical requests check both bounded Windows SSH and proxy/vUART/launcher.
Before any hardware build/run preregister exact hypotheses, source/package hashes,
commands, receipts and recovery in EXPERIMENTS; collect evidence then exact-package
rollback according to the root playbook. Do not use GPU-hidden routine recovery.
