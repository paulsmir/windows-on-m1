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

## Current offline boundary — EXP754
Implementation commit 4adc9c59cf4ee59948d04d1d2f75b784e10e5bb2 replaces exact
buffer/index filters with the D3D10 contract:
- arbitrary nonzero buffer widths within pinned D3D10 limits;
- DEFAULT CPU0; IMMUTABLE CPU0+initial data; DYNAMIC CPU_WRITE only and no SO
  output; STAGING bind0 with declared READ/WRITE; exclusive 16-byte CB <=64KiB;
- VB/IB/CB/SO combinations allowed by those rules; R16/R32 index binding;
- whole/region linear buffer copies use exact Asahi BO ranges;
- pending WRITE_NOOVERWRITE does not retire the immutable request; consumed BO
  maps use a resource shadow, uploaded at the next IA bind after ordered
  completion; WRITE_DISCARD remains supported;
- real draw payload propagates count/start; triangle-list nonzero multiples of3;
- nonindexed StartVertex=5 and indexed StartIndex=1/BaseVertex=7 pass actual
  producer, capture, composer, physical KMD plan/patch and retirement;
- indexed capture validates R16/R32 encoder tags, logical used bytes, aligned
  physical fetch span and encoded BaseVertex; KMD no longer inspects literal
  index contents or requires offset0/exact8.

Final source archive `buffers-green25-source.tar` SHA256
50254acd513b7ff1180800ed3cc74d22231e70de17dc672cd38615d166f596af.
x64 full executable TestExit0, EXE SHA256
3d5e344511622864ab4187600198970a6ce44439e85146e76ff01520edf60470.
ARM64 native closure PASS; ARM64 UmdContractTest build PASS, EXE SHA256
75039bf7aa8e1723bacb2d4b0abd309c891b46f3bf31adeaf526f694d60c6fe3.
Host ABI, dynamic-job, reference-transport and reloc-capture tests PASS.
`test_apple_agx_mesa_win32_transport.py` also fails at clean HEAD on an unchanged
textured-v6 fixture and is recorded as pre-existing, not an EXP754 regression.
Inventory: `.local/experiments/EXP753-vertexid-hardware/FRONTEND_CONTRACT_INVENTORY.json`.

## EXP754 hardware result / current causal target
Package749 installed once and was fully removed. DWM selected exact UMD749 and
Apple AGX FL10_0. The EXP753 buffer boundary is fixed in hardware: dynamic
VB144/160000/240012, dynamic IB16000, CBs and a 50x50 BGRA RT|SRV cached visual
are created. DWM draw type16 and clear finish. Draw type17 then starts and the
runtime removes the device for UMD E_NOTIMPL. Dump exception0x889800C0 means
DWM failed to create a display swap chain in CreateLegacySwapChain, but is a
cascade after removal. No DWM-correlated AGX submission/completion/Present is
proven. Evidence: EXP754 causal-result.json, debug-dwm.log, etw-relevant.json.

Strongest causal target: current native graph admits one draw per batch while DWM
issues multiple draws before Windows Flush. Offline gate must make two consecutive
frontend draws finalize as two ordered batches through the existing adapter,
composer, physical KMD plans and retirement. No new allocator/composer.
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
       EXP753 exact hardware-device selection; exact748 cleanup/recovery.
[PASS] EXP754 contract-wide buffers, append cycle, StartVertex/BaseVertex,
       x64 execution and ARM64 closure/test build.
[PASS] Package749 run fixed DWM dynamic-buffer admission; exact cleanup/recovery.
[NOW] Multiple Draw calls before Windows Flush -> ordered existing native batches.
[NEXT] x64 executable + ARM64 package/sign/hash/preregister.
[HW] Verify DWM draw type17 reaches native submission/completion or next boundary.
POST-HARDWARE: optional features, performance, sustained desktop stability,
OpenGL and CS1.6 only after first proven DWM AGX execution/Present.
