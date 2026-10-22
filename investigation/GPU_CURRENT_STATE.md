# AGX continuation — READ FIRST — 2026-10-19 UTC

Worktree `/Volumes/pwdev/public_windows/.worktrees/integration-ad04-windows-compiler`,
branch `integration/ad04-windows-compiler`. Russian reports, autonomous continuous
loop; bounded polls and NO final during active experiments. Root alone owns
tracked edits/Git/ledgers, builder/guest/launcher. Preserve user dirty `mu`.
No attribution/push/PR/new chats/goals/automations. CS enabled. Read agx-d3d12-loop,
AGENTS.md/CLAUDE_WORKING_METHOD; only referenced ledger/evidence, no archaeology.
Existing c18 security-blocked UBFE/C21E task MUST NOT be retried/routed around.

## CURRENT 2026-10-22T05:59Z -- SM 6.0 + WAVE OPS VALIDATED ON ARM64 (SM6 PROFILE); TESS WINDING FIXED

Last validated package 30.0.1861.0 from 6f692bac (EXP1861, rec1861h PASS).
Proven on J313 (EXP1857-EXP1861), profile APPLE_AGX_D3D12_SM6=1:
- R1 build 2 (0012): CORE_0012, SHADER_MODELS {5.1, 6.0}, SHADER_CAPS_0012
  WaveOps, 32-lane waves, 896 lanes (7 cores x 128).
- The R1 runtime converts every DXBC shader to DXIL (dxilconv); every
  stage reaches agx_dxil_to_nir. Probes ARM64 103/103 (C44A compute, C44B
  graphics, C48A waves 0x3fff, C39A stream output, C37 tessellation, C41
  loops/UAV sizes); 17/19 samples as the default profile; MiniEngine
  ModelViewerI6 pixel-identical to the DXBC run (x64).
- Fixes behind it: dispatch base (30ed91c2), heap ranges, GS/HS/DS,
  pass-through HS, DXIL xfb, dxil-spirv two-output udiv patch (c49 libs,
  patches/), indexed tess factors + image placeholders (5e8aa33b), float
  control preserve flags (0993ebe3).
- Tessellation winding: D3D triangle_cw = GL ccw in both frontends
  (139a7509, C37D/C37E as WARP); the Asahi path reading the HS winding is
  not located.
Offline guards: test_dxil_to_nir (49 dxilconv probe shaders, Asahi-handler
check for every final intrinsic, image span, tess factors).
Open: x64 SM6 probe dies of heap corruption (0xc0000374) inside C38A's
CachedPSO step (ARM64 silent) -- next: run it under the guest's ARM64 cdb.
Then: SM6 by default (removes APPLE_AGX_D3D12_SM6 gate), SM 6.1+.

## Prior state superseded by EXP1857-EXP1861

### (was CURRENT) 2026-10-22 02:10Z -- RESOURCE BINDING TIER 2 VALIDATED; DXIL/SM6 ADMISSION IN PROGRESS

Last validated package 30.0.1856.0 from 0f7bf15a (EXP1856, rec1856h PASS).
Proven on J313 (EXP1853-EXP1856):
- Tier 2 (C47, D3D12-C47-binding-tier2.md): SRV/UAV/sampler/counter heap
  ranges through bindless handles (heap form at index*160+64, cb14 range
  table + sampler map, cb15 heap); probe C47B 16/16 words both ABIs;
  MiniEngine ModelViewerI runs its own tier-2 g_CommonRS unpatched.
- KMD: RunCompute EncoderParams carry the batch sampler heap (95d86d62).
- agx_batch_init_state before heap tracking (05fbd40c); descriptor heaps
  walked in a cached shadow, heap forms mirrored to the WC GPU buffer
  (c375b538): DynamicIndexing 1218/1205 presents ARM64/x64.
- x64 UMD is assertion-enabled: a "hang" of an x64 probe is usually an
  assert message box; probe tool dumps on timeout (comsvcs MiniDump).
Active (C44, D3D12-C44-dxil.md): agx_dxil_to_nir (dxil-spirv 8434 + libvtn)
in both 64-bit UMDs; SM 6 needs the R1 interface (0003 runtime never asks
SHADER_MODELS); R1 CORE_0010 + SM 6.0 offered only to APPLE_AGX_D3D12_SM6=1
processes. EXP1857: R1 device creation works, the runtime itself creates
SM 6 shaders; DXIL compute aborted on load_base_global_invocation_id (fixed
30ed91c2). Next: EXP1858 with the fix; then GS/HS/DS and heap ranges in the
DXIL path, wave ops (SHADER_CAPS_0012 / interface 0012), SM 6 by default.

## Prior state superseded by EXP1853-EXP1856

### (was CURRENT) 2026-10-21 20:55Z -- MINIENGINE SPONZA CORRECT AT 13 Hz; GPU-BOUND; DXIL NEXT

Accepted package 30.0.1852.0 from 5f0d0fdc (EXP1852, recovery rec1852h PASS).
Proven on J313 (EXP1839-EXP1852):
- Probes 97/97 on ARM64 and x64 (C41-C42N incl. R11G11B10 paths, gathers).
- MiniEngine ModelViewer (x64, FXC SM5.1, variants in .local/miniengine-*;
  guest copies C:\Users\pavel\EXP1838/1844-1848): correct colours since
  EXP1849 (MSVC-ABI signed enum bit-fields in the Asahi compiler/linker keys:
  AGX_GATHER_B read -3, GatherBlue = 0, TAA lost blue; fix a6798af8+).
- Speed: 4 Hz -> 11 Hz (EXP1850: NOT_AVAILABLE heaps unmapped, no per-frame
  64 MB light-grid download) -> 13 Hz (EXP1852: no memory_barrier per
  dispatch + written-allocation bound 128; EXP1851 crashed on the old bound
  of 16). Main list 9 submissions, replay ~11-12 ms; B now waits ~58 ms/frame
  on the GPU (shading cost), F (constant PS) 39 ms/frame.
- Receipts: measure-d3d12-execute {lists, cmds, replay us, submit us},
  measure-d3d12-owner-time, measure-d3d12-owner-fences {submissions, fence},
  reject-batch (winsys refusals, D3D12 owner hooks).
Next causal targets:
- DXIL / SM 6.x (investigation/analysis/D3D12-C44-dxil.md): dxil-spirv ->
  spirv_to_nir; download/licence of dxil-spirv awaits the user's permission.
- GPU cost of ModelViewer B shading (sample-rate PS, light-grid loops, DXBC
  translation quality); MiniEngine GPU timers read per merged batch.
- Steam/3DMark on the guest awaits the user's permission.

## Prior state superseded by EXP1839-EXP1852

### (was CURRENT) 2026-10-21 07:57Z -- FL11_0 PIPELINE COMPLETE; 17 MICROSOFT SAMPLES; MINIENGINE NEXT

Proven on J313 (EXP1831-EXP1836; package 30.0.1836.0 from 1e9ac471):
- Probes ARM64 and x64: 81 PASS each. New since 04:00Z: C32A 4x MSAA
  resolve, C35A depth-only PSO, C36A placed 2D textures (own heap object),
  C37A-C tessellation (HS/DS; hull/domain control points link by register,
  dxbc_to_nir hs_cp_slot), C38A cached PSOs (pfnSerializeObject: hObject =
  driver PSO pointer; CachedPSO -> pfnCreateDeserializedObject), C39A stream
  output (runtime passes no program for a GS-less SO pipeline; capture from
  the VS; filled size round-tripped through Asahi's target offset buffer).
- Samples (68907e31), 20 s each, both ABIs: the 11 EXP1825 samples plus
  SmallResources, Fullscreen, HDR (D2D/DWrite through D3D11On12),
  DepthBoundsTest, Residency, PipelineStateCache (after f26cb83b: unset
  SM5.0 root arguments do not refuse). ReservedResources shows its
  tiled-unsupported message; HeterogeneousMultiadapter fails (expected).
- Timing (EXP1836 measure-d3d12-execute): Multithreading ~2050 draws/frame,
  replay ~64% of the frame (ARM64 7.9 + 13.8 ms, submit 6.3 + 6.3 ms).
Running: EXP1837 replay overhead (scratch zeroing, framebuffer cache) +
measure-d3d12-owner-time (bind/draw_vbo/unbind/flush/retire split).
Next causal targets:
- MiniEngine ModelViewer (x64 FXC SM5.1 build, agent-built on the builder):
  CreateRootSignature E_INVALIDARG in the runtime -- g_CommonRS declares
  u0-u9, tier 1 at FL 11_0 allows 8 UAV registers. Honest Tier 2 needs
  heap-based bindless (SRV tables as large as the heap, 64 UAVs); do NOT
  advertise Tier 2 before that (AGENTS capability rule). Test-only
  adaptation: variant A (UAV range 8), B (+RGBA8 swapchain).
- 10-bit (R10G10B10A2) swapchains: the presentation path is BGRA8/RGBA8
  only (admission UMD, kernel allocation, Asahi linear import).
- Async submission (submit+wait is ~36% of the frame).

## CURRENT 2026-10-21 04:00Z -- 10 OF 11 MICROSOFT D3D12 SAMPLES ON ARM64 + x64

Proven on J313 (EXP1825-EXP1829; package 30.0.1829.0 from 6fb17957):
- Probes ARM64 and x64: 73 PASS / C21E SKIP each (C29A BC1-BC7 textures,
  C30A bundles, C31A root-argument ExecuteIndirect, C33A UAV counters, C34A
  SM5.1 ranges with dynamic indices).
- Microsoft samples (68907e31) run 20 s and close cleanly on both ABIs:
  HelloWindow/Triangle/Texture/ConstBuffers/FrameBuffering/Bundles,
  Bundles (BC1 city, 60 fps), DynamicIndexing (120 materials, ARM64 ~49 fps,
  x64 ~23 fps), ExecuteIndirect (GPU culling + counters), PredicationQueries.
- Device-initialisation rule (EXP1827/1828, cdb on D3D12Core): per typeless
  cast set, MULTISAMPLE_RENDERTARGET and 2x/4x levels must agree, and
  members with MultisampleRenderTargetSupport -1 (typeless, depth view
  formats, BC, XR_BIAS) report neither -- test_d3d12_multisample_caps.
Tooling: probe "warp STEP..." runs new steps on WARP with the debug layer
on the builder (catches probe API misuse before hardware); cdb on the guest
(C:\Users\pavel\dbg, -cf command files) for app crashes and runtime throws.
Pending (EXP1830): C32A 4x MSAA after 44951eb0 (MS targets were taken by
the single-sampled presentation path); Multithreading after d46292c1
(empty FS for PS-less PSOs; Asahi read a NULL FS).
Open: owner submits synchronously on its own context; per-draw sampler-view
creation (121 views per DynamicIndexing draw); x86 D3D12 UMD; MS depth SRVs
(view formats report no levels).

## CURRENT 2026-10-21 01:05Z -- D3D12 ARM64 + x64 RUN A REAL APP (nBody); CLAUDE AGAIN

Codex delegation ended 2026-10-21 (user stopped it); Claude drives the bench
with the scratchpad prepare.sh/run-probe.sh flow (EXP numbers = hardware runs).
Build recipe: builder run182x.ps1 (package_source.py snapshot -> ARM64 native
runtime from that source -> build-driver.ps1 WITHOUT the prepared NIR owner
(classic owner compute on both ABIs; fe68ed22 links it) -> x64 native runtime +
build-umd-x64.ps1 -> umd/forwarder ARM64X -> INF variant -> repackage-driver.ps1
-X64Umd -Arm64xForwarder). Package = forwarder AppleAgxRenderAdmissionUmd.dll +
UmdArm64 + UmdX64 in System32 (020285b4); cleanup removes all three.

Proven on J313 (EXP1820-EXP1824, package 30.0.1824.0 from 463cf860):
- ARM64 probe 68 PASS / C21E SKIP and the same probe built for x64: 68 PASS.
- C27 geometry shaders (C27A/B), C28 groupshared barrier loops + nBody CS.
- Microsoft D3D12nBodyGravity (async compute thread + GS particles) runs
  60 s on ARM64 (~49 fps, 7625 dispatches) and x64 (~45 fps), particles
  move, WM_CLOSE exits cleanly. D3D11 x86 smoke + ARM64 GS gsa/soa PASS.
Fixed causes: GS fail-closed (c6195e13); DXBC ubfe/ibfe/bfi -> shifts/masks
(c076be8f, agx_compile "Unhandled ALU op"); IOCoherent TRUE (5b7f6abe, x64
D3D12Core CacheHeapCaps); owner recursive lock + ExecuteCommandLists critical
section (463cf860, two threads raced one pipe_context -> compute never retired).
Test harness: nbody-run ConvertTo-Json of Get-Content strings grew PowerShell
to GBs (fixed; explained every missing result*.json since EXP1795).
Open: owner submits on its own KMD context, not the queue's, so cross-queue
GPU waits do not order owner work (ordering rests on synchronous replay);
EXP1820 whole-machine reset unexplained (removal/kill/teardown ruled out);
DWM dwmcore 0xc00001ad once ~1 min after some full-owner boots (pre-existing);
Night Raid DLC vanished from 3DMark (download interrupted by reboots) -- ask
the user to reinstall; next real apps: 3DMark GUI (x64 D3D11), Night Raid
(ARM64 D3D12), Portal 2 (x86 D3D9On12 -> needs a WOW64 D3D12 UMD).

## Prior state superseded by the EXP1824 nBody result

## CURRENT 2026-10-20 22:19Z — X64 UMD OLD CLOSURE REJECTED; FRESH SOURCE NEXT

EXP1808 old configured x64 Mesa graph built seven missing archives, diagnostic
only. EXP1809 verified all 729 exact c498 driver files and tried x64 UMD
compile against that old x64 Mesa/runtime closure. First C boundary failed:
`umd_win32_screen.c:1281` needs `AGX_WIN32_SCREEN_OPERATIONS.GpuvaOps`, but
old x64 dependency header lacks it; exact c498 header defines it. No x64
DLL produced. Stop old-source comparisons after this complete offline pass;
no include-order shim or old native_runtime link. Next build one fresh x64
Mesa/NIR/Asahi closure from the EXP1466/1760 pinned source snapshot and
generated headers, then retry UMD compile. ARM64X dual-loader contract from
EXP1805 remains proven; J313 normal Code28/PackageAbsent/8CPU, no GPU
package. Night Raid/real-game acceptance still open.

## Prior state superseded by EXP1809 source-ABI verdict

## CURRENT 2026-10-20 22:05Z — ARM64X DUAL LOADER J313 PASS; X64 UMD NEXT

EXP1805 normal Code28/PackageAbsent J313 loaded the same EXP1803 ARM64X
forwarder from x64 and ARM64 processes. x64 caller loaded only x64 marker
backing, returned0x18006422; ARM64 caller loaded only ARM64 backing,
returned0x18006412. Both exited0/`FORWARDER_ABI_PASS`, stderr empty; post
Code28/Staged0/8CPU PASS. This proves the one-name dual-ABI loader path,
using no-GPU marker DLLs. The forwarder is not a UMD yet.

Next build actual x64 UMD from the exact qualified driver source and one
x64 Mesa/NIR/Asahi closure. Current builder has configured `nir-x64` but
its libgallium archive is missing; EXP1466 contains pinned current Mesa
source overlay and ARM64 core archive identity. Audit source/build closure
before compiling or linking, then test exports/PE/stack offline. After
that combine native UMD, x64 UMD and ARM64X forwarder into a newly signed
exact package, verify ARM64 and x64 D3D12 on J313, run regressions, recover.
Normal GPU-visible guest remains Code28/PackageAbsent/8CPU; real-game
acceptance and Night Raid still open. Preserve user-dirty Mu.

## Prior state superseded by EXP1805 loader verdict

## CURRENT 2026-10-20 21:59Z — ARM64X FORWARDER BUILT; LOADER TEST NEXT

EXP1803 builder-only pure forwarder: 13824B ARM64X PE SHA
a1d08ff60b16c0b2d233130551ba338871ba266522ac1ed0c13985b79c865ba2,
no import directory, native OpenAdapter10_2/OpenAdapter12 forwarded to an
ARM64 backing name. Official pinned VS Arm64 environment resolved the CRT
link boundary; EXP1801/1802 stopped on missing CRT or EC helpers and remain
recorded. x64 forwarder view and Windows loader behavior are **not yet
proven**. Next build architecture-distinct no-GPU backing stubs and x64/
ARM64 LoadLibrary callers, then test them on normal Code28 guest without
AppleAgx package. Actual x64 UMD closure/package build remains open.
Normal J313 remains Code28/PackageAbsent/8CPU after EXP1800 recovery.

## Prior state superseded by EXP1803 builder result

## CURRENT 2026-10-20 21:46Z — EXP1800 X64 UMD NAME MAPPING; RECOVERY_DONE

Exact signed package30.0.1764.0/oem5.inf full-owner Code0/8CPU. Same-source
x64 and ARM64 KMT probes produced byte-identical UMD-name outputs: both see
AGX LUID low0x4952f and `AppleAgxRenderAdmissionUmd.dll` for DX9/10/11/12.
That named DLL is ordinary ARM64 PE0xAA64; only other UMD is x86 PE0x014c.
The package supplies no x64/Arm64EC-compatible UMD. This rules out hidden
AGX adapter or alternate x64 name as causes of EXP1795's x64
`D3D12CreateDevice` DXGI_ERROR_UNSUPPORTED. Official Microsoft UMD/Arm64EC
ABI makes the UMD/package mismatch the strongest owner-layer cause; direct
loader error or successful compatible UMD still needed for final proof.
Focused D3D12 probe 60 PASS/known C21E SKIP, DWM fault log empty; no fresh
GL/corpus/CS. FactoryV2 exact recovery RECOVERY_DONE normal visible
Code28/PackageAbsent/noService/8CPU. Evidence/root verdict under
`.local/experiments/EXP1800-kmt-abi-pair`.

Next design/build x64 UMD from current x64 Mesa closure plus an ARM64X
forwarder or merged UMD that serves the one registered name to native ARM64
and x64 callers. Microsoft documents `BuildAsX` and a pure forwarder. Inspect
exports and linker/INF/source closures offline, test PE/imports and loader
before any new signed package. KMD/firmware unchanged. Night Raid native-WoA
DX12 FL11_0 benchmark remains separate, and first-release game proof open.

## Prior state superseded by EXP1800 mapping and recovery

## CURRENT 2026-10-20 21:32Z — X64 CODE28 KMT CONTROL PASS; PACKAGE QUERY NEXT

EXP1798 on normal J313 Code28/PackageAbsent/8CPU ran exact EXP1797 x64
KMT UMD-name query: pointer8, EnumAdapters2 two baseline WARP adapters,
DX9/10/11/12 names `d3d10warp.dll`, AGX_KMT_UMDNAME DONE, exit0/stderr empty.
Pre/post package absent gates PASS, no reboot. Legacy `IsWow64Process`
printed0 and is not used as architecture proof; EXE PE is x64. Next one
exact-package run queries UMD names from x64 and native ARM64 callers,
then full FactoryV2 rollback. Current guest still Code28/PackageAbsent/8CPU.

## Prior state superseded by EXP1798 control

## CURRENT 2026-10-20 21:29Z — X64 UMD-NAME QUERY BUILT; CODE28 CONTROL NEXT

EXP1797 rebuilt the existing D3DKMT UMD-name diagnostic source as x64 PE
0x8664, EXE SHA96b038bd121950fffa825be5db02a1f3532bd898ac4f069a048708fcb3fe627f,
builder rc0. EXP1796 stopped before link on PowerShell native stderr policy;
four unchanged printf-format warnings retained in EXP1797. No J313 action.
Next use this exact probe on normal Code28 guest, then under one exact signed
package to measure x64 DXGI/KMT adapter and UMD-name selection before
deciding how to supply an x64-compatible UMD. EXP1795 recovery remains
RECOVERY_DONE Code28/PackageAbsent/8CPU; no package installed now.

## Prior state superseded by EXP1797 diagnostic build acceptance

## CURRENT 2026-10-20 21:24Z — EXP1795 FIRST HRESULT PROVED; RECOVERY_DONE

Exact EXP1764 package/full-owner Code0/8CPU plus EXP1794 x64 nBody fail-
path receipt: first failure `D3D12CreateDevice(adapter, FL11_0)` at
`D3D12nBodyGravity.cpp:109`, HRESULT 0x887A0004 DXGI_ERROR_UNSUPPORTED.
WER again ucrtbase 0xc0000409/data7, no AppleAgx UMD listed at crash;
no app frame/UMD trace. App runner did not write result JSON after failure;
failure log and WER are durable. Same-package D3D12 probe 60 PASS/known
C21E SKIP, all 61 statuses equal EXP1765, DWM fault log empty. FactoryV2
exact rollback RECOVERY_DONE Code28/PackageAbsent/noService/8CPU; normal
GPU-visible guest current. Evidence/root verdict under
`.local/experiments/EXP1795-nbody-hr`.

Strongest current hypothesis: x64 app cannot use current UMD package, which
contains AA64 native and x86 WOW PE DLLs but no x64/Arm64EC UMD. Microsoft
documents x64/Arm64EC processes cannot load ordinary Arm64 DLLs and Direct3D
runtime loads UMD as DLL. Verify exact INF/runtime selection and x86 build
closure before UMD/package owner fix; no KMD/firmware change. Native-WoA
free Night Raid DX12 FL11_0 benchmark is independent, but real-game release
criterion remains open. Preserve user-dirty Mu and security-blocked tasks.

## Prior state superseded by EXP1795 verdict and recovery

## CURRENT 2026-10-20 21:07Z — EXP1794 HRESULT SAMPLE BUILT; GPU CHECK NEXT

EXP1794 external Microsoft nBody x64 SM5 diagnostic EXE SHA
50ccd855939668c48d5fb9f92c1bbc72f864455aac91f19126ad25d4ee67fc85,
PE x64, pinned source/project/dependencies, MSBuild0/0 warnings/errors. Only
test-only DXSampleHelper fail-path receipt changed; HLSL and app-local DLLs
equal EXP1791. On failure it writes HRESULT/file/line to an environment-
selected path, then throws the same HrException. J313/GPU not touched by build.
Next use exact artifact to discriminate first failed D3D12 call in a new
preregistered J313 run, then consider driver vs sample/runtime ownership.
Normal guest remains Code28/PackageAbsent/8CPU after EXP1793 recovery.

## Prior state superseded by EXP1794 builder acceptance

## CURRENT 2026-10-20 21:02Z — EXP1793 APP EARLY CRASH; RECOVERY_DONE

Exact EXP1764/1765 package30.0.1764.0/oem5.inf, same source c4980c7e,
full-owner Code0/8CPU. Microsoft nBody SM5 x64 PID10164 exited before first
5s sample with 0xc0000409, WER ucrtbase.dll/data7. WER loaded dxgi/d3d12/
app-local D3D12Core, no AppleAgx UMD at crash; no app UMD trace or screenshot.
This does not identify the failed HRESULT or establish whether UMD was loaded
earlier. Same-package D3D12 probe 60 PASS/known C21E SKIP, all 61 named
statuses equal EXP1765, DWM fault log empty; no fresh GL/corpus/CS claim.
FactoryV2 exact recovery RECOVERY_DONE Code28/PackageAbsent/noService/8CPU,
normal GPU-visible guest current. Evidence/root verdict in
`.local/experiments/EXP1793-nbody-gpu`. Next minimal discriminator: test-only
instrumented external sample records selected DXGI adapter and first failing
HRESULT/call without modifying driver. Free 3DMark Night Raid is an additional
native DX12 FL11_0 Windows-on-Arm benchmark candidate; Portal 2 is DX9 and
only an indirect D3D9On12 check. First release remains SM5.1 plus real games.
Preserve user-dirty Mu and security-blocked C21E/UBFE/C27 unchanged.

## Prior state superseded by EXP1793 verdict and recovery

## CURRENT 2026-10-20 20:32Z — SM5.x SAMPLE J313 RUNTIME PASS; GPU CHECK NEXT

EXP1792 CPU-only normal Code28/PackageAbsent J313: exact x64 Microsoft nBody
SM5 EXE PID4404 alive for six 5s samples, window titled D3D12 n-Body Gravity
Simulation, loaded dxgi/d3dcompiler_47/d3d12/app-local D3D12Core, no WER or
runner error. Owned PID stopped and task removed. Pre/post Code28/Staged0/
noDriverInf/8CPU, SSH alive; no AppleAgx module or GPU package. This proves
x64/Agility startup only, not adapter selection or rendering. Next one
EXP1764-qualified exact-package GPU run with module/UMD/KMD/frame receipts
and FactoryV2 recovery. Sample source skips software-flagged adapters but can
retain last pointer; measure selected adapter. Real game acceptance still open.

## Prior state superseded by EXP1792 runtime result

## CURRENT 2026-10-20 20:27Z — SM5.x SAMPLE BUILT; J313 GAME-LIKE CHECKPOINT NEXT

EXP1789 builder produced external Microsoft nBody SM5 x64 EXE SHA
819d205641a1c0c6adbdaffea972cd0e1ff166d694a00dbdb80561f64e8d1df3.
EXP1790 source21/project+package90 hashes passed before PE dump but its
final audit consumed 10.697GB and was stopped as exact PID40520, no receipt.
EXP1791 bounded output audit PASS: six named app/runtime files hashed;
HLSL equals source commit68907e31 and WinPix/Agility DLLs equal pinned
NuGet. Build log says zero warnings/errors; PE machine x64, D3DCompileFromFile,
dxgi and delay-loaded d3d12. This is a game-like diagnostic, **not a real
game or release proof**. Next prepare one J313 checkpoint with exact
EXP1764/1765-qualified package, app-local runtime/SM5 shaders, module/UMD
receipts, regressions and FactoryV2 recovery. No GPU package installed now;
normal guest Code28/PackageAbsent/8CPU. Portal 2 Steam route stays stopped
pending new account evidence. User-dirty Mu and security-blocked C21E/UBFE/C27
unchanged.

## Prior state superseded by EXP1791 builder acceptance

## CURRENT 2026-10-20 19:40Z — SM5.1 GAME ROUTE EXP1780 PRELAUNCH STOP; RECOVERY_DONE

User first-release target is stable D3D12 SM5.1 plus games; DXIL/SM6 deferred.
EXP1776/1777 inventory found no installed native D3D12 game. Portal 2,
DX9 renderer and intro map plus OS x86 D3D9On12/D3D12Core were pinned in
EXP1778/1779. EXP1780 reused exact EXP1765-qualified package1764 and booted
Code0, but limited interactive task called admin-only Get-WindowsDriver and
stopped before Steam/game (PortalPid null, no D3D12 route verdict). Root verdict
`.local/experiments/EXP1780-portal2-d3d9on12/root-verdict1780.json`
SHA6f6fd5901b9c2bed11a69539b57bb4bf6b3c991fd051b0724c629fd5add5a1e8.
EXP1781 exact FactoryV2 rollback RECOVERY_DONE Code28/PackageAbsent/8CPU;
new host proxy helper proved two stable transitions. No package/active game run.
EXP1782 fixed the privilege gate and created Portal2 PID6004, but the process
exited before its first15s sample without WER/game log/UMD trace. Exact package
recovered RECOVERY_DONE Code28/PackageAbsent/8CPU. EXP1783 bounded Steam log
read showed no tracked AppID620, and EXP1784 Valve -applaunch620 on clean guest
produced zero Portal2 processes in90s. Stop this route until Steam/account state
changes. Next: pinned external D3D12 game-like engine workload for SM5.1 driver
progress; do not call release ready without a real playable game.
Preserve user-dirty Mu; C21E/UBFE/C27 remain security-blocked.

## Prior state superseded by EXP1780/1781 game and recovery verdict

## CURRENT 2026-10-20 18:57Z — EXP1765 OWNER ADMISSION J313 PASS; RECOVERY_DONE

Last GPU-qualified D3D12 source is isolated c4980c7e6d2ced728a89a78e6e98f5ae3e789c4d,
exactly EXP1681 a6d871ad plus one owner DDI file. EXP1764 signed package
30.0.1764.0 root-reviewed; EXP1765 J313 full-owner run: 63/63 unique non-E
D3D12 PASS, 43 owner receipts each before its successful CreateDevice receipt,
no duplicate; D3D11/FL11/pool/corpus/DWM regressions baseline-equivalent.
CS46.627fps, GL53.1fps, internal59.6fps; only known MSAA-a000r FAIL.
Root verdict `.local/experiments/EXP1765-owner-admission/root-verdict1765.json`
SHA9dbadf345dadeac3b9720f68920622c8f68cc92c3ac5d818757f5801087a7882.

Visible recovery initially stopped fail-closed on two separate stale SYS paths;
EXP1766 fixed exact DriverStore/ImagePath disable helper, EXP1767 fixed exact
remover, EXP1768 deleted oem5.inf, and EXP1769 durable preflight PASS plus
RECOVERY_DONE Code28/PackageAbsent/noService/8CPU. Normal GPU-visible inert
J313 is current, no AppleAgx package staged/installed, no active GPU experiment.
Preserve user-dirty Mu and security-blocked C21E/UBFE/C27 untouched. Main branch
includes additional unqualified C25/C26/C27/R1 edits beyond isolated c498;
EXP1765 validates owner publication only in the a6-compatible source.
USER READINESS DECISION 2026-10-20: first release targets stable D3D12 SM5.1
with games; DXIL/SM6 is deferred. EXP1776/1777 found no installed native D3D12
game, only Counter-Strike/Half-Life and Portal 2 plus Solitaire. EXP1778
confirmed Portal 2 EXE and OS x86 D3D9On12/D3D12Core present, but routing to
our D3D12 UMD is unproven. Next target is a Portal 2 D3D9On12 game-path
checkpoint with exact EXP1764 package bytes, full rollback and module/UMD receipts.

C15 CORE_0010/SM6 remains fail-closed. EXP1770/1771 added unlinked CPU-only
pipeline-library envelope/table structural gates and digest/identity predicate;
EXP1774 proved the unlinked Windows CNG SHA-256 callback on J313 CPU with
Code28/PackageAbsent/8CPU unchanged. These are not a full importer. Next causal
target: component/recipe graph and transactional owned refs plus typed R1
shader error/lifetime and compiler contracts offline before a new GPU package. One broader old rejected-device-error host fixture
still lacks provider/R1 aliases; no full-suite GREEN claim.

## Prior state superseded by EXP1765 hardware verdict and EXP1769 recovery

## CURRENT 2026-10-20 17:17Z — EAGER OWNER ADMISSION HOST/WDK PASS; GPU CHECK NEXT

EXP1753 current DDI owner admission candidate: native owner constructed once
inside successful CreateDevice before runtime free-threaded resource creation;
Interlocked cached publication/failure and in-flight guard reject synchronous
constructor reentry without another owner. Actual-source host seven controls
ASan/UBSan PASS; pinned WDK NativeOwner/None/winsys3rc0 for exact DDI
SHA11f1fc2fd786175539deba234a52283371a100e297a17e08baa6bf23faa104dd.
Offline receiptSHA7bf355bfe8086a34442bc1e453dae80c2e63e136f8515d7dca3d2febbc733d65.
One broader pre-existing rejected-device-error fixture does not compile with
newer provider/R1 types; no full-suite GREEN claim. No GPU package installed
or active experiment yet. Next prepare one exact signed package, preregister
WHY/ATOMIC/windows-AGX translation and run a J313 owner timing discriminator
with full regressions and FactoryV2 recovery. Last GPU-qualified EXP1681.
Preserve user-dirty Mu, security-blocked C21E/UBFE/C27 untouched. C15
CORE_0010/SM6 remains fail-closed.

## Prior state superseded by owner admission candidate

## CURRENT 2026-10-20 10:12Z — C15 WINDOWS PERSISTENT BACKING PASS; DISTINCT GATE NEXT

EXP1749 fresh test-only ARM64 `/MD` persistent64KiB backing EXE root-reviewed;
EXP1750 normal J313 CPU-only N1..4 one/N1..5 mixed nine children ALL PASS.
Each one targeted failure, attempts exactly N while hook stayed armed through
failed job, status3/bytezero result, next SAME-thread owned980/2152 keys1/5,
exit0/stderr empty; SSH up. ReceiptSHA2869710b4bc1881cc8ca9347556299f1e4e6824137643a935f061c984eb5a10a,
reviewSHAbd368d540120d587270b2a20d48bdb133b6f498ccb6bc5890b2cb8d88597d3cd.
This covers only observed custom backing requests/two inputs, not universal
OOM, unseen shapes, typed CORE_0010 or GPU. Return to distinct C15 contract
gate. Last GPU-qualified source EXP1681, no AppleAgx package/active GPU run;
owner publication race EXP1745 still awaits callback/lifetime policy.
EXP1751 restored the current actual CreateDevice poisoned-storage fixture;
EXP1752 added a bounded durable expected failure plus separate outcome-shape
check for that race.

## Prior state superseded by persistent Windows backing check

## CURRENT 2026-10-20 10:07Z — C15 WINDOWS BACKING NTH PASS; PERSISTENT CHECK NEXT

EXP1746 test-only Windows ARM64 `/MD` custom64KiB backing hook build AA64
root-reviewed. EXP1747 normal J313 CPU-only baseline measured one4/mixed5
matching allocations (different from host one3/mixed5), outputs owned980/2152.
EXP1748 all Windows N1..4/N1..5 one-shot failures PASS: each status3,
bytezero failed result, next SAME-thread status0 valid, exit0/stderr empty;
guest SSH stayed up. ReceiptSHA8b1b05be5c185a1650e17b34f68b085c28bb9ff8050a46a0dcf611dbe19dfe38,
reviewSHAb972f4c49eba38f5f97d5dfeeb0cd82438c099f6b8143f4ce79092be25d7bbc6.
Details/limits `investigation/analysis/D3D12-C15-windows-backing-oom.md`.
Next one persistent matching64KiB failure cleanup check, then distinct C15
gate. Last GPU-qualified EXP1681; no AppleAgx package/active GPU experiment.
EXP1745 owner double-publication defect remains confirmed but fix awaits
callback reentry/lifetime contract. CORE_0010/SM6 stays fail-closed.

## Prior state superseded by Windows backing fault sweep

## CURRENT 2026-10-20 07:52Z — C15 OWNER PUBLICATION RACE RED; CALLBACK CONTRACT NEXT

EXP1745 extracted current actual Device/CreateDevice/EnsureOwner under
ASan/UBSan. Coordinated A-before-B publication produced two owner
constructions, distinct returned owners and `device.owner==B` (RED rc1);
success/failure/invalid baseline controls rc0. Raw resultSHA57024cdff471b7bd892f46a38b0bf9391831435a26bef4bc1b534b0eb45ba5dd.
Confirmed UMD source-level duplicate/overwrite defect; J313 incidence
unproven. Current heap registry already locked; old owner-threading note
corrected. Owner construction makes outward adapter/kernel callbacks;
same-device callback reentry/wait guarantees remain unresolved, so no
blocking publication lock or package/hardware run yet. Next establish
callback/reentry and lifetime policy, then tracked regression and owner-layer
fix. Last GPU-qualified EXP1681; GPU package absent; CORE_0010/SM6 fail-closed.

## Prior state superseded by owner publication counterexample

## CURRENT 2026-10-20 07:46Z — C15 INTERNAL R1 PREFLIGHT GREEN; TYPED RUNTIME GATE NEXT

EXP1744 current-source deterministic RED on missing combined Standard
PROGRAM/IO preflight; GREEN actual provider host ASan/UBSan96 checks, opt-in
65 pinned inputs and pinned WDK builder DDI NativeOwner/None + winsys3rc0.
Internal caller-trusted helper rejects malformed declared PROGRAM/IO before
provider lease/sink and leaves output clear; valid actual DXIL copies owned.
It is uncalled from CORE_0010; no DDI/caps/GPU package/reboot change. Exact
`investigation/analysis/D3D12-C15-r1-shader-error-boundary.md` updated.
Still open: full PROGRAM body readability, Calc/Create pointer lifetime,
exact CreateShader_0010 permitted callback error set, owner/GPU integration.
Next source-first typed runtime gate, not GPU.

## Prior state superseded by internal R1 preflight checkpoint

## CURRENT 2026-10-20 07:38Z — C15 WINDOWS /MD OBSERVED NEW CATEGORY PASS; DISTINCT GATE NEXT

EXP1742 fresh ARM64 `/MD` test-only Nth-fault EXE AA64/root-reviewed;
EXP1743 J313 normal guest finite N1..14 one-key/N1..15 mixed 29 separate
children ALL PASS: each one targeted ordinary-new failure, status3 and
bytezero public result, next SAME-thread status0 owned980/2152 keys1/5,
exit0/stderr empty; SSH stayed up. ReceiptSHA143505aa6c764786d079a74f7a58f4d9a92d4ba08fca28687711014c0aa3ff94,
reviewSHAfea3ee35a93dfe48577db8e077d31654fbf03353263152c96bd37f9dea440487.
Only two inputs and observed ordinary-new category; unseen allocations,
external custom backing on Windows, new[], broad OOM, typed CORE_0010/SM6,
owner/GPU remain open. Last GPU source still EXP1681; no GPU package or
experiment active. Next distinct C15 gate: typed shader error routing and
full input-readability/lifetime contract. Details in
`investigation/analysis/D3D12-C15-windows-new-fault.md`.

## Prior state superseded by finite Windows observed-category sweep

## CURRENT 2026-10-20 07:31Z — C15 WINDOWS /MD FIRST NEW FAULT PASS; NTH SWEEP NEXT

Last GPU hardware-qualified source remains EXP1681 a6d871ad: 63 unique
non-E D3D12 PASS, exact FactoryV2 normal visible Code28/PackageAbsent
recovery. No GPU package staged/installed and no GPU experiment active.
Preserve user-dirty Mu; security-blocked C21E/UBFE/C27 untouched. Branch
Standard IO and internal shader transaction helpers remain uncalled;
CORE_0010/SM6 fail-closed.

Pinned external dxil-spirv composite + current owned converter Windows ARM64
`/MD` test-only checkpoint: EXP1734 retained exact native inputs audit;
EXP1735/36 AA64 EXE/root review; EXP1737 runner-only incomplete; EXP1738/39
two nominal fixture jobs 980/2152B keys1/5 with14/15 observed scalar-new
events; EXP1740/41 fresh fault EXE and J313 CPU-only first observed
ordinary-new failure on both fixtures status3, bytezero failed result, next
SAME-thread valid, exit0/stderr empty. J313 SSH remained up, no driver/package
or reboot mutation. Exact receiptSHA2811f430166d42fb73ec70aef9aa52b05ec5241b292aa827d98a9ed92df1bab6.
Details/limits: `investigation/analysis/D3D12-C15-windows-new-fault.md`.
This does not prove broad OOM or GPU/SM6 admission. Next causal target is a
finite Windows Nth observed-call sweep, then a distinct C15 integration gate.

## Prior state superseded by Windows /MD first-site checkpoint

## CURRENT 2026-10-20 07:08Z — C15 TWO HOST OOM CATEGORIES SWEPT; WINDOWS GATE NEXT

Last J313 hardware-qualified source is EXP1681 a6d871ad old0003
Device/Root provider: 63 unique non-E D3D12 PASS, exact FactoryV2
recovery Code28/PackageAbsent/noService/8CPU. No new GPU package staged,
no active hardware experiment. User-dirty Mu preserved. Branch uncalled
Standard IO span and shader transaction helpers EXP1683/1686 host/WDK PASS;
CORE_0010/SM6 still fail-closed.

External pinned dxil-spirv8434 original source and project production code
unchanged. Reviewed composite+profile clone with TEST-ONLY hook: EXP1711/12
nine archives84 objects/103 C++ -fexceptions; EXP1713/15 current project
checked+owned one/mixed/profile nominal12rc0,697 actual deps UNKNOWN0,
mixed output exact. EXP1716 first64KiB backing fault status3/empty result,
next same-thread valid. EXP1719/20 one-shot N1..3 one-key/N1..5 mixed custom
backing failures all PASS; EXP1728/29 persistent matching64KiB failure all
PASS. EXP1721/22 proved host ordinary new/new[] interposition at9/10 calls
with selected compiler/LLVMBC/ChainAllocator metadata PCs; EXP1723/24
one-shot N1..9/N1..10 all PASS. EXP1731 first persistent new PASS;
EXP1732/33 per-child calibrated persistent N1..9/N1..10 all19 status3,
bytezero result, no unmatched calls, next same-thread valid, rc0/stderr
empty. Category limits and exact artifacts in
`investigation/analysis/D3D12-C15-backing-sweep.md`,
`D3D12-C15-persistent-backing-sweep.md` and
`D3D12-C15-ordinary-new-sweep.md`.

This is NOT universal recoverable OOM: unobserved source sites, huge/noTLS,
other shader shapes, full-graph ASan/UBSan, Windows ARM64 /MD interposition
and EH behavior, exact VOID shader error mapping, signed CORE_0010/caps and
GPU execution remain open. Next causal target is a pinned Windows ARM64
CPU-only TEST-OVERLAY build/runtime check for one targeted backing failure
and ordinary-new symbol resolution, then broader reviewed fault coverage.
Do not run J313 GPU merely to test an unlinked CPU dependency. Security-
blocked C21E/UBFE/C27 untouched.

## Prior state superseded by host allocation-category sweeps

## CURRENT 2026-10-20 06:37Z — C15 NARROW PARSE OOM PASS; BROAD OOM STILL OPEN

Last J313 hardware-qualified source remains EXP1681 a6d871ad old0003
Device/Root provider lifecycle: D3D12 63 unique non-E PASS and exact
FactoryV2 visible recovery Code28/PackageAbsent/noService/8CPU. No active
GPU experiment or AppleAgx package staged/installed. Preserve user-dirty Mu.
Branch-only uncalled Standard IO span and internal shader transaction helpers
EXP1683/1686 passed host/WDK; CORE_0010/SM6 still fail-closed.

External pinned dxil-spirv8434 scratch (original upstream/repo sources
unchanged): EXP1687/1689/1691/1692 isolated owner sites RED/GREEN, EXP1690
Function early-registration RED, EXP1693 actual glslang TU rc0. EXP1694/95
four-site exception-enabled graph compiled nine archives/83 objects with102
C++ `-fexceptions` commands; EXP1699 normal 980-byte SPIR-V exact baseline.
EXP1701/02 added allocator-A/test hook (nine archives/84 objects/103 flags);
EXP1704 actual first64KiB backing failure escaped current C API as
`std::bad_alloc` (hook1/blobNULL). EXP1705/06 added candidate LLVMContext
and parse_dxil_blob RAII, same 84-object graph; EXP1707 original narrow
`noexcept` C++ parse-only facade returned internal OOM -1001/blobNULL on
hook1, then parsed valid input on the SAME thread after reset/end (rc0,
caller ASan/UBSan stderr empty). All artifacts/limits in
`investigation/analysis/D3D12-C15-narrow-parse-oom-facade.md`.

This is NOT general recoverable converter OOM or Windows ARM64 evidence.
Next causal target is one reviewed facade for the exact project-used C API
subset, fail-at-N across parse/scan/create/setters/run/output, partial
ownership/destructor proof and all selected Windows /EHsc TUs. No hardware
run can distinguish an unlinked CPU dependency. The exact VOID CreateShader
error category remains open. Security-blocked C21E/UBFE/C27 untouched.

## Prior state superseded by narrow parse fault checkpoint

## CURRENT 2026-10-20 06:19Z — C15 CPU INPUT GATES + EXTERNAL OOM SITES; EXP1681 RECOVERY_DONE

EXP1681 remains the last hardware-qualified source: a6d871ad old0003
Device/Root provider lifecycle, D3D12 63 unique non-E PASS and exact
FactoryV2 visible recovery Code28/PackageAbsent/noService/8CPU. No active
hardware experiment, no staged AppleAgx package. Preserve user-dirty Mu.

Branch CPU-only C15 after that run: EXP1682 WDK include-order STOP, EXP1683
Standard IO pointer/count span host guard-page11 PASS/WDK3rc0; EXP1684
one-use operation-token STOP, EXP1685 actual provider/owned-core DXIL+legacy
transaction host90 PASS, EXP1686 WDK3rc0. These headers are uncalled in the
accepted source. Typed CORE_0010/SM6 table remains fail-closed; exact VOID
CreateShader error category and full PROGRAM/IO readability remain open.

Pinned dxil-spirv8434 external OOM site checkpoints, no production patch:
EXP1687 Builder::import RED42/GREEN0 local early unique_ptr; EXP1688 test-model
compile STOP, EXP1689 groupedTypes makeVoidType three-site RED42/GREEN0
local ownership/rollback; EXP1690 Function constructor second-parameter
RED42, EXP1691 local RAII GREEN0; EXP1692 Builder raw Function adoption
RED42/GREEN0 local. All are source-extracted model tests outside Git, not an
exception-enabled 83-TU/nine-archive build. Next causal target is one coherent
external whole-job allocation/ownership policy and fail-at-N sweep covering
remaining grouped types, Module/Builder destructor safety, allocator/context,
facade and Windows ARM64 unwind; do not claim recoverable OOM from these
isolated sites. No J313 run can distinguish an unlinked CPU dependency defect.
Security-blocked C21E/UBFE/C27 remains untouched.

## Prior state superseded by C15 CPU/OOM checkpoints

## CURRENT 2026-10-20 06:00Z — EXP1681 ROOT PROVIDER PASS, FACTORYV2 RECOVERY_DONE

Hardware-qualified current0003 Device/Root provider lifecycle source
`a6d871ad33416c94a3dc25bd0c22fff78b00fa8c` on EXP1663 source9b374daf.
Signed build1671 EXP1678 exact packet run as EXP1681: 63 unique non-E
D3D12 PASS/0FAIL, ROOTC/ROOTG/C14A PASS, C21E known SKIP; UMD root83/83
hr0/device-create43/device-destroy34 and no root reject/OOM. AGX PASS status
occurrences142/78 unique labels identical EXP1663, only known MSAA-a000r
FAIL. KMD pool workers5 PASS, FL11 ARM64/WOW/debug PASS, GL51.7/internal59.6,
CS46.666, corpus414 each/createfail64/4/hangs0/pixel_diff0, DWM faults0.
Raw `.local/experiments/EXP1681-d3d12-root-provider/root-verdict1681.json`
SHA `8293e8c8925c34d425f2b79d8c1c9535aa7fedae6017beb5f22b66884e023c87`.
Exact FactoryV2 rollback RECOVERY_DONE. Current J313 normal GPU-visible
inert one ACPI\APPL0002 Code28, PackageAbsent/noService/noFiles, 8CPU at
boot2026-10-20T05:59:42.5326950Z. No active GPU experiment.

Next causal target: C15 internal typed shader input adapter using the now
qualified Root provider, with explicit PROGRAM/IO span and exact VOID-DDI
error category. See `investigation/analysis/D3D12-C15-r1-shader-error-boundary.md`.
Do not publish CORE_0010/SM6 or run J313 for a CPU-only unknown; dxil-spirv
internal recoverable OOM is still separate. EXP1664 range-overflow correction
is implemented on branch but deliberately excluded from the accepted EXP1681
hardware source. Security-blocked C21E/UBFE/C27 stays untouched. Preserve
user-dirty Mu.

## Prior state superseded by EXP1681

## CURRENT 2026-10-20 04:55Z — EXP1663 ROOT CALC/CREATE PASS, FACTORYV2 RECOVERY_DONE

Hardware-qualified old0003 root Calc/Create shared checked extent source
`9b374daf5bb08e4af541f198cf52dbe88206dd9b` on accepted EXP1594
source `57bd44323168c39397987c97db08adf0c122f50e`. Signed build1659
EXP1662 exact package run as EXP1663: 63 unique non-E D3D12 PASS/0FAIL,
ROOTC/ROOTG/C14A PASS, C21E known SKIP; 142 AGX PASS occurrences/78
unique labels match EXP1594, only known MSAA-a000r FAIL. KMD pool workers5
PASS, FL11 ARM64/WOW/debug PASS, GL52.3/internal59.6, CS46.406,
corpus414 each/createfail64/4/hangs0/pixel_diff0, DWM faults0. Raw
`.local/experiments/EXP1663-d3d12-root-preflight/root-verdict1663.json`
SHA `9be27baf90f7523c6ab0af953789366da030680e7f32535574e464536972bfc1`.
Exact FactoryV2 rollback completed RECOVERY_DONE. Current J313 normal
GPU-visible inert one ACPI\APPL0002 Code28, PackageAbsent/noService/noFiles,
8CPU at boot 2026-10-20T04:54:38.7459360Z. Source root extent rejection
is deterministic host-verified; J313 validated ordinary integration.
No R1/SM6/provider DXIL callbacks registered. Next causal target C15
canonical root snapshot with provider Register/Retire and lifetime, then typed
shader path; first offline source/WDK tests, then separately isolated build
and J313. Security-blocked C21E/UBFE/C27 stays untouched. Preserve user-dirty
Mu. No active GPU experiment.

## Prior state superseded by EXP1663

## CURRENT 2026-10-20 03:46Z — C15 R1 STATE/PSO LINKS + SHADER ROOT CPU BOUNDS; EXP1594 RECOVERY_DONE

New uncalled checked root-private extent helper811d0931: explicit
parameter/range/static-sampler/DWORD policy, NULL/overflow guards, host
ASan/UBSan PROT_NONE checks14 PASS; EXP1653 pinned MSVC C3493 STOP preserved,
EXP1654 corrected pinned WDK ARM64 three rc0. It is included but current
CalcPrivateRootSignatureSize/CreateRootSignature do not use it; no provider
Device/Root registration or lifetime claim. Next causal target is one
deterministic actual-source test and owner-layer wiring of exact extent plus
canonical root Register/Retire. That changes current 0003 root private
lifecycle and must have separate production package/J313 recovery checkpoint
after offline tests. Existing CORE_0003/SM5.1 remains, J313 normal visible
Code28/PackageAbsent/noService/8CPU.

## Prior state superseded by EXP1654 root extent CPU gate

Accepted build-only PSO link EXP1650 remains below. New CPU shader
prerequisites: uncalled bounded first-eight-byte PROGRAM span helper
71ebdfc4, host ASan/UBSan PROT_NONE checks9 PASS and EXP1651 WDK ARM64
3rc0; uncalled canonical root encoder576f0ec7, host guard-page checks16 PASS
and EXP1652 WDK ARM64 3rc0. Neither proves remaining shader body readability
on arbitrary pointers or registers Device/Root with linked input provider.
Current root private object does not record its allocation extent and
DestroyRootSignature is a no-op; provider Root canonical snapshot/lifetime
cannot be wired by passing empty/raw bytes. Next causal target: establish
exact root storage extent and Device/Root registration/quiescence with a
deterministic CPU lifecycle regression before typed shader callbacks.
CORE_0003/SM5.1 unchanged, dxil-spirv internal recoverable OOM blocker
unchanged, J313 normal visible Code28/PackageAbsent/noService/8CPU.

## Prior state superseded by EXP1652 shader CPU prerequisites

Isolated sourcebf2e8be5 parent accepted depth/rasterae3eb572 changes only
typed 0010 PSO destination adapter: 727 other archived files and current
CORE_0003 table/caps byteequal. EXP1646 runtime203rc0, EXP1647 fresh8rc0,
EXP1648 full SYS/UMD build rc0/version30.0.1648.0/no new warnings/stackPASS.
EXP1650 tlog3149reads/344unique UNKNOWN0 after exact two-header pins;
imports equal accepted1527, exports two openers. Combined build-only receipt
.local/c15-r1-pso-package1648-result/root-r1-pso-link-review1650.json
SHAdf0cfa78b483a6fd08e83f1da05a33c160a3873c0eef27e22ea017c6da969141.
No signed install, PE execution or J313 run: typed blend, element,
depth/raster and PSO callbacks remain unregistered in CORE_0003/SM5.1.
Next C15 target typed shader input/lifetime and coherent CORE_0010 table;
dxil-spirv recoverable internal OOM remains independent production blocker.
Normal visible guest Code28/PackageAbsent/noService/8CPU; no GPU discriminator.

## Prior state superseded by EXP1650 PSO native link verdict

Isolated sourceae3eb572 parent accepted element07a9d20e changes only
typed 0010 depth/raster CPU wrappers; 727 other archive files, current
CORE_0003 table/caps and prior R1 blend/element code byteequal. EXP1638
runtime203rc0, EXP1639 fresh8rc0, EXP1640 full SYS/UMD build rc0/version
30.0.1640.0/no new warnings/stackPASS. EXP1642 tlog3149reads/344unique
UNKNOWN0 after exact two-header source pins; imports equal accepted1527,
exports only two openers. Combined build-only receipt
.local/c15-r1-depth-raster-package1640-result/root-r1-depth-raster-link-review1642.json
SHA0e83c9cd36d341185edb421228c2b0c1bbcbc5ae865e38707b7b473f070c6707.
No signed install, PE execution or J313 run: all new typed state callbacks
remain unregistered under CORE_0003/SM5.1. Next C15 target typed shader and
PSO/lifetime paths, then coherent CORE_0010 table; independent dxil-spirv
internal recoverable OOM blocker remains. Normal visible guest
Code28/PackageAbsent/noService/8CPU. No GPU discriminator yet.

## Prior state superseded by EXP1642 depth/raster native link verdict

Isolated source07a9d20e parent accepted blend34cacd10 changes only the
typed 0010 element-layout wrapper DDI hunk; 727 other archived files,
current CORE_0003 table/caps and R1 blend code byteequal. EXP1631 runtime
203rc0, EXP1632 fresh8rc0, EXP1633 full SYS/UMD build rc0/version
30.0.1633.0/no new warnings/stackPASS. EXP1635 tlog3149reads/344unique
UNKNOWN0 after exact provider/projector header pins; 17archives/eighthelpers
once; PE imports equal accepted1527 and exports only two openers. Combined
build-only receipt .local/c15-r1-element-package1633-result/root-r1-element-link-review1635.json
SHAdb8cc18e6368e58350385b51296d7ed5057b91a5ca29746a84d13800aed4b05c.
No signed install, PE execution or J313 run: typed blend and element
callbacks unregistered in current CORE_0003/SM5.1. Next C15 target: typed
depth/raster/shader/PSO groups, then coherent CORE_0010 table; independent
dxil-spirv internal recoverable OOM blocker remains. Normal visible guest
Code28/PackageAbsent/noService/8CPU. No GPU discriminator yet.

## Prior state superseded by EXP1635 element native link verdict

Isolated source34cacd10 parent accepted02b79418 changes only DDI typed
blend wrappers plus new projector header; it retains accepted 0003
FillDDITable/caps and726 other archived source files byteequal. EXP1624
runtime203rc0, EXP1625 fresh8rc0, EXP1626 full SYS/UMD build rc0/version
30.0.1626.0/no new warnings/stackPASS. EXP1628 actual tlog3149reads/344unique
UNKNOWN0 after exact two-header source-manifest pins, 17archives/eighthelpers
once; PE imports equal accepted1527 and exports only two openers. Combined
build-only receipt .local/c15-r1-blend-package1626-result/root-r1-blend-link-review1628.json
SHA0828b4d10aa2f71ae86986df377fd2b978c0e0816d608ddf90f354e2a5187d2e.
No signed package, PE execution or J313 run: typed wrappers unregistered in
current CORE_0003/SM5.1. Next typed R1 CPU state group is element layout,
depth/raster/shader/PSO, then coherent CORE_0010 table; dxil-spirv internal
recoverable OOM remains independent. Normal visible guest Code28/Absent/8CPU.

## Prior state superseded by EXP1628 full native blend link verdict

Provider native link build-only receipt EXP1620 remains accepted (below).
New unregistered typed R1 blend projector/header ee60b4ea and DDI
calc/create wrappers c8fafeb0: actual-source host ASan/UBSan checks16+21
PASS; EXP1621/1622 pinned Windows ARM64 WDK native-owner/NONE/winsys syntax
3rc0 each. The wrappers validate same-device LibraryReference through the
linked zero-cache module, retain all eight render-target semantics in current
private state and report invalid VOID create without writing output. They are
NOT assigned to CORE_0010, do not change GetVersions/GetCaps/SM5.1 and have
no full production PE or GPU execution claim. Next C15 CPU target: typed
element layout/depth/raster/shader/PSO groups and positive CORE_0010 table
only after every required callback/lifetime path is coherent. DXIL internal
recoverable OOM remains independent production blocker. J313 normal visible
Code28/PackageAbsent/noService/8CPU; no reason for a GPU run yet.

## Prior state superseded by EXP1622 typed blend CPU gate

Source02b79418 parentfd107ef1/one UMD project delta now has original
zero-cache, owned-core and provider CPU modules in production ARM64 UMD.
EXP1613 normal runtime203rc0, EXP1614 fresh8rc0. EXP1615 full SYS/UMD
build rc0/stack ARM64+WOW PASS/version30.0.1615.0. EXP1618 exact tlog
closure3147reads/343unique UNKNOWN0 after source-manifest-pin of one newly
read provider header; zero-cache/owned-core/provider CL+link objects once,
prepared program/legacy sole parser owners, no project parser duplicates.
EXP1620 PE review confirms import set equal accepted1527, exports only
OpenAdapter10_2/OpenAdapter12, warning delta empty. Combined build-only
receipt .local/c15-provider-dedup-package1615-result/root-provider-link-review1620.json
SHAd727ba6fcac93cb1b353a45cb7d82005aa27061d3b3817afc8f11f5217453ee5.
Old reviewer STOPs EXP1616/1617/1619 preserved; native build not rerun.
No signed package or J313 experiment because modules uncalled in current
CORE_0003/SM5.1. Normal visible J313 Code28/PackageAbsent/noService/8CPU.
Next source-first C15 target: complete typed R1 adapters and owned pipeline
recipe/library behavior, with recoverable dxil-spirv internal OOM still an
independent production blocker. Do not advertise SM6 or probe caps yet.

## Prior state superseded by EXP1620 native link verdict

EXP1609 source-bound runtime and EXP1610 fresh8 accepted for scoped NOMINMAX
sourcefd107ef1. EXP1611 then compiled owned-core/provider/parsers with0warnings
but linker STOPPED LNK2005: prepared program.obj/legacy.obj already define
agx_dxil_parse/agx_legacy_frame_program, duplicated by project C objects.
No valid UMD PE or guest run. Project correction fdd6900f removes only two
parser C ClCompile entries, leaving provider/owned-core include paths and
NOMINMAX; isolated EXP1612 source02b79418 one-project delta/726 other files
byteequal. Next fresh runtime EXP1613, helpers, full UMD link. Previous
EXP1603, EXP1607, EXP1611 failed receipts remain unchanged. J313 normal
visible Code28/PackageAbsent/noService/8CPU, current CORE_0003/SM5.1.
Do not run hardware for these compile-only facts.

## Prior state superseded by EXP1611 linker duplicate-owner verdict

Corrected provider source closure EXP1604 fd6b7f59, runtime1605 and fresh8
helpers1606 passed, but full ARM64 UMD build EXP1607 STOPPED before link:
provider `std::numeric_limits::max()` collided with pinned Windows SDK
`max(a,b)` macro under /WX. This excludes EXP1603's missing-file/header cause.
Project fix f75b4381 sets NOMINMAX only on owned-core/provider C++ entries;
new isolated source EXP1608 fd107ef1 has one project delta/726 other files
byteequal. EXP1609 source-bound runtime next, then fresh8 and full UMD link.
EXP1603 and EXP1607 remain failed receipts. No signed package or guest run;
normal visible J313 remains Code28/PackageAbsent/noService/8CPU. Current
CORE_0003/SM5.1 unchanged and provider modules uncalled. No new GPU
experiment is justified by these compile-only facts.

## Prior state superseded by EXP1607 native macro failure

EXP1601 corrected-project first source28438fdd complete normal runtime203rc0,
199AA64/25422deps UNKNOWN0; EXP1602 fresh eight helpers AA64/300deps PASS.
EXP1603 full UMD build STOPPED before link on two actual C1083: missing
`agx_dxil_program.h` include directory and provider CPP absent from isolated
source snapshot. No guest action or package. Correction commit b2c12c1b adds
mesa/dxil+dxbc include directories for owned-core and original parser C
translation units to project; isolated source EXP1604 commit fd6b7f59 parent
14c38bfd includes exact project delta and original provider CPP/H, 727 files
with724 old source files byteequal. Next fresh normal-runtime EXP1605, eight
helpers, then full native link. Do not reuse or relabel failed EXP1603. The
modules remain uncalled under CORE_0003/SM5.1, so no J313 diagnostic run is
justified. Normal visible J313 remains Code28/PackageAbsent/noService/8CPU.

## Prior state superseded by EXP1603 C1083 and EXP1604 source correction

J313 normal visible Code28/PackageAbsent/noService/8CPU after exact EXP1594
FactoryV2 recovery, boot01:20:23Z. No active GPU experiment or package.
EXP1594 source57bd4432 hardware63nonE PASS/C21EknownSKIP,73smokePASS,
pool/fl11/corpus/DWM unchanged; root-verdict SHAad4477ca5c2ced53dea0c78b67372559e9bea03168dcea0a16bd0f357d217a96.
Original zero-native-cache CPP module now included by project commit30278dbd;
isolated source14c38bfd parent57bd/onlyprojectXML changed,724otherfiles
byteequal. Runtime1596 and fresh8-1597 accepted. Full UMD build1598 rc0;
old conservative1525 tlog reviewer STOP on newly intended object preserved,
new EXP1599 data review confirms actual CL read and singleton UMD link input
agx_d3d12_zero_cache_library.obj,2661reads294uniqueUNKNOWN0,
importsEqualbaseline/only2exports, warnings0/stackPASS. Link verdict
.local/c15-zero-cache-link-package1598-result/root-zero-link-review1599.json
SHA03050b07346747421bedb48dd82fb1962cde0e98101d5fefb742020ecdc9bed5.
This is BUILD_ONLY: CORE_0003/SM5.1 unchanged, callbacks not published,
no new hardware reason. Next C15 contract requires complete typed0010 adapters,
owned pipeline recipes/serialization and recoverable dxil-spirv allocator OOM;
selected upstream HEAD remains licensed pin8434b063. Do not probe caps,
link unsafe DXIL production path or retry C21E/UBFE/C27 blocked work.
Preserve userdirtyMu. Ledger/CHANGES commit pending.

## Prior state superseded by native C15 link verdict

EXP1594 signed1592/source57bd4432: exactly63unique nonE D3D12 PASS/0FAIL,
C21EknownSKIP, baseline1582 missing0; ROOTC/ROOTG bound receipts,
C14A512/512, C24ABC/C25/C26 unchanged. 73SMOKEPASS+knownMSAAFAIL,
KMDpool5workersforeign_reclaim1/FL11/corpus414each64/4createfail/DWM0.
GL52.5/internalGL59.6/CS46.282fps single samples. FactoryV2 exact-owner
recovery finished normal Code28/PackageAbsent/noService/8CPU/RECOVERY_DONE
after boot2026-10-20T01:20:23Z; no hidden/fallback. Root verdict
.local/experiments/EXP1594-d3d12-live-identity/root-verdict1594.json
SHAad4477ca5c2ced53dea0c78b67372559e9bea03168dcea0a16bd0f357d217a96.
Actual-source CPU ASan/UBSan/TSan proves live A/B/unknown/retired identities;
GPU run proves native private layout and ordinary regressions. Zero-native-cache
module is still unlinked: no R1 library execution/SM6 admission claim.
Selected dxil-spirv upstream HEAD on2026-10-20 remained exact licensed pin
8434b063; internal recoverable OOM blocker persists. Next causal target is
coherent typed0010/R1 library/provider/allocator contract, using source-first
offline gates; current0003/SM5.1 caps unchanged. No active GPU experiment.
Keep J313 normal visible inert; preserve userdirtyMu and C21E/UBFE/C27 blocks.
Ledger/verdict commit pending.

## Prior state superseded by EXP1594 verdict

EXP1594 dispatched00:45:27Z unified session76010/root-run1594.log,
stageHASHES_MATCH23/ARTIFACTS_AND_STAGE_PASS/factoryV2 wrapper61acaddf,
pipeline generated BEFORE commit c6930ccd. Full-owner Windows pinnedSSH up,
first probe RESULT failures0; DWM/GL and complete regression still pending.
DO_NOT_RESTART. Monitor scratch newexp1594.log/watchdog1594.log/run1594.log.
After TOOL_d3d12-on_END+NEWEXP_DONE check63nonE+C21ESKIP and all regressions,
collect evidence then exact rec1594h/recover.sh to RECOVERY_DONE. No final
while experiment or recovery active.

## Signed packet plan superseded by active EXP1594

J313 normal visible guest Code28/PackageAbsent/noService/8CPU boot00:19:02Z,
USB m1n1 uartproxy present and normal recovery-owned launcher active. No GPU
experiment active; ordered restart owns previous launcher for next run.
Next C15 identity source57bd44323168c39397987c97db08adf0c122f50e
parent hardware-qualified621ffcc4, only DDI delta/724other files byteequal;
CPU ASan/UBSan+TSan live identity PASS, six real PSO host modes PASS,
WDK3rc0/full1640 exact124known failure IDs/no added. Fresh runtime1590,
eight1591, full1592 and signed1593 accepted. Final30.0.1592.0 packet
.local/c15-final-package1592-signed1593.zip SHA0e19729a53bd27e53c8f9bddb8ae8bc0971fd853566cb967c95ce9ea52a6c8ff;
receipt6120ea9c/sourcearchivefb31a39d/manifest7a22157a. EXP1594 full
BEFORE .local/preparation-1594/before1594.txt SHA39f6bfaa; 28-file prep
manifestSHA52b79b6c. Root approval and dispatch pending after ledger commit.
Expected63nonE+C21ESKIP, smoke73+knownMSAA, pool/FL11/corpus/DWM;
exactFactoryV2 rec1594h cleanup to RECOVERY_DONE. No R1/library/SM6 claim:
zero-native-cache module remains unlinked and DXIL allocator OOM unresolved.
Preserve userdirtyMu. Do not restart/stage except through hash guard.

## Prior state superseded by signed EXP1594 plan

Normal GPU-visible guest is Code28/PackageAbsent/noService/8CPU after exact
EXP1582 recovery; no active GPU experiment. Accepted EXP1582 source621ffcc4
passed63nonE/knownC21ESKIP, smoke73+knownMSAA, pool/fl11/corpus/DWM.
Next C15 live Device/PSO identity adapter commit2566fd70: actual-source
ASan/UBSan+TSan address-safe A/B live/retired/concurrent resolver test PASS,
six PSO compute/graphics serial/parallel/failure modes PASS, WDK1586 3rc0,
full1588 1640tests exact124known failure IDs ADDED0. Prior1585 declaration
order STOP preserved; 1586 corrected. Source snapshot EXP1589 commit57bd4432
parent621ffcc4, one DDI delta/724other files byteequal, archivefb31a39d,
manifest7a22157a. Runtime1590 BEFORE written; builder dispatch pending after
ledger commit. No pipeline library linkage, R1 table/caps, DXIL or public SM6.
Further native package and normal GPU checkpoint must be preregistered exactly,
then recover visible Code28. Owner publication/native context and converter OOM
remain separate gates. Preserve userdirtyMu and blocked C21E/UBFE/C27 tasks.

## Prior state superseded by C15 identity source gate

EXP1582 source621ffcc4/signed30.0.1580.0: exactly63nonE D3D12 PASS/0FAIL,
C21EknownSKIP, baseline1573 missing0; ROOTC/ROOTG direct bound receipts,
C14A512/512, C24ABC/C25/C26 unchanged. 73SMOKEPASS+knownMSAAFAIL,
KMDpool5workersforeign_reclaim1/FL11/corpus414each64/4createfail/DWM0.
GL52.2/internalGL59.6/CS46.259fps single samples. FactoryV2 exact-owner
recovery completed clean normal Code28/PackageAbsent/noService/8CPU,
RECOVERY_DONE after boot2026-10-20T00:19:02Z, no hidden/fallback.
Root verdict .local/experiments/EXP1582-d3d12-heap-registry/root-verdict1582.json
SHA3fee574387d1cc93de87c05a02ecc5a24dfd0782b5c1ed181a8e40c288709da9.
Actual-source TSan RED/GREEN proves registry race correction; GPU run proves
native private-device layout and ordinary regressions, not a forced concurrent
runtime schedule or complete owner/context thread safety. No active GPU run.
Next causal target C15 coherent provider/library/owner contract and safe DXIL
allocator failure handling. Existing dxil-spirv pin8434b063 is licensed/reviewed
but lacks internal recoverable OOM; current0003/caps SM5.1 remains. Source-first
offline gate before another hardware experiment; do not probe caps or repeat
security-blocked C21E/UBFE/C27 delegation. Preserve userdirtyMu and normal
visible guest. Ledger/verdict commit pending.

## Prior state superseded by EXP1582 verdict

EXP1582 dispatched at23:44:03Z, unified session27195/root-run1582.log.
Stage HASHES_MATCH23/ARTIFACTS_AND_STAGE_PASS, fresh FactoryV2 wrapper
SHA6c9dca764388ed7591085cf2481f7e93fee46fd69a745da0595451419c2d7f6d;
pipeline generated BEFORE commit f27a7fba. Normal recovery guest being
restarted into full-owner profile, GPU result pending. Do not restart EXP1582.
Monitor scratch newexp1582.log/watchdog1582.log/run1582.log with bounded waits.
After TOOL_d3d12-on_END and NEWEXP_DONE verify63nonE/knownC21ESKIP and
regressions, collect evidence, run exact rec1582h/recover.sh to RECOVERY_DONE.
No final turn during active experiment or recovery.

## Signed package plan superseded by EXP1582 active run

EXP1573 root identity validated direct ROOTC/ROOTG receipts,63nonE PASS,
exact recovery finished Code28/PackageAbsent/noService/8CPU. Normal guest boot
23:37:26Z; its recovery-owned normal run_uefi PID79948 is alive and will end
through the next ordered restart. No installed AppleAgx package.
NEXT isolated heap-registry source621ffcc4/e5802714 has real TSan RED/36GREEN,
WDK3rc0/fullhost1639 exact124known IDs, runtime1578 and fresh8-1579 PASS,
full package1580 and signed1581 accepted. Final 30.0.1580.0 packet
.local/c15-final-package1580-signed1581.zip SHAa4d5f2f79dbee5b2558b84ffafa67289175e5b7c0791d42e55bfbdcf11f8fb02;
receipt3fe21b5d/source621ffcc4/manifest55efd511. EXP1582 full BEFORE
.local/preparation-1582/before1582.txt SHAefa5a44e; 28-file prep manifest
SHA81dcdee8. Root approval and dispatch pending after this ledger commit.
Expected same63nonE+C21ESKIP, smoke73+knownMSAA, pool/fl11/corpus/DWM;
normal FactoryV2 exact package1580 cleanup after run. No complete shared-owner
context/concurrency or public SM6 claim.

## Prior state superseded by signed EXP1582 package

EXP1573 signed1571/sourcea1e01c91: ROOTC+ROOTG direct bound-state receipts
and output PASS; 63unique nonE PASS/0FAIL/C21EknownSKIP, baseline missing0.
73SMOKEPASS/knownMSAAFAIL, pool/fl11/corpus/DWM unchanged; CS47.026fps.
FactoryV2 guarded no-host-owner recovery completed exact package removal,
normalCode28/PackageAbsent/noService/8CPU/RECOVERY_DONE after boot23:37:26Z.
Verdict evidence .local/experiments/EXP1573-d3d12-root-identity/root-verdict1573.json
SHA5fde05f1241c646d2388262f477e5377505d4bc7e5479c99fd50ec7ad9ce8201.
Original exact-owner guardSTOP preserved before fallback. No active guest experiment.
Next independent source621ffcc4/e5802714 is per-device heap-registry SRW lock:
actual TSan RED ->36GREEN, WDK3rc0, full1639 exact124knownIDs/no newfail.
Runtime1578ROOTPASS; fresh8EXP1579ROOTPASS; fullpackage EXP1580 builder active
session32430, .local/c15-heap-registry-package1580-prep/controller-root.log.
After complete, root-review/tlog, sign1581, freeze, then new hardware BEFORE1582.
Do not claim complete owner/context concurrency or publicSM6.

## Prior state superseded by EXP1573 recovery

1571fullpackageROOTPASS/1572finalsignature+CMSGREEN packet4fc1cf67,
sourcea1e01c91/manifest3602ae75/receipt97b40ace, version30.0.1571.0.
Next1573 exact .local/preparation-1573/run1573.sh; BEFORE nowledgered,
preparationmanifest SHA36b7c869c0a70f7ac9ba14624e76437eabb685d14e9e0ade804e1ee97ac5b46f.
ROOT-APPROVAL.json source/manifestPASS beforecommit3b481ff6. Dispatched
19:19:38Z unifiedsession4267/root-run1573.log. DO_NOT_RESTART.
StagePASS/HASHES_MATCH23/factoryV2wrapper85bb5863; fullownerboot
CM_PROB_NONE/DWM1224/Explorer5204/SSHup and displaylatches. CS47.026fps,
GS/MRTongoing DWMfaults0; no finalGPUverdict. Scratchnewexp1573.log/watchdog1573.log.
Pipeline appends generatedBEFORE/commit afterstage; do not editEXPERIMENTS untilthat.
Await TOOL_d3d12-on_END+NEWEXP_DONE, allregressions then FactoryV2recover1573
toRECOVERY_DONE. No finalduringactiveexperiment.
Expected ROOTC/ROOTG plusprior61nonE =63PASS/C21EknownSKIP; runtimeelision
limitsdirectbranchverdict unlessnonzeroVA/DWORDreceipts. KMD/firmware/caps same.
FreshguestPASSCode28Absent/noService/noFiles8CPU boot18:59:38.8120150Z;
pgrepANYrun_uefi rc1empty, USBrunninguartproxy. Active1573hardwarepipeline only; no separatebuilder/recovery now.
Previous active1569/1570/1571/1572 notes below superseded by these verifiedresults.
NEXT independentmetadatafix e5802714: actualvalid heapregistry TSANrace RED ->36GREEN,
1575native3RC0/full1576tests1639 exact124known IDs ADDED0. Snapshot1577source
621ffcc4dcbbdb9c6bc664cb7201de14cb5dd0b5 ONLYDDI vsa1e; archive59685dda/
manifest55efd511. Runtime1578 ACTIVE unifiedsession67849, beforecommit726c80f2, approval
c1b55521e4fc999dbd15e4db1217e41e30b7c3265c1f1419c837db647d0eb2f6.
Do NOTrestart. .local/c15-heap-registry-runtime1578-prep/controller-root.log;
result05.stdout runtime-closure91rc0 observed; finalpostpins/collectionPENDING.
Preps .local/c15-heap-registry-runtime1578-prep; future1579/1580generatorsprepared.
No metadata lock covers nativecallbacks; ownerpublication/context stillopen.
Futurepackage activation MUST wait current1573verdict and exactrecovery.
Do not installbeforeguard/rootapproval, nofakePID/hiddenboot/manualprereboot.

## EXP1562 RECOVERY_DONE (prior verdict)

EXP1562 sourcef424/package30.0.1560.0 finished:61nonE PASS/0FAIL/C21EknownSKIP;
C14A512/512; C24ABC/C25/C26PASS; pool/fl11/corpus/DWM no newfailure.
GL53.1/internalGL59.6/CS45.232fps singlepoints. Full-owner start18:17Z,
D3D12END18:44:42Z/NEWEXP_DONE. Exact FactoryV2 recovery finished normalboots
18:57:35Z and18:59:09Z, Code28/PackageAbsent/noService/8CPU/RECOVERY_DONE.
First exact-owner guard stopped0 BEFOREwrites; existing no-owner guardedmode
then succeeded with all three conditions. InitialSTOP preserved, nohiddenboot.
No active GPU/recovery process. EXP1562 verdict/ledger commit e1215b49.
Root identity fix commit450c90128484f8e79a6d6d4a0a0dc0bf8db7d64f;
probe ROOTC/ROOTG commit17629e9e, build1567AA64/41pinsMATCH/importsunchanged.
Source1568a1e01c91048e64d0476c62505e662ee6a55c79d9 exact724unchanged;
archive7387bfb6/manifest3602ae75. CURRENT ACTIVE BUILDER1569 session49677,
.local/c15-root-identity-runtime1569-prep/controller-root.log; result05.stdout
has203commandsRC0; postpins/collection/rootreviewPENDING. Never restart.
Approval134e91565d2f1819cc1475d61ed2fb7911150dde44426d0c2628086148715860,
beforecommit056d0bfe. Afterruntime finish run root-review-build1569.py then
review-closure1569.py in prep. Prepared next generators .local/
c15-root-identity-prepared1570-prep/prepare.py and
c15-root-identity-package1571-prep/prepare.py; require actual preceding output.
Plan1570fresh8 ->1571package ->1572sign ->1573GPU ROOTC/ROOTG+regressions.
No pending new hardware launch yet; normalguest packageAbsent.
 Next source root identity actual1563RED ->35GREEN;
final threefield receipt nativegate1566 threeRC0; ROOTC/ROOTG candidate outsideGit.
Do not rerun1562 or claim publicSM6/directerrorbranch GPU validation.
Current source/test/doc changes uncommitted separatefrom1562 bookkeeping.

## Prior state (superseded by EXP1562 verdict) — EXP1530 RECOVERY_DONE; C15 CPU boundary validated

ACTIVE1562 stagePASS/HASHES_MATCH23/FactoryV2wrapper701a6cf67b423f549cb68c195d9f0e2fec7d7cae03a442d48a4c45c1504d65ae.
PipelinegeneratedBEFOREcommit ad77b5ba. Newfull-ownerboot reachedCM_PROB_NONE,
DWM1236/Explorer5252 alive, firstGLdrawgreen removed0. NoGPUverdict yet.
18:23boundedSSHup; currentlyCSplaynoovl+segstats170s parallel (segstats output
buffered, NOT guesthang). Newexp1562log mayquietduringCS; checknestedbenchlogs
andboundedwatchdog, notblindreset. Unifiedsession70506/root-run1562.log active.
Userinformednewbootactuallyoccurred. AwaitTOOL_d3d12-on_END+NEWEXP_DONE andall
regressions, thenFactoryV2exact1560cleanupRECOVERY_DONE. C21EknownSKIPneverretry.

ACTIVE GPU1562 DISPATCHED/EXP1562_START18:17:23Z. DO_NOT_RESTART.
Unifiedhostsession70506 runs .local/preparation-1562/run1562.sh; Root approval
e86813b687a92261817acb559d6b4b7cbf733c95/prepmanifest38152a9c. Logs
.local/preparation-1562/root-run1562.log and scratchnewexp1562.log/watchdog1562.log/
run1562.log. Sourcef424/signed1560packet237d0bf6/receiptf3e588b1 frozen, guards+
priorrecovery+packagebaselineMATCHpassed, initialwatchdogSSHup. Stage/full-owner
boot/regressions IN_PROGRESS, noverdict yet. Expected61nonEPASS+C21EknownSKIP,
SMOKE73+knownMSAA, GL/corpus/DWM/CS. Pipeline willappendfullgeneratedBEFORE+
commit afterstage; do NOTeditEXPERIMENTS until thatcommit. Monitor bounded.
AfterTOOL_d3d12-on_END+NEWEXP_DONE collect/checkevidence thenexactFactoryV2
evidence/rec1562h/recover.sh toRECOVERY_DONE; no manualprereboot/hiddenboot,
no retryold1530recovery. Do not endturnwhileexperiment/recoveryactive.

1561 FINAL_PACKET ROOT_ACCEPTED: .local/c15-final-package1560-signed1561.zip
15011336B SHA237d0bf67f641f166aa0e55065f4a2287481dfa85afed29143e07dc518395641.
Finalreceiptf3e588b1/signedreviewa3ea1625/receiptReview2eed0889. INFd9fad7e2,
CAT3149b535, SYS2843c61e/UMD49014599 immutable; version30.0.1560.0/DIRID12.
CMSvalid/signerE9BE...; builderUnknownError exactuntrustedrootmessage, UMDcat-only,
gueststageCATmembershipmandatoryBEFOREactivation. All85policy/28noticesPASS.
NEXT GPU1562 BEFOREnowwritten .local/preparation-1562/before1562.txt;28file
DRAFTmanifest/14controls syntax+ALL64hexpreserved. Sourcef424 ONLYerror-owner
state correction vsaccepted40e; directerrorbranchesCPU-only, hardwarechecks
newnativequeueCalcPrivate layout/ordinary61regressions, noSM6/capsprobe.
LiveguestPASS Code28Absent/noService/noFiles/8CPU sameBoot15:15:38.3435750Z;
pgrep noowner/USBm1n1uartproxy. Actualm1n1_windows clean5b857603, Mu361a2134
userdirtySHA6ec31378 preserved. Prior1530factory+recoveryreview0b9c73ba.
RootapprovalBeforeCommit pendingthenIMMEDIATEdispatch run1562.sh. Logpaths
prep/root-run1562.log +scratchnewexp1562.log/watchdog1562.log/run1562.log;
EXP1562-d3d12-error-owner-regression. Do not restartafterdispatch. Collect61
nonEPASS+C21EknownSKIP/regressions thennormalFactoryV2recover1562exactpackage1560
toRECOVERY_DONE. PriorityGPU, nofinalduringactiveexperiment.

1560 FULLPACKAGE_BUILD ROOT_ACCEPTED .local/c15-package1560-result/root-post-review1560.json
SHAabea3e71bdf7080af7de9432dddeb0b402a16c70dae08dd9f6e69fd151554691.
13991prepostMATCH/114outputhashes/8freshAA64+17archivesONCE/tlogs2461reads285unique
UNKNOWN0/UMDmacro1C++17/importsexportsSAME1527/notice28MATCH/stackARM6413824WOW6544PASS.
ActualSYSUMDversion30.0.1560.0, initialINFsameversion. SYS2843c61e/UMD49014599.
NEXT1561finalINF1235DIRID12/WOW/CATsign freeze BEFOREwritten; prep
.local/c15-package1561-repack-prep controller49bfa79b/owned088b520f/run2e6645a6.
85inputrows,ONLYINF+CATmutable; allSYSUMDWOWPDBnoticestoolsimmutable;promotion38.
RootapprovalBeforeCommit/stagemanifestfinalization+dispatchpending. Noactivebuild
orGPUexperiment now. J313SSHup/USBuartproxyvisible; no newreboot/package since1530.
Hardware1562priorreceiptready .local/preparation-1562/prerequisite-review1530.json
SHA0b9c73ba7e2e0c10afa3a3da22f3c83e62db681a3d423c0e76b7f7cfd6c6840f;
runharness/newfinalsignedpacket+liveguardstillpending. PRIORITYactualhardware.

1559 EIGHT_BUILD_ONLY ROOT_ACCEPTED:8freshAA64/8rc0/300depsUNKNOWN0/exact
predictedstderr; nativePID11408Exit0/controlsPID29404Exit0,3016Exit7.
Review .local/c15-error-owner-prepared1559-result/root-review1559.json SHA
1c44b24c2821f29815d2e45fb3f2ccb3771710071912aaeea3bb387ee7ba4c71.
Userrightly flagged2.5hnoGPU; PRIORITYfullsignedpackage->J313 now; no more
research/newCPUsubtasks untilGPUrun. NEXTfullBUILD1560BEFOREwritten at
.local/c15-package1560-prep. Exactaccepted1527producerprofile rebase tosource
f424/runtime1557/request1559b93;13991pins/922pathrebases/10intendedhashupdates,
28notices unchanged. Freshcache1557non-timestamp879B matches1520/proofc244.
Producer3499e600/childb24c9376/controller197689db; capturesONEactualfreshUMD
tlogdir. NativeapprovalBeforeCommit/stagehashes/Rootapproval finalization+
dispatchpending. Newfullpackage MUSTreviewactual8once+17archives/AA64/imports/
stacks/signatures/CAT/notices/postpins beforeGPU. J3131530inertunchanged.

BUILD1558 STOP preserved .local/c15-error-owner-prepared1558-result: first
frontendcompiler0 but strictstderr hash rejected literalnew1557projectpaths;
actual90dffd17 equalsaccepted1523stderr ONLYfullprefix16replacements. Same19
warnings. Noobjectsaccepted. Separate nativePS Start-Process/Refresh exitNULL
reportingbug; rawcollected/localpostMATCH. NEVERretry1558orrelaxdiagnostics.
NEXT1559BEFOREwritten/prep .local/c15-error-owner-prepared1559-prep, request
b93adebdba586ecc3b717c878055ddf814d80bf26c4e93277e9dc71e4a3aadfc.
ONLY2stderrSHAleaves exactpredictionsfrontend16/map1prefix replacements,
proof281702a9. Source/pins/archives/flags unchanged1558. NativePSf0f78184
directProcess+asyncstreams/noRefresh actualexitcontrols0/7 BEFOREsame8build;
controllerE0e90f77/collectorFE8295a6 freshstage/output1559. Rootapproval+
dispatchpending. NoactiveGPUexperiment, J3131530recoveredinertunchanged.

NEXT BUILD1558 BEFORE written, approved-dispatchpending. Fresh8COFF canonical
builder fromactual1557project/sourcef424; requestECD7efb79af6abd738935cbefce87e0bc63bc376c7c41d8b6844734eb560f942
at .local/c15-error-owner-prepared1558-prep.13954pins/17archives,888exactpath
rebases/only6hashupdates actualnewruntime/DDI/manifest,13066externalunchanged.
NativePS41021e9d/controllerEE2e0b40/collector38c32e67; bothfreshstage/output
guards,3stagehash+PSParserbeforepayload; own900s/nativechild/1100outer, noPE.
Result .local/c15-error-owner-prepared1558-result MUSTrootreviewactual8AA64/
diagnostics/deps/source/provenance/props. No old1523objectreuse. J3131530normal
Code28Absent8CPU unchanged; no activeGPUexperiment.

BUILD1557 COMPLETE/ROOT_ACCEPTED freshnormalruntime forsourcef42452fc. All203rc0,
3stages0/199AA64/119canonicalTUs/25422deps UNKNOWN0,13058prepostMATCH,
2537outputhashes/154actualarchivebodymatches+RSP154. Resourcef981same/normal
verifierPASS/newmanifest9f6bf0b6. Library4071316BSHA1938bd08cfa5fff44f647bef05ac127d740b0d9d5c51fe5846e6f472d3dd02e1;
propsc0bf8d7b/resultc0d34635. Review .local/c15-error-owner-runtime1557-result/
root-review1557.json SHA50efd69f04f0bf5613adc6480b7523e0bf1fa465edad9e27cf3bf3f190a13551;
independentclosurec6e8555e. NoPE/package/sign/stack/GPU. NEXTfreshprepared8
request boundnew1557actualruntime/sourcef424, thennewfullUMD/KMDpackagewith
exactacceptedflags/provenance/notices/stack/sign. Do notreuseold1523objects
orretrofit1527runtime. Noactivebuildercommand/guestexperiment now.

NEXT BUILD1557 prepared ROOT-reviewed: source f42452fcf15e184a66df7b913851b1b21e3193b0
refs/agx-experiments/EXP1556-source. Accepted40e725file snapshot + ONLY DDI
owner fixes,724byteequal/CompileCompute+caps+openerbyteequal. Sourcezip015890e8/
manifest9f6bf0b6/frozenreviewa92f9d3b at .local/c15-error-owner-assembly1556.
CandidateactualsourceASANroute/canaryPASS. Prior unvalidated FillDDITable
publisher remains excluded EXACTLYasaccepted1527 (solecandidate-vsHEADdiff).
Freshnormalruntime REQUIREDeverynewmanifest, no old1520receiptretrofit.
Prep .local/c15-error-owner-runtime1557-prep controllerad0626d0/runnerfe3b226d
UNCHANGED,13058nativepinsallBytesSHAunchanged/onlySELFpathrebase;metadata3b5d60d9.
BEFORE1557written; Rootapproval-beforecommit and dispatch pending. Expected
203commands/3stages/199AA64/normalverifier andRootclosure review. NoPE/sign/
package/guest/GPU. Preserve all oldrefs/artifacts; no retry on firstSTOP.

EXP1554/1555 closes last COMMAND_QUEUE error fallback. Actual-source RED
realCreateCommandQueue then SignalFenceA/B/A ->A0/B3; stored queue.device
and typed firstHCOMMANDQUEUE now A2/B1/exactHRTDEVICE/nullB isolation.
ActualCalcPrivate-sized malloc+16canary each intact; target34GREEN, WDK3rc0.
First1554buildFAIL oldDestroyDevice reset C2065 preserved;1555fixPASS.
No process-global last-created callback/HRTDEVICE remains. CPU-only, queue
native runtime private-size/hardware regression still unproved. Next prepare
exact accepted1527/source40e signed-package assembly plus finite owner-routing
diff only; preserve accepted legacy prepared compile path and exclude inherited
unvalidated C15publisher. Normal J3131530 Code28Absent8CPU unchanged.

EXP1551 closes rejected COMMAND_LIST error route through existing ListError:
current-source CPU RED ExecuteBundle A/B/A ->deviceB3/listA0/listB0; GREEN
listA2/listB1/exactHRTLIST/devicecallbacks0/nullB isolation. Existing core,
shader/state cases target34GREEN; builder WDK3rc0. Full EXP1552 1637tests
historical117FAIL/ERROR IDs plus5 sandbox localhost-bind errors; exact5
localhost-enabled EXP1553 controlPASS. No inherited timer confound, no new
code regression established, not wholeGREEN. Only COMMAND_QUEUE rejected
fallback retains last-created globals. Actual queue has runtime/flags/context
but no parent device; next source/test checkpoint must prove creation-owner
association before replacing fallback. No J313/package change or publicSM6.

Current next deterministic C15 prerequisite: DEVICE_CORE rejected-DDI errors
used last-created globals and misrouted A/B/A as A0/B3 in an actual-source
ASAN/UBSAN CPU RED. Candidate routes typed HDEVICE through the stored
per-device callback/HRT; same test now A2/B1 in both creation orders and
null-B isolation. EXP1546 builder WDK26100 syncheck all3rc0 and targeted
34GREEN. EXP1547 full1637 has historical117FAIL/ERROR IDs plus one watchdog
runner-confounded ITIMER_REAL test; EXP1548 same test unwrapped PASS. No J313
package or guest state changed, no public SM6/DXIL/DDI/caps claim. Route
committed as 7b4db8cc and indexed in CHANGES by 35a05c57. EXP1549 next
finite source correction: invalid compute/graphics StoreShader used the same
last-created globals. Actual-source RED A0/B1 for invalid A compute after
A/B; per-device HDEVICE propagation now target34GREEN and builder3rc0 with
unchanged E_INVALIDARG. No J313 state change. Ordinary state/element invalid
argument paths remain global and require separate source/test decision. This
new paragraph supersedes the older line below saying the route is only in
source-only preparation.

EXP1550 closes those ordinary state/element invalid-argument callback routes:
actual-source state RED B error delivered A after B,A create; both state B
and element A isolation now target34GREEN, builder3rc0. Only non-DEVICE_CORE
generic rejected fallback retains global SetErrorCb; list/queue HDEVICE
ownership is a distinct source-first gate. Next C15 production blocker is
coherent R1/0011 typed provider/root/lifetime/checked-compiler contract, not
an SM6 capability probe. Normal J313 EXP1530 recovery remains unchanged.

Worktree integration/ad04-windows-compiler, userdirtyMu preserved. Normal J313
GPU-visible inert guest: one APPL0002 Code28, PackageAbsent, no AppleAgx service,
SYS/UMD absent, 8 CPUs. EXP1530 RECOVERY_DONE with same normal profile;
rootverdict SHA83f643dcf921c9764d5fcee80b52a47ba4cf559a40d7058246a73cd34f8c5181.
Do not rerun1530 recovery. New signed1527 package/source40e explicitly selected
legacy prepared NIR owner route: all61nonE D3D12 PASS, C21E knownSKIP,
73SMOKE PASS+onlyknownMSAAfail, pool5 foreignreclaim1, fl11/corpus/DWM0.
GL52.8/internal59.6/CS47.067 singlepoints. Exactpackage cleaned after evidence.

C15: real EXP1532 oldadapter RED (SM6.1 bitcode masked cs6.0 header and cs6.0
wrong DXIL1.1 header both wrongly accepted). EXP1537 host 83/9 rebuilt GREEN
2612/0; existing one/mixed controls GREEN1540. Native EXP1542 83AA64/9fresh
archives/6projectobjects/three CRT-only AA64EXEs BUILD_ONLY PASS. Physical
J313 CPU EXP1534 three separate PROFILE/ONE/MIXED processes PASS and normal
sameBoot Code28Absent8CPU after. Rootreview
.local/c15-profile-cpu1534-result/root-review1534.json SHA
51d6ae9ca4e844f9e2552fb5eb7776cb964b15ea09b03e21023757a4e3cad44f.
Tracked original3project fix+realDXCcs61fixture/test commit ffb7733c;
CHANGES validation row appended. Current trackedsource target test EXP1544 12commands
GREEN; one full suite 1636 tests/117 preexisting FAIL+ERROR vsprior119,
NO added testfailure IDs, NOT wholeGREEN. External MIT implementation body remains
outsideGit at pinned .local/c15-profile-source1537/external; selectedAPI header
SHA3ffb6bb8efcd67a5aa5bec9797d08fd459545818f8ea36b11c155da26578a53a.
This is private CPU profile admission only: no production DXIL link, GPU SM6
execution, public SHADER_MODELS/caps/DDI/library/exception/fullOOM claim.
Next C15 causal target: complete coherent provider/root-description/R1 source
contract after source-only selection; do not probe caps bits. Agent sm51 is
preparing narrow design with current primary Microsoft/selected library source.
Existing C21E/UBFE security-blocked; C27 delegation security-rejected.
Never retry/reroute either blocked task. User asked Daybreak; current chat cannot
switch model by tool and user was informed; continue on current model.
Preserve normal visible guest and userdirtyMu. Historic paragraphs below
superseded where inconsistent.

## Historical GPU: EXP1522 ACTIVE / EXP1519 RECOVERY_DONE

- EXP1519Root5986/10698completed; RECOVERY_DONE PackageAbsent/noService8CPU
  Code28 ordinaryGPUvisible. Same1506signedpackage/source3d40/runtime1504.
  C25PASS6frames/1ResizeS_OK/bothindices/all26112pixelsbad0;60nonEPASS+
  C21EknownSKIP. Original59controlsunchanged/C14Acounter512drawn512/3indirect
  receiptsexact1510/C24ABC6subresources165texels padding0. 73SMOKE+onlyknown
  MSAA-a000r;pool6workersforeignreclaim1/fl11ARM64WOWdebugPASS/corpus414each
  createfail64/4/nohang/pixeldiff0/DWM0. GL52.7/internal59.6/CS46.057singlepoints.
- Actualrecoveryno manualprereboot: firstzeroownerSTOPbeforewrites preserved;
  existingguardedNO_HOST_OWNER(noANYlauncher+liveSSH) ->Start4/disarm ->normal
  Code32Stopped->exactcleanupSSH0->normalCode28Absent/noService8CPU.
  Before11:21:35.8155480Z/After11:23:44.5171950Z/durablemanifest56FBD526...
  .local/experiments/EXP1519-d3d12-present-resize/evidence/rec1519h/recovery-final.log
  0f4d16511cc90543c0891bd368f6b35ab92ab3e1e7e1a7344e57a7e19a35c9b7;
  Rootverdictd96e41d044ea51713767737102f4c53a26825356b20c164c525a2ada7d26d642;
  pre-recovery2241filemanifest472e9404/finalartifactsc8098d23. NEVERrerun1519recovery.
- ACTIVE1522root26505/.local/preparation-1522/root-run1522.log; Start11:28:32Z;
  stagePASS/hash23/generatedfullBEFORE27787d8d/FactoryV2wrapper3175058c.
  Newprobe1521C26PE49f94edd/rootreview9a27cf3f; prior1519actualrecoveryaccepted.
  LocalprelaunchguardcaughtglobalIDreplacementinRALLOClicensehash BEFOREany
  stage/guest/launcher; originalpreserved, entirepackageartifactJSONrestored
  BYTEIDENTICALaccepted1519; correctionf621a70c/newDRAFT5068172a/guardsPASS.
  SolefactorC26SAMEclosedlisttwice/fencedvolatileCBV7->11, exact32768words;
  all61nonEexpected+C21ESKIP total62logical/NEVERALL62. Samefresh1506package/
  explicitversionoverride; CSenabled. No newC15packageinthishardwareexperiment.
  DO NOT restart; monitor scratchnewexp1522/watchdog1522,
  .local/preparation-1522/root-run1522.log. RecoverynewFactoryV2orderednormal
  exactcleanup beforefollowingexperiment. No hostownerassumption/fakePID.

## Parallel C15 BUILD gates / native package1524 ACTIVE

- Hook74e0d7c4 defaultOFF/9new+5normaltestsPASS/full1630 SAME119oldfailures
  noaddedremoved/postMATCH/logc502288f. DDIroute dae87ae3 qualifiedCPU1515
  116checks; futurepackagesbaseline3d40+factory75+DDIonlyf338 excludes666publisher.
- Complete725source40e2055cfb0457d332320c358928f53e2912a0fb/customrefEXP1520-source;
  .local/c15-production-legacy-assembly1520 ZIP1bb18d59/manifeste02f4609.
  Native1520COMPLETE203RC0/199AA64/119canonical/25422deps/154archivebodyMATCH/
  RSP154/2537hashes/originalnormalverifierResourcef981PASS; normalruntime4072240B
  efdca12938e5ab5a0e56f673551438fa54b8e66c13d3099bf974425ae5949dfc/
  props46269730b7e33f9eaa60a171d6583f2d2d424fa910f7cc7990862f85a3e30e81.
- Native1523COMPLETE10RC0/8helpers+22UMDobjectsAA64/post13956MATCH; source725/
  EVALbeforeBuild/defaultmacro0/enabledexact1/exact8/realDDIstdcpp17ownerDefault;
  actual17archives+8helpersONCE/no fixture/provider/coreoldAPI;738helperdeps/
  3242UMDincludesunknown0/opaqueowner shader_info0; normalUMD0warnings/errors.
  DLL13220864Bffcc5c547a640da079f07fcc30408c8ded1e6c6b2df0568ff47f552a08e38cf7;
  imports13/exportsONLYOpenAdapter10_2+12/CRTsame5as1517primary6a023178/XIempty/
  TLSsole__dyn_tls_init/XDempty/normalDllMainretained; stacksARM6413824/WOW6544/
  noCRTassertPASS. NOPEload/sign/package/GPUclaim. Rootreviewde38b1cc local1523result.
- Newcacheobject883Be37e32ea ALL879bytesexceptCOFFtimestamp match1504qualified
  0de334b5; LOCALproof72fac0bc tiesactual1507NOOPinit/store/retrieveNULLbody,
  no redundantpreprocess. Preservehonestproof/notnewPPclaim.
- BUILD1524STOPbeforeANYcompiler/CAT/sign: nativePSConvertFrom-Jsonline8
  rejectsNoticeFilesrelative/Relative+bytes/Bytes+sha256/SHA256;28rows84aliases.
  Original.local/c15-package1524-result/controller-result/raw04stderr retained;
  no dest/build-verdict becauseproducerfailsBEFOREtry/finally; collector0files
  accuratelyreportedmissingverdict, notransportretry. Allsource/noticebytesunchanged.
- OWNERfix0b84ee9d/newhostchecker scripts/agx_powershell_json.py recursively
  rejectsduplicatecasekeys; nativeNoticeFilesnowONLYSource,Relative,Bytes,SHA256
  afteroldaliasvaluesMATCH; provenancehostnotice-files.json retainedseparate.
  TargetRED3/GREEN6/actualmetadataRED84GREEN0; full1636 SAME119oldfailures/
  noaddedremoved/sourcepostMATCH/log6ec21871. Index+1525BEFOREe932fc2e.
- BUILD1525ACTIVEroot63765 .local/c15-package1525-prep/controller.py;
  result.local/c15-package1525-result; approvalfef58c2477165a4f301197e7689ab8166fac13c6c505acebc7c0a04f2b33c07c.
  FreshbuilderEXP1525-production-prepared-package-inputs ->...-build,
  objects8=build/objects8 NEW/not1523reuse; distribution=build/distribution.
  Exact40e normalbuild-driver flags/source/native1520/immutable1523request5976
  SAME; onlyJSONownerrepair/newID/versionbookkeeping. HostcasegateAFTERpins
  beforetransport/nativeStage37hash+syntax+actualJSONparse BEFOREproducer.
  Producer c304babc96243835ca3b5200f1ebed5be09cec920efaeee72cc935ddd4a09a10;
  controller5433dfb460085363eb266f18484002462a77d26463e98b9598098e0f081a4660.
  28qualifiednoticefiles BEFOREbaselineWDKCAT/sign/recheckafter; NOTunsigned-only/
  NOTnoticeCATcoverage.13988nativepins/Owned900sbuildchild/stacks120s/firstSTOPnewID.
  Rootmustactual8receipt/COFF/tlogs(readclosureCLclearedbyhook, notstdoutclaim)/
  17archives+8helpersONCE/version/sign/importCRTexports/physicalINF SYS UMD CAT+
  2PDB/notices/postpins. SeparateINF1235repack/CATsign/finalreceipt onlyafteracceptance.
  SOURCEpostbundle .local/c15-package1525-post-source-prep/READY8c1ff292;
  PLAN/11filesfullRootreviewSTILLPENDING; signed/posthashesPENDING. Exactlywhole
  path/token/expectedbuildfieldrebase, embedded64hexallUNCHANGED proof1732015c;
  original1524postbundleimmutable. NO globalnumericID/hashreplacementever.
  No publicSM6/caps/PEexecution/newpacketGPUuntilfinalsignedfreeze.


## CPU1515 + native1517 COMPLETE — production hook SOURCE review next

- Native1517all17RC0/33AA64=10freshNoCore22fullDLL1host/post14518/152hashes;
  12actualclosures1455deps/unknown0/bothrealEVALgatesPASS/UMDhost0warnings.
  Linkactual17externalarchivesONCE/noforbiddenCoreprovider/realOwnerCreate and
  preparedCreate fromfullownerwrapper/factorycommon. DLL13227520B26d195664229f73f90f0102e94fcc44d665cd0fdffcec94286d2a25566dd3ef1;
  host84480Ba110d0c7ffed7a21d37b5a61e135b3f0cfca444ce4d139ef978738233667ba4d.
  Buildreview2bb876ba84e926d33e53112bbf84112776725f629d17830ed8b6407e9800a717.
  Loadreview440031c12a35ee8bfa4ff0f238362e551c442a9cd9f18a92210d58551fd41b9b:
  actual5CRTsourceinit_CPUprintfcache/LUT/string/vectors noGPU/device/threadOS
  callbackcreation; normalDllMainVEH/SIGABRT/flushretained. SoleCRTtlscallback/
  XDempty; installedMicrosofttlsdyn sourceff55. HostTLS0. ActualLoad1515below.
- ActualCPU1515PID9616exit0/stderr0/33inputspostMATCH/localpostMATCH; DLLLoad/
  4cases/FreeLibrary succeeded; 116checks31+27+31+27. PSOcopied431/66words after
  shaderDestroy+eraseLIVEbacking (notfreed). CS50block8,8,1; CS51static64,1,1/
  map1/3. Eachcallback1/clonefree1/delete1success0NULL/graphics0/GLSL2balanced;
  cacheS_OKtwice orE_FAILtwice. No actualAsahiBO/GPU/DDIcaps/rootdevice/thread
  lifetime/fullOOM proof. Normal1510inertCode28PackageAbsent remainscurrent.
- GuardedactualCompileCompute DDI29line deltaCOMMITTED dae87ae3/defaultOFF;
  functionbodybyteequalsactualcompiledf338. Target30GREEN/full1621in215.801s
  SAME119failures(noaddedremoved;117baseline+2liveair.lockenvironment), postsource
  MATCH/log549f32980742acab18c3f1c420cbd6afd2ae23cc0152244aed282600fda51396.
  Futurepackageassembly MUSTexcludeinheritedunvalidatedpublisher666abec;
  baseline15f+onlyd962givesf338 whilecurrentHEADfile94b7 retainsoldpublisher.
- NEXTproduction8object/defaultOFFhook candidateSOURCE .local/c15-production-legacy-package-hook-proposal
  READYe3a910cd (10files) / PLAN97794f rootread. RootMUSTfullreview/adopt/test real
  provenance fixtures beforecommit/newfinalsnapshot. SourceMMnormalverifier
  unchanged/newruntimewholecommitfreshqualificationrequired. No1504identityretrofit.
- ParallelC25 SOURCEprobe .local/c25-present-resize-source-prep READY497db9a8;
  meaningfulnewlifetimeboundaryC7singlePresent neverowner-aliveResize. Sixframes/
  2backbufferindices/clearreadback BEFOREPresent/3frames64x64→releaseALLrefs→one
  Resize96x48→3frames. ProbeONLY/no drivercapschange; original1509C21ESKIP retained.
  ROOTsource+primaryreview BEFOREbuildercompile1518/hardware1519; untouchedsigned
  1510package possiblefreshinstallwithsame1506version explicitFactoryoverride,
  prior1510RECOVERY_DONE02c54. No newhardwarelaunched yet. AgentsSOURCEONLY.
- Preserve1514preflightSTOPwrongSELF/1516RCtoolPATHSTOP. ExactfullpathSELF
  finalizer/rootcanonicalP.resolve nowmandatory; SDK26100x64binprimarypath+RCtools
  pinned. No pendingguest/builderprocess, sessions1517/1515 completed.

## CPU1513 COMPLETE — next private native1514 BUILD review NOW

- ActualJ313PID2172exit0/no timeout/stderr0/34inputspostMATCH, local42postMATCH.
  Native45checks/GLSL10acquired10released; twoactualtypedownerCS22/44PASS;
  CS50+SM51factory/NIRserializedparity/maps/WG/nonempty/refusals qualified.
  FourphysicalruntimeDLLsAA64 inventoried; no DLLcopied/package/service/firmware/
  rebootmutation. Same1510inertCode28baselinecurrent. NoCPUrecoveryneeded.
  `.local/c15-legacy-no-core-cpu1513-result/root-review1513.json`
  0739761fef24c0d4dfad6046b603b5293a34199c527554b333231c452c125f99.
  Implementation75d861c173f752cc5147d8579b47d0cbc9d25e70, noDDIrouteadopted.
- NEXT FULLUMD1514sourcebundle READYbd134359 at
  `.local/c15-full-umd-native1514-prep`; execution74702490/ZIPd6e2b647/
  runner7a190c10/controller4cda661a/approval17870095. ROOTFULLREVIEW+BEFORE
  stillrequiredbeforebuilder. 17commands normal1504verifier+10FRESHobjects+
  2evaluations+DLLhostbuilds+2staticinventories; 17archives (normalpropsruntime+
  core5, extraVTN/SIMD+translator9). 732stagefiles/14516nativepins. Sourceprivate
  fullUMD READY027f442a at `.local/c15-full-umd-ddi-cpu-proposal`; exact3d40+
  15fDDIonlyd962=f338/fivefactory75; actualPSO/CompileCompute/cache/EnsureOwner
  realfullgraph. No fakeowner/diag/resolver/private-layoutcopy. Testinputshader
  retired+erasedLIVEbacking, NOTfreed. PrivateDEFonlyexport. ActualnormalDllMain
  processhandlers retained/nottriggered. NOPEloadbeforeCRT/TLS/import/mapreview+
  separateCPU1515BEFORE. No package. AgentsSOURCEONLYfinished, rootoperations.

## NEXT BUILD ACCEPTED: EXP1512

- Source75d861c173f752cc5147d8579b47d0cbc9d25e70 factory/TUsplit committed,
  noDDIroute. LOCAL1511host14/18/19PASS; full1621/119failures=117old+2liveair.lock
  environment during1510; NOTfullGREEN. PostpinsMATCH. Factory43/GLSL10balanced,
  old79/384words, wrapper199/20valid8NULL/689snapshot32NULL/384TWICE.
- EXP1512 actual14RC0/13AA64/12linked/post13806/93hashes/unknown0. Rootreview
  `.local/c15-legacy-no-core-native1512-result/root-review1512.json`
  337c45bc5632fa0e321a0d7f2e939f4e2451aaf08a36172f1c893ec65c714bc8.
  EXE14515712B89545cb7f5de855690b2431d54263dbfc76f962c4bc10fdfd3431e7143693d6f.
  Runtime1500BUILD_ONLYb6a5c5c/props71b8 (same1503CPUlineage), NOTnormal1504;
  originaldraft1504wording supersededcd9edclarification. 14DLLnames same1503,
  MSVCP/KERNEL32descriptororder swapped. FullownerABIobj compiledNOTlinked.
- CPU1513 harness kmdagent preparingSOURCEONLY; DO NOT stage/execute until1510
  NEWEXP_DONE+RECOVERY_DONE/exactAbsent. Use correctedwholeproducerread_bytes
  (1502hostTypeError preserved), 4actualCS50/CS51static/SM6/VS inputs, noGPU.
- Next actualDDIgate plan `investigation/analysis/D3D12-C15-actual-pso-cpu-gate.md`;
  sm51agent privatefullUMDtwoinclude-wrapperSOURCEproposal inprogress.
  ActualCreatePSO/CompileCompute/cache/EnsureOwner+fullowner/logger graph;
  no fake resolver/diag, no copiedprivateownerlayout, DllMain normalprocesssideeffects
  retained, buildCRT/TLS/import reviewbeforeseparateCPUload. Normalruntime1504
  mandatoryforprivatefullUMD; explicit1512BUILD_ONLYsupport separate.

## GPU EXP1510 COMPLETE — RECOVERY_DONE, Package Absent

- ACTUALd65971f4, 59D3D12nonEPASS/noFAIL/C21ESKIP knownUNCOMPLETED. C14A
  counter512/drawn512/threeExecuteIndirectreceipts; C24ABC6subresources165texels
  padding0/C7PresentPASS. 73SMOKEPASS+knownMSAA-a000rFAIL, pool6workers/
  foreignreclaim1/fl11a+x+debugPASS; corpus414each64and4createfail/hang0/pixeldiff0;
  DWM0. GL51.5/internal59.6/CS46.493 singlepoints/no causalperfclaim.
- Signedpackage30.0.1506.0/source3d40/runtime1504normal54437; owningearlyNULL
  repair/a905 qualifiednormalpackage; no hardwareNULLinjection/SM6admission.
  Original1506HOSTSTOP preserved; finalunsignedreceipt28aec fixedwithoutchanging
  signedbytes. Factory1510explicitversionoverride actualscripts/cleanup verified.
- Recovery59502 servicedisableStart4/disarmPASS→orderednormalCode32Stopped→exact
  oem5/INFcfd/SYS50d2/UMD7147cleanupRC0→orderednormalCode28Absent/noService8CPU.
  No ordinaryhidden/manualprereboot/operatoraction. Before09:43:48.5437810Z/
  After09:45:45.9257170Z/durablemanifestEC6DEA55.
  `.local/experiments/EXP1510-arm64-nir-consume/evidence/rec1510h/recovery-final.log`
  02c54cb849694fbd428f144b8646186d3f26ed5a69d549d32ceaac0e7394fc72.
  Rootverdictc7bddb2ee4c5bbe79b5a322a1ffcba0cfdbf7f0aaab8987b3e3a7acda24f7d6e;
  finalartifacts2ed6176273c251eced2d8e4472f48bd0da5507f5cfaa78ad5ef03b432105c44a;
  pre-recovery2123filemanifest3641f77f. PhysicalUSB/RDPnotindependentlytested.
- BIG8textures512MiBverify0/pressure0texturesOOM8007000e removed0 samebaseline;
  intentionalLONGJOBHANG887a0006 followedalivecorpus/desktop/SSH; no unexpected
  Windowsstopobserved. Finalnormalinertguest is current baseline. NEVERrerun1510.

## Latest completed CPU boundaries

- EXP1480 PASS actualnativeWindows ownedPROGRAM->ownedNIR bridge:
 PID10916exit0/no timeout/stderr0/41inputspostMATCH;
 79checks/5accepted6refused/legacyCSVSPS3+mixedDXIL1; actual384goldwords/
192lanes173active19tail346stores/negativeSRVswap,seedloss,wrongslot346each.
 Root/input/program storage legitimate release+overwrite and rootretirement leave
 ownedNIR/constmap/copiedroot independent; valid-again and workgroup64,1,1 proved.
 NativeNIRvalidatorNOOP/upstreamNDEBUG; no ASan/fullOOM/WindowsDDIreadability/capacity/
 provenance/teardown/SM6caps/backend/GPU claim. Prototype remains UNLINKED/outsideproduction.
 ACTUAL8d5f4525; `.local/c15-owned-nir-cpu1480-result/root-review1480.json`
5bfbb002248068126a63d604b6c3b822000aec863058581e7ad710a7528a5544;
 artifactmanifest63507b571f4f2298538d012648ea1233a4f97f7aba032581853cda83ce1bffa2/40files.
 Sourceoriginal `.local/c15-owned-nir-bridge-prep` READY17781974/portable16RC0/
7110post/79checks; realrun7RED uint16[3]->uint32[3]memcpy corrected by elementwise widening.
 Native1476all13commandsRC0/post12997/12AA64/3canonical551fcTU+9opaque;
 wrapperSTOPMSVCP140missingC-onlyallowlist preserved. SeparateLOCAL1479postACCEPT
33550ec8 (14imports exactprior+MSVCP140, actual1421PEprecedent), no recompile.
 NativeEXE14543360B773591dcd388d5c1b59a4427e60e251aafe18b159b5ff9ad2ea3941ed288f8cd.
1480finalPS/controller actualpostcheckbytes-in-str bug caught/fixed BEFOREfirstexecution,
 actualportablelogRED/stringsameoracleGREEN; originalprepfolderpreserved.
- EXP1477 FAIL actualCPUbackend: ownedPID9928exitC000001D/notimeout/inputsunchanged;
 exact52Bstderr `Unhandled intrinsic load_base_global_invocation_id` SHA
c2eb9203ab6c75725e3d9754e9b4c8b6dd89116eb6e012c6231dbb82bc3f0744;
 stdout0 (bufferlost)/NObackendPASS/NOAGXbinary. Both768BgoldarraysBYTEEQUAL;
 pre/post/precompileNIRpersisted; actualprecompile707bda85 containsbaseglobal18–20.
 `.local/c15-backend-cpu1477-result`, ACTUALc2fffa4f/correction84b7d7d1 (52Bnot49).
 Sourcecause: nir_lower_system_values.c210 createsglobal+baseglobal;727–735 lowersbases
 ONLYwithnonnullfalseoptions. ProductionMesa state_tracker st_glsl_to_nir.cpp557–560
 explicitlycallssecondpass, st_program.c385–389 bothfalse; fixtureomittedit.
 WindowsWDKDispatch1776/2383 onlyXYZcounts/no baseAPI; useactualMesapass intranslation,
 backendunchanged. DO NOT conflate thisordinaryCPUfixtureissue withblockedUBFEtask.
- EXP1481 build-only accepted (beforea5be3f4c/ACTUALcb25c987): onlyfixturebase
 normalization+realinvariantsdiff, native9compile+link10RC0/post12774MATCH/61hashes/
 actualincludesunknown0/2positive551fcTU+7opaque. Same13imports/oldwarningsretained,
 linkstderr0. `.local/c15-backend-compute-sysvals1481-result/root-review1481.json`
102a6668f014d47016e0d165feac6cc87250cb94919398dbe4a7a256dfbd7aab.
 Sourceprep `.local/c15-backend-compute-sysvals1481-prep` READY171c210e,
 actualsource054384d8/executionf221da74/ZIP9991420b; helpersroot-onlydiff from1478.
- Earliernative1474linkSTOP11missingownerfunctions resolved by actual7projectproviders
 native_device/native_bo/construction_address/reloc_capture/screen/transport/sharedABI;
1478all10RC0/post12774/rootreview531a5b66. LLD resolvesundefinedBEFOREmarkLive;
 noOPTREF-onlyguess/stubs/oldlibs. 1475STOP0compilersstalenestedrunnerSELF preserved;
 independentoriginalRED/new1478GREEN15nestedstagepins. AlwayscheckTOP+nestedselfafterrebase.

## Next source-only adoption plan in progress

- kmdagent explicitly followedup after1480 PASS: `.local/c15-owned-nir-adoption-next-boundary`,
 boundedprimaryplan for propercurrentprojectmodule/commoncompute-base normalization and
 actualAsahi ownerhandoff; NO tracked edits/Git/remote/guest/execution. Do not wait forever.
 Initialstrongsourcefact: Gallium p_state.h302 says driver takes NIR; actualprojected
 agx_state.c create_shader1987–1990/create_compute2167–2170 returnNULL on FIRSTso
 allocation BEFOREconsume/freeinput, while laterfailure2182 freesNIR. Caller cannotinfer
 cloneownershipfromNULL. Needfix/explicitowningboundary contract before productionhandoff;
 neverpassborrowedNIR or blindlyfreeonNULL. Screen alreadyholdsGLSLref agx_pipe2679->2549,
 bridge ref mustremain throughclone/create. Await final preciseplan, no guess/provenance magic.
- CurrentunvalidatedC15publisher666abec remains EXCLUDED from acceptedGPUsource;
 no SM6model/DDI/cap admission. Provider/ownedcore/checkedconverter/map standalonebounds
 are proven; runtimepointerreadability/CalcCreatecapacity/registration/quiescence/
 requiredstages/resources remainOPEN. Legacy APIs/caps remain truthful baseline.

## GPU EXP1471 COMPLETE — RECOVERY_DONE, Package Absent (DO NOT rerun)

- ACTUAL537fc602/validatedCHANGESef98c550. Package30.0.1471.0/source5e64cc617884ac2767a978b72790e3ec5a5c2b55
 (base1439b5fcb91c + EXACT4helper/projectoroverlays; DDI/winsys/KMD/C24/PBEbodiesbaseline).
 Five-fieldshader_info unsignedstorage, allselectedARM64consumersfresh andcanonical551fc.
 59D3D12ownPASS includingC14A(countbufferdraw512counter512/3indirectreceiptsexact1439),
 C24ABC6subresources165texelspadding0/C21Cmipsbad0/C7APresentPASS;
 C21E remainsUNCOMPLETEDsame23Bubfe, NOALL60/no fixclaim. 2deallocatefailuresafterabort
 alsobaseline2. 73SMOKEPASS+onlyknownMSAA-a000r; pool6workersforeignreclaim1PASS;
 fl11a/x/debugPASS/corpus414each64/4knowncreatefail/nohang/pixeldiff0/DWMfault0.
 BIG8textures512MiBverify0; pressure0texturesOOM8007000e/removed0samebaseline;
 intentionalLONGJOBHANGremoved887a0006 thenlatercorpusalivesamebaseline.
 GL51.2/internal59.5/CS45.388vs1439CS47.253 (-3.95%singlepoint/no causalperfclaim).
 No unexpectedWindowsstopobserved; render/readback/desktopDWM+Explorer/SSH/storagealive;
 final8CPUs, physicalUSB/RDPnotindependentlytested.
- RECOVERY_DONE root10326RC0. FactoryV2wrapperb11f9008/host10f7771d unchanged;
 serviceStart4/disarmPASS -> orderednormalCode32Stopped -> exactpackagecleanup ->
 orderednormalCode28/PackageAbsent/noService/8CPU. NOhiddenboot/operatoraction/prereboot.
 `.local/experiments/EXP1471-arm64-shader-info-unsigned/evidence/rec1471h/recovery-final.log`
764cdb6f5d2efead59e01aa3f24949652a6460fa560d2d6b829fccb6cfe2ac3c;
 durablepreflight1f630ba17d1600e7ee235d93204ef6110d9915351672e19b368f85edaca718fb;
 before04:44:13.6476590Z/after04:46:15.7333360Z/manifest55A71AB7/packageAbsent.
 Final2252filemanifestfa7c4da89b5c49999416569cfa3d0c4b651eeb2b488718dd21c973e9b1c9b432;
 rootverdict542e19e3dd8c36b038781a372bcbe73e12e38162c22cfc2e7c5fc634b59b12ee.
 Normalinertguest remains current throughCPU1477/1480/1482; no GPUpackage.
- Acceptedpackage SYS6378e5a94ea8ad62b0984435803b36734e219c995cc18bfa43813e28d2483284;
 ARM64UMD1b89add05698f74fadcbc2c07b7f9593397631d40565c4067c1e63c9b047dae0;
 INF633ba99c1eb0aa195a6cd6d99affb7c29a4d35436ab37b9320f82d00c1cbbee1;
 CATee258bb54a0abf555e554687b6dc5babba0e37e72c47e51b5a43ea036eeb9b2a.
 WOWICDd51ebf72/WOWUMD1625ffde unchanged; CMSsigvalid/leafE9BE15BD2A184BFABA0C8035B3C620C58037A241;
 buildertestrootuntrustedrecorded, no publicrelease trustclaim. PhysicalstackARM6413824/WOW6544<16384.
 Source698ZIP6ecc9fc2/manifest3fa24bc6; no latepackage-repack/sign inPREPACKEDpipeline.
 Firmware m1n1_windows5b8576031eaae5958c5494d4f22f5a6c3a25d191/macho3e5e9f1f...
 (fullowner/reserveV2/tables4096), Mu361a2134b2f742f331f8d75ae5f60ad4f7f413f1/FD1c7d87d6... unchanged.

## Common source/license and build gates

- Native1466fresh353core/7archives/361RC0/250actual551fcTUs;1469fresh45compiler+154runtime/
203RC0/119actual551fcTUs/postpins12280+280MATCH. Core C:\Users\pauls\EXP1466-core353-unsigned;
 runtime C:\Users\pauls\EXP1469-native-runtime-unsigned. native_runtime4070192Bbada08b8;
 no old/newNIRlibrarymix. Actual1470cachepreprocess store/initempty/retrieveNULL accepted73825c0f.
- Actual1471buildMSBuildRC0/source698PASS preserved; wrapperPOSTfailedrawaliases/GenericList.
1472POST_ONLY actual58aliases/275ownerheaders/3236includes/13573postMATCH/ToArrayREDGREEN/
 physicalAA64+2stackPASS review9743a84a; owneropaque0shader_info includes reportedhonestly.
1473CAT-onlysigning samebaseline completedafter realJSONarrayreaderfix; oldSTOPbeforeanysignretained.
- Commonnotices `.local/c15-backend-cpu1477-notices-final/NOTICES-MANIFEST.json`
 c33530e96df286a6a052a75e4e1b1757c9688e7dbfdc85fb6bcc416ef70549cd/18members+manifest,
 MIT/BSL/KhronosApache/Blake3CC0/projectMIT/fullattributions/noimplementationbodies/unresolved0.
 Threehelpersproved `.local/c15-backend-notice1477-primary-read`: full3builderSHA/prefix
 PID42988RC0; lutIntel2025MIT/libagxMesaContributorsMIT; agx2generatedEXACT92694B/d5e1e0d4
 fromprimaryMIT gen-disasm.py/AGX2.xml/isa.py. InitialgenerationmissingMakoSTOPretained;
 successfultemporaryMako1.3.10+MarkupSafe3.0.2 OUTSIDErepo `/private/tmp/c15-notice-generation-tools-1477`,
 no runtime dependency. No furtherlicensearchaeology.1480alsoincludesreviewed1421native9notices.
- Full1463hostsuite1617/219.389s EXACT117old67fail50error18skip/noaddedremoved/13postpinsMATCH;
 NOTwhole-suiteGREEN. Originalprojector189c08a0/source-rootbuilders e4433190 qualifiedGPU1471.
 No broadtestrepeatabsentnewchange/failure. tests/test_change_ledger2PASSafterrows; append40charcommits.
- RootTandem airssh/airscp `/Users/pavel/public_windows/.local/tandem`, guestpavel@192.168.1.37.
 Builderpauls@192.168.1.24/key~/.ssh/windows_builder. Bareairaliasunconfigured.
 BeforephysicalaskcheckguestSSH ANDproxy/vUARTUSB/ownedlauncher; neveraskalreadyRunningproxy.
 Normalvisible recovery ownsorderedreboots/disable/exactpackagecleanup; no manualprereboot/fakePID/
 broadforeignkill/ordinaryGPU-hiddenboot. Pinnedhost10f requiresownexactlauncheror guardedNO_HOST_OWNER.
 Old1450orphan41676 killedonce; PIDlaterreusedandclosed, NEVERkillstalePID.
 NativeSELF+input_pinsnestedhelper+stageentries+ZIP/localpins mustmatch AFTERfinalrebase.

## Latest proven boundary: actual CPU NIR semantics and enum defect

**EXP1456 PASS**, ACTUAL `291acf93`, CHANGES `10622012`:
- Actual J313 ARM64 EXE2309632B SHA
  `62bc9637caeabe55ee70669dc454de6ab1b3f32010d849afd4bb62208ac22200`.
  Exact24e3 SPIR-V2152B + seven original1397 vectors. Actual VTN -> NIR ->
  original project map -> restricted actual-NIR interpreter; 399 active checks,
  7 structural refusals preserve NIR/map,192lanes173active19tail384gold words,
  346stores; both768B outputs byte-equal host gold. Sensitivity346each.
- Exit0/stderrEMPTY/no timeout/hashMATCH; NDEBUG preserved, native
  `nir_validate_shader` NOOP explicitly reported. No backend/GPU/DDI/caps claim.
  Physical kernel32/VCRUNTIME140/ucrtbase inventories captured; no DLL copied.
- Raw `.local/c15-native-map-cpu1456-result`; artifact manifest
  `fc27af67115b1b5185fa57965f693365362c12d8515452a56950db78de331b55`.
  Result `f58840ef3f89ace8a285d13ab51fea8fff6401e331242bbc907b2265466b26dc`.
  Session78434 finished. No package/service/firmware/reboot mutation.

**EXP1457 CONFIRMED four-field enum ABI defect**, ACTUAL `d359a387`:
- Actual owned volatile shader_info: depth/stencil-front/stencil-back4 -> -4;
  derivative2 -> -2. Full enum copies remain negative; unsigned4294967292/94.
  NONE control PASS; sizeof(shader_info)440/full enums4. Exit1/FAILfailed4 is
  valid causal measurement, not transport failure; stderrEMPTY/hashMATCH.
- Raw `.local/c15-enum-abi-cpu1457-result`, artifact manifest
  `cc81b97b4d9cb7001d17e1ec7cc24fe1f91152a071031108a25dd864d11f0b4d`.
  Session96696 finished. No guest/GPU change. Fifth hash_type:2/DXBC2 was subsequently confirmed by1460 above.

## Owner remedy / source-only status

- Reviewed plan `investigation/analysis/D3D12-C15-native-enum-abi-remedy.md`,
  commit01f1b667/process rowaf2eb664;1460supplement5925f35d. Owner: shared Mesa CPU metadata storage
  under Windows compiler ABI, not WDK/KMD/firmware or a downstream getter mask.
- Exact Mesa9aa1215f878b504f66159dd2ead4c7973142126e header23753B SHA
  `893b5a63f7dd0fbaf1d96953451fb073e35fd38acd8be60a51528e2f37204a09`
  matches actual native pin. Root read Intel2016MIT notice: modification allowed
  with full notice retained; external header bodies stay outside Git.
- sm51 private four-confirmed-field prototype `.local/c15-enum-abi-four-field-prototype`:
  READY83214da5, original projector25df4f33 (exact SHA+unique anchors, MIT prefix,
  hash_type untouched, refuses existing output), actual-header C/C++ cross-TU
  fixturea1748475 with all legal values, neighbors/raw masks/layout. SOURCE ONLY;
  no projection/compiler execution yet. Five-field candidate executed1461/1462 and GREEN within scope above.
- Must prove C++ member-type compatibility and old/new packing/serialization,
  not just sizeof440. `nir_serialize.c` stores raw sizeof(shader_info). Rebuild
  ALL actual header-dependent NIR/compiler/Asahi/VTN/runtime/owner TUs from one
  consistent header; no patched-VTN/old-NIR mixture. Compiled shader cache keys
  also require actual namespace/invalidation proof; raw-byte equality alone is
  insufficient. sm51 CACHE_AUDIT source note exists. No guessed compiler flags.

## Native build evidence supporting exact CPU checkpoint

- EXP1451:19rc0/8414 unchanged pins/16actual include closures/AA64/imports.
  VTN4810858B SHA994b7cfec9ed1dfd540aff97fbdf298a8c7ea43f934c581a820a7e652823a9a3;
  SIMD6610B SHA1465f0c02604ad940a1ce87067fc899657339edc757e9340d965ce589877f838.
  Output112file manifest55dddf5b; root review31b249f5 accepts build closure ONLY
  for exact24e3 GLCompute/LocalSize64,1,1 module, no flagged modes. Never run
  noop frontend-link.exe as a semantic checkpoint.
- EXP1453: actual map+fixture2C/link3rc0/8423pins/2actualdeps; corrected missing
  project dxbc include after1452 STOP. Manifest1a0b7df5/root reviewe81f8d59.
- EXP1454: execution-policy refusal before payload/EXE, noout. Preserved.
  EXP1456 used process-local Bypass only. T/airssh and airscp required; bare
  ssh alias `air` is unconfigured. Runtime wrappers capture raw Process streams.
- EXP1455: original four-field enum probe d6cddc7b, native2rc0/8388pins;
  EXE25088B0d3ff9c7; manifestd025bd67/root review3e856189; executed1457.

## GPU baseline: EXP1439 COMPLETE / exact package removed

- ACTUAL+validated CHANGES4030799a. C24 sourceb5fcb91c/package30.0.1439.0.
  59unique non-E D3D12 steps ALLPASS incl C24ABC/C23ABC/C14A/allC14/C21A-D/
  C22AB/C7A; C24arrays6subresources165texels/paddingbad0. C21Cmipsbad0.
  C21E UNCOMPLETED exact23B `Unhandled ALU op ubfe`, not ALL60 or compiler fix.
- 73SMOKE+onlyknownMSAA-a000r; GL52.6/internal59.5/CS47.253fps;
  poolworkers6/foreign_reclaim1PASS; fl11a/x/debugPASS; corpus414each64/4createfail,
  nohang/pixeldiff/DWMfault0. No observed unexpected stop.
- RECOVERY_DONE Code28/PackageAbsent/noService/8CPU; FactoryV2 host10f7771d
  explicit guarded NO_HOST_OWNER/liveSSH cleanup. Raw durabilitya28a3ea4;
  final2259filemanifest9f33ffcd. Normal GPU-visible inert guest remains current.
  Physical USB input/RDP not independently tested. Do not rerun recovery now.
- Normal recovery host10f requires exact owner or guarded explicit no-owner,
  owns ordered reboots. No manual prereboot, fakePID or ordinary GPU-hidden boot.
  Both guestSSH+USB/launcher before requesting physical action.

## Continuation constraints and paths

- Builder192.168.1.24 pauls/key~/.ssh/windows_builder. EXP1450 proved old22:55
  broad readonly PowerShell41676 orphaned after SSH timeout, consuming104GB.
  Exact command hash/source/time matched; native taskkill ONLY41676 succeeded,
  post freephysical23.6GB/freevirtual21.2GB. User intervention no longer needed.
  Never repeat broad recursive inventory; finite metadata only. Local SSH timeout
  does not necessarily kill remote process. No foreign/broad kills.
- Global `.local` usually `/Volumes/pwdev/public_windows/.local` or Users alias;
  read exact approval receipt path. Normalize prepared_root to resolved path.
  Native self-pin must match actual runner bytes AFTER root rebase.
- m1n1_windows5b857603/Mu361a2134 user dirty preserved. Skill/source-first,
  BEFORE/ACTUAL and CHANGES40char/RFC4180/test_change_ledger required.
- Full baseline: latest1612suite117existing67fail50errors16skip/noadded failures,
  two old serial-lock environmental failures removed; no whole-suite GREEN.
- C15 root key873c7f34/provider phasesd8cfe7d0/NIRmape0ff8fff/checked owned
  adapter751a950e remain UNLINKED. License/composite8434+df3470/197inputs/9archives
  reviewed; no production SM6 model list/DDI upgrade. Coherent admission debt
  (GS/SO/HSDS etc), runtime capacity/readability/library owner integration remain.
- Security review previously rejected C21E/UBFE and fault-sweep delegation.
  Do not retry identical tasks or route around via model/agent/security setup.
  Ordinary nominal CPU/header/getter regression tasks are separate authorized work.
