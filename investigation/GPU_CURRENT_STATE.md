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
Before another hardware package, instrument every frontend `SetError(hDevice,
E_*)` through one `reject-seterror` path that records `__func__`, source line and
HRESULT only when `APPLE_AGX_UMD_REFUSALS_ONLY=1`. Add a deterministic offline
test proving success remains silent and each rejection emits one line with exact
arguments. In the same offline phase, close the EXP755 bridge checks required by
R1: second-batch color/depth LOAD action from valid attachments, state persistence
without rebinding, and both the native `draws != 1` and draw-receipt guards.
Source-first verify and remove the remaining exact-value admissions on this
reached FL10_0 path: D3D10 topology propagation, contract-wide instancing,
constant-buffer slots and shader-resource ranges. Do not run Air again until
these gates identify or exclude the exact first rejection.

## Fixed experiment procedure
Git `/opt/homebrew/bin/git`; artifacts live under main repo `.local`, not the
worktree. Builder `pauls@192.168.1.24`, key `~/.ssh/windows_builder`. Air
`pavel@192.168.1.37`, key `~/.ssh/air`, pinned known-host file from EXP641.
Preserve TESTSIGNING and existing signer; Smart App Control is separate.

Before asking the operator, probe Windows SSH and both proxy/vUART USB endpoints.
Full owner uses EXP584 m1n1 plus EXP406 Mu; ordinary recovery uses EXP377 m1n1
plus EXP392 Mu. Set LLDDIR=/tmp/agx-lld-dir and the frozen-launch compatibility
environment; full owner additionally needs WOM1_AGX_G2_POWER_BROKER=1. Keep the
launcher foreground with a durable log. Verify native CAT and exact hashes,
install once, collect ETW/dumps/receipts before cleanup, remove exact package and
hash-matched residues/signer, then restore ordinary GPU-visible recovery.

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
[NOW] Exact frontend SetError attribution, honest batch-split color/depth/state
      gates, and source-verified removal of reached FL10_0 draw/bind exact-value
      admissions. Offline; EXP755 repeats E_NOTIMPL while reject logs are empty.
[NEXT] Build/sign/hash and preregister one candidate containing only independently
       offline-proven fixes exposed by the exact rejection. Offline until package.
[HW] One standard-runtime Air run must produce the first DWM-correlated native
     graph -> KMD Render/Patch/Submit -> physical AGX completion -> DXGI Present,
     or name the next exact semantic RED.
POST-HARDWARE: native multi-draw batching, optional features, performance,
sustained desktop stability, OpenGL and CS1.6 after accelerated-desktop acceptance.
