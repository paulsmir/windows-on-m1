# GPU current boundary — 2026-09-23

## Working mode
One executor; routine build/test/SSH/launch/recovery work stays in this task. No
new agents without user direction. Read this compact file first and consult only
experiment evidence named here. Do not load the full historical ledger.

## Objective and fixed architecture
Stable, visibly correct accelerated Windows desktop on Air M1 is UNPROVEN.
Architecture remains: real Asahi producer -> typed capture -> immutable
request materialization -> UMD composer/pfnRenderCb -> physical/patch-list KMD
Render/Patch/Submit -> AGX. GPUVA is CLOSED/NO. Do not redesign these layers.
OpenGL and CS1.6 follow accelerated desktop acceptance.

## Proven hardware boundary — EXP753/package748
Package748 passed ARM64 analysis, Universal ApiValidator, Inf2Cat, signer,
catalog membership and native Air CAT verification. Hashes: CAT
3f459c5f2a667ffd870e78e96e5ac4175642e8766bdc29e3f1c7b39325301cab;
INF 64138fe2a75bf7cb0f2540ae88563f5579b582b9078fa5d6892a569a04838d6a;
SYS 306444414d80a6fdd8a3f3a7d7c4741fd7357d490176a0821e58985877ff812d;
UMD 5bf50a9c7fd493a6848f66ad24f3ec54a64bef9828f3d9f1c99d5e1bd781519d.

EXP753 fixed the EXP752 hardware-VS zero-base VertexID assertion. DWM PID2904
loaded exact System32 UMD748 (checksum12390304, timestamp1790151853), and
DXGKRNL HWDEVICE selected adapter `Apple AGX clean WDDM render-admission
experiment`, FL10_0, Interface0xA0006, Version0x177a. Thus hardware-device
selection is PROVEN. No dwm.exe-correlated native AGX graph, submission,
completion or standard Present receipt exists; acceleration is NOT PROVEN.
The old Wom1PresentTransferReceipt fence253 predates PID2904 and is excluded.

DWM then failed 0x8898008D in dwmcore!CD3DDevice::CreateBuffer through
CD3DDynamicAppendBuffer::EnsureByteSpace and CSharedDirect3DResources::Init.
ETW measured dynamic VB widths144,160000,240012 with Usage DYNAMIC, Bind VB,
CPU_WRITE. These are regression cases only, never an admission whitelist.
Operator saw a desktop background with black taskbar and no progress. Without
DWM AGX receipts, background is probably fallback/GDI, not our D3D Present;
black XAML/DComp taskbar is consistent with DWM losing its D3D device.
Evidence: `.local/experiments/EXP753-vertexid-hardware/causal-result.json`,
`hwdevice-submit-events.json`, `debug-dwm0.log`, `etw-buffer-events.json`,
`physical-observation.json`.

Exact748 cleanup completed. Ordinary GPU-visible recovery is active/reachable:
one inert ACPI\\APPL0002 Code28/null INF; no package/service/module/SYS/UMD or
signer; 8 CPUs, storage/USB healthy; trace environment absent. Do not retain an
AppleAgx package between experiments.

## EXP754/EXP755 boundary and hardware verdict
EXP754 implementation commit 4adc9c59 admits contract-wide D3D10 buffers and
arbitrary draw offsets. Package749 proved that DWM passes dynamic
VB144/160000/240012, dynamic IB16000, CBs, the 50x50 BGRA RT|SRV cached visual,
draw type16 and clear. It then recorded UMD E_NOTIMPL during draw type17. No
DWM-correlated AGX submission/completion/Present was proven.

EXP755 implementation commit 098ddedf adds a temporary one-draw-per-native-batch
bridge: Draw/DrawIndexed with an existing actual draw receipt invokes the
existing FlushRetire path before the next normal Mesa draw. Its x64 and ARM64
offline producer/capture/materializer/KMD/retirement gates pass, but the proof is
limited to counters. Before another package it still requires color and depth
LOAD-action plus state-persistence checks across the split; native multi-draw is
post-hardware performance work.

Package750 was installed once after native CAT/hash verification. DWM selected
Apple AGX FL10_0 and exact System32 UMD750 SHA256
50089bf12bce3a8fe790fae3ecd34fe6380336182416fa1acd513feea6ca4177. The
60-second trace contains 632894 events and lost0. First DWM PID6496 finishes
draw type16 and clear, starts type17 at 14:01:26.0797609, then records bad UMD
E_NOTIMPL at 14:01:26.0797925 without a type17 Stop. Retry PID3456 records the
same E_NOTIMPL during type17; a later DWM Stop/SchedulePresent occurs only after
device removal and is not execution proof. `umd-refusals.txt` is empty. The
retained present-transfer receipt is unchanged from login to final capture.
There is no DWM-correlated native graph, KMD Render/Patch/Submit, physical AGX
completion or standard Present receipt. Physical screen behavior was not
observed for EXP755.

Verdict: REJECTED_NO_CAUSAL_ADVANCE. The multi-draw hypothesis is not sufficient,
and the exact rejecting frontend DDI remains unknown because direct SetError
paths are not instrumented. Evidence:
`.local/experiments/EXP755-multidraw-offline/causal-result.json` SHA256
59c03763d81cd5fb67835046ecd8ce6654909c612d35c5a9654e9e3c11d28eb4 and
`dwm-first-failure-window.json`. Exact750 cleanup completed. Ordinary GPU-visible
recovery is restored: one inert ACPI\APPL0002 Code28/null INF; no package,
service, module, SYS/UMD or signer; 8 CPUs and storage/USB healthy.

## Current causal target
The post-EXP755 offline gate passes at source-diff SHA256
`efb35c31b25f510c4da886b55df33a959bd4dac1bba090eeeae1f9f5d668d2cf`.
Every frontend SetError now emits one refusals-only `reject-seterror` record with
function, generated-source line and HRESULT; a successful call stays silent.
The real producer/capture path proves second-batch color and depth LOAD, stable
draw-state roots without rebinding, and both one-draw guards. Basic FL10_0 point,
line-list/strip and triangle-list/strip topologies, arbitrary counts, instancing
and nonzero StartInstance propagate into the typed encoder. CB slots 0..13 and
SRV slots 0..127 are admitted; authored cb1/t1 shaders prove their real captured
relocations. x64 full integrated execution and ARM64 archive/link gates pass.

Adjacency topology is NOT claimed: Asahi routes it through a passthrough GS and
the current mixed-compute capture fails at a separate graph boundary. It is
post-hardware completeness, not part of the reached DWM basic-draw candidate;
the frontend emits `reject-capture reason=adjacency` and fail-closes before the
unsupported graph rather than leaving an unattributed native fault.
The current target is one exact build/sign/hash/preregistered ARM64 package and
one Air discriminator with the new SetError attribution.

Package751 is the preregistered candidate from implementation commit 39a5bc7b.
ARM64 analysis reports 0 warnings/0 errors; Universal ApiValidator, Inf2Cat,
signer thumbprint and catalog membership pass. Hashes: CAT
3ce86663ea375e39225a50d037ac67c7342007b5ca0c79e6e739727d44310a15;
INF 37ff68367989d38f5a56d33dddbaf15926cb3b000cc61f5ba54e573902709fe6;
SYS d00799c143483b8c4385a8ca3c7a624a63706a5509796c9815a6684e814a41f5;
UMD 25b62f5b5b3eea4abd669d9b5091e6e3e4ddae6f7d64613eb802e36456c1c5bb.
Those hashes identify the package later executed in EXP756 below.

## EXP756 hardware verdict
Package751 was installed once with native catalog verification and exact active
SYS/UMD hashes. Autologon entered `J313-WIN\\pavel`; DWM selected the Apple AGX
hardware device but repeatedly failed with 0x889800c0. The 45-second trace has
630464 events and lost0. The new diagnostic records 1890 identical exact
rejections: `reject-seterror fn=CreateResource line=453 hr=0x80004001`.
Generated `Resource.cpp:452-453` is the `bufferResource && !validBufferUsage`
branch. This is causal advance over EXP755: the invisible E_NOTIMPL is now
owned by buffer-usage admission, not inferred from draw type17.

No DWM-correlated native graph, KMD Render/Patch/Submit, physical completion or
standard Present was proven. The retained present-transfer receipt is unchanged
from login to final capture and remains the pre-existing value. Exact oem5 and
package751 were removed; ordinary377/392 recovery is active with Code28, no
package/files/signer, while autologon remains enabled by explicit user request.
Verdict: REJECTED_WITH_CAUSAL_ADVANCE. Evidence:
`.local/experiments/EXP756-dwm-frontend-contract/causal-result.json` and
`hardware-evidence/umd-refusals.txt` SHA256
2d24455119386ca1043da5d8f9e9b16a0b6ac24598c494fcf84a5e9014c85075.

## Buffer-usage offline verdict and next architecture
The EXP756 `Resource.cpp:452-453` boundary is PASS_OFFLINE at source-diff SHA256
`293bd30a6d23da3ff74ae9121b882b491cb56693a1c3a0339d3acb8848f764be`.
Pinned WDK26100 `d3d10umddi.h` (SHA256
`61899403d94840fab282dbb7da6faf234e2954bbdb47e3455f0f0572eb4e723a`),
Microsoft D3D10 DDI documentation and Mesa commit
`9aa1215f878b504f66159dd2ead4c7973142126e` establish typed buffer SRV/RT
binds, buffer view element ranges, and DynamicResourceMapDiscard for dynamic
SRV buffers. The implementation admits contract-wide SRV/RT buffer creation
without size or bind-combination whitelists; constant buffers remain exclusive,
output binds remain DEFAULT-only, and dynamic SRV buffers remain WRITE_DISCARD
without WRITE_NOOVERWRITE.

The real frontend creates a `PIPE_BUFFER` view with byte offset/size, authored
`Buffer<float4>.Load` reaches TGSI SAMPLE_I then NIR `txf`/BUF, and Asahi emits
its native texture-buffer descriptor. Capture records the selected buffer range
as a relocatable texture reference. The same candidate emits
`reject-buffer-usage` with usage/bind/map/misc/logical bytes before the generic
SetError record. x64 integrated execution passes with the typed view range
offset32/bytes64, command v6, three texture-address relocations and two KMD
materializations; ARM64 archive/link and UmdContractTest build pass. Those
numbers are regression evidence only, not admission conditions.

Ownership remains unchanged: the D3D10 frontend owns usage/view/map validation;
TGSI/NIR owns shader lowering; Asahi owns descriptor emission; Windows capture
owns relocation; KMD owns materialization/submission. m1n1/Mu continue to own
the already-proven inherited hardware/ACPI contracts and are unchanged. The
smallest falsifiable checkpoint was one real typed-buffer draw through capture,
two placements and retirement; failure remains fail-closed before hardware.

Later user direction recorded as REVIEW R13 selects GpuMmu/GPUVA for the next
phase. Therefore no package752, EXP757 preregistration or Air run is authorized
from this capture-path candidate. The frontend/shader contract carries into the
new G0/G1 phase; recovery remains the ordinary GPU-visible EXP377/EXP392 pair.

## Fixed experiment procedure
Git `/opt/homebrew/bin/git`; artifacts live under main repo `.local`, not the
worktree. Builder `pauls@192.168.1.24`, key `~/.ssh/windows_builder`. Air
`pavel@192.168.1.37`, key `~/.ssh/air`, pinned known-host file from EXP641.
Preserve TESTSIGNING and existing signer; Smart App Control is separate.
For local host ABI tests use `CC=/tmp/agx-clang-wrapper` (Homebrew LLVM plus the
MacOSX15.5 SDK and `/tmp/agx-ld64-wrapper`); the older admission wrapper is
`CC=/tmp/agx-clang PATH=/tmp/agx-cc:/opt/homebrew/bin:$PATH`. For builder
PowerShell, use the encoded-command helper `/tmp/agx_builder.py` when an inline
SSH command would cross quoting boundaries.

Before asking the operator, probe Windows SSH and both proxy/vUART USB endpoints.
Full owner uses EXP584 m1n1 plus EXP406 Mu; ordinary recovery uses EXP377 m1n1
plus EXP392 Mu. Set LLDDIR=/tmp/agx-lld-dir and the frozen-launch compatibility
environment; full owner additionally needs WOM1_AGX_G2_POWER_BROKER=1. Keep the
launcher foreground with a durable log. Verify native CAT and exact hashes,
install once, collect ETW/dumps/receipts before cleanup, remove exact package and
hash-matched residues/signer, then restore ordinary GPU-visible recovery.

Recovery baseline exception: by the user's explicit 2026-09-23 request,
AutoAdminLogon remains `1` and DefaultPassword is present. Do not record its
value. All GPU-cleanliness checks remain unchanged.

DirectFlip remains required by the advertised WDDM contract; behavior without
CheckDirectFlipSupport is UNKNOWN and observed through reject/ETW. Kernel-mode
command-buffer cap remains clear until coherent aperture exists. TDR ABI remains,
but software ResetFromTimeout does not quiesce AGX firmware; timeout is fatal and
requires reboot. General Blt remains post-first-DWM under NO_REDIRECTION unless
an actual reject-BltDXGI reopens it.

HARDWARE ROADMAP
[PASS] Frozen admission/shared/BGR/DXGI1.1 gates; EXP751/752/753 shader advances;
       exact hardware-device selection and ordinary recovery.
[PASS] EXP754 contract-wide buffers and package749 hardware buffer admission.
[PASS] EXP755 package750 exact build/sign/hash/install/evidence/cleanup cycle.
[PASS] Exact frontend SetError attribution, honest batch-split color/depth/state
       gates, draw guards, basic topology/instancing and cb1/t1 capture. x64 full
       execution and ARM64 link pass at implementation-tree hash efb35c31.
[PASS] Package751 exact ARM64 build/sign/hash/catalog gate; EXP756 evidence-first
       hardware run; exact cleanup and ordinary Code28 recovery.
[PASS] Resource.cpp:452-453 buffer usage, typed SRV load/capture relocation,
       DynamicResourceMapDiscard and argument-bearing rejection pass offline.
[NEXT] New thread: GpuMmu/GPUVA G0/G1. Do not package or run EXP757 on Air.
POST-HARDWARE: native multi-draw batching, optional features, performance,
sustained desktop stability, OpenGL and CS1.6 after accelerated-desktop acceptance.
