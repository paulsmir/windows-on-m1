# J313 GPU: CS 1.6 menu renders on the AGX OpenGL ICD (EXP1163)


## Current as of 2026-10-10 14:55Z (read this first)

CS 1.6 (32-bit hl.exe, Half-Life directory untouched) renders its main menu
on AGX: GL_RENDERER "Apple M1 (G13G)", GL 2.1 Mesa 26.3 (EXP1163). The x86
ICD is installed by the driver package as the adapter's OpenGL ICD:
driver-store AppleAgxOpenGL32.dll, OpenGLDriverNameWow/VersionWow/FlagsWow
plus a native OpenGLDriverName/Version/Flags entry (ARM64 ICD not built;
ARM64 GL callers fall back to GDI Generic). Microsoft's opengl32.dll loads it
for any x86 GL app (x86 smoke without opengl32.dll beside it: 57-60 fps).
Accepted package: PACKAGE1163 (.local/experiments/EXP1163-cs16-icd-nomsaa):
PACKAGE1151 sys 407bffbc / UMD bbe106b3 + INF f7db56b7 + ICD 57aa7e18.
Contracts learned (do not relearn):
- DriverVer must equal the KMD VERSIONINFO, else dxgkrnl DpiAddDevice fails
  Code 31 0xC0000182 (EXP1156-1159); re-catalog only with
  render-admission/scripts/repackage-driver.ps1.
- dxgkrnl reads OpenGLVersion/Flags(+Wow) only with a native OpenGLDriverName;
  opengl32 declines an ICD reported as version 0 (EXP1160/1161).
- WGL contexts share one screen/G4 context: residency sets of other contexts
  are retired, not refused (7b034fd0, EXP1162).
- The G4 builder is single-sample: the Windows screen sets AGX_DBG_NOMSAA
  (0eddfcf0, EXP1163).
- A private opengl32.dll beside hl.exe is never used (EXP1155).
Recovery: standard recNNNNh; an adapter that never starts leaves G3Armed
unconsumed (clear it after evidence: arm-clear-if-unstarted.ps1).
Guest C: free must stay >= 4 GB (stage gate); traces archived in
.local/experiments/guest-trace-archive-20261010.
Next causal targets: map load / gameplay rendering, frame rate, fullscreen
(operator saw a black fullscreen; the KMD exposes only 2560x1600), MSAA in
the G4 builder, ARM64 ICD.


## Current as of 2026-10-10 09:15Z (read this first)

OpenGL on AGX works from a 32-bit x86 process (EXP1149): x86 ICD
(libgallium_wgl.dll = Mesa GL stack + x86 Asahi closure + umd_* code +
GPUVA D3DKMT bridge, drivers/apple-agx/windows/icd, plan
docs/superpowers/plans/2026-10-10-cs16-opengl-icd.md) ran agx_gl_smoke in
FBO mode: 300 frames 59.9 fps, all pixel checks correct, desktop unaffected.
Driver package for all ICD runs: EXP1143 (unchanged).
Fixed on the way: window colour buffers without DISPLAY_TARGET (EXP1146),
Windows fences + fence callbacks (EXP1147), Gallium clear masks and
flush_resource = flush writer (EXP1148).
Blocker for CS 1.6: the G4 KMD builder (shared/src/apple_agx_g4_builder.c
AppleAgxG4BindNativeObjects) refuses every ZLS field and the UMD finish
refuses batches with a zsbuf; Mesa WGL offers no depth-less pixel format.
Next causal target: depth/stencil (ZLS) mapping from the Asahi render
command into the G4 job template, then hl.exe -gl with the private
opengl32/libgallium_wgl pair in the Half-Life directory.

## Earlier: 2026-10-10 06:47Z

EXP1145 (profile, same package): the same-size GDI drag's extra two-period
frames come from win32k recreating the window's redirection bitmap on
every SetWindowPos without SWP_NOSIZE (RecreateRedirectionBitmap, CPU
EngCopyBits) while DWM waits on the GDI semaphore and reopens the surface;
no AppleAgx frame on that path. DWM per frame there: 6.5 ms running,
2.3 ms retire waits. Typing capture was perturbed by the stack walk
(inconclusive). Desktop status: all loads 57-60 fps, 94-99 % one-period,
24-minute soak clean (EXP1144).

## Earlier: 2026-10-10 06:35Z

EXP1144 soak (same package as EXP1143, four load cycles, ~24 min, 12
Settings cycles): no hang/TDR/DWM restart, flips identical in every cycle,
private pool bounded, DWM memory flat. Remaining stutter: typing 94 % and
same-size GDI drag 91-94 % one-period (DWM-only loads 98-99 %). Next:
EXP1145 profile of exactly those two loads.

## Earlier: 2026-10-10 05:55Z

Best validated launch: exp/1143-build 5d8fe283 = exp/1141-build + UMD two
submissions in flight (df628fdc) + per-process private quota 246 units
(5d8fe283), m1n1 98cdabae. Integration f0d1c5af has the identical driver
tree (two-in-flight is the default again). EXP1143: Notepad drag 57-59,
typing 57-58, charmap 60, same-size drag 56-58, one-period pacing 94-99 %,
private pressure 0, no rejects/TDR, Settings cycles clean.
Note: every exp/1122..exp/1142 hardware build carried a 128-unit private
quota (integration's 192 never reached that lineage).
Proven: the GDI-window regression of two-in-flight (EXP1139/EXP1140) was
private-scene quota pressure (EXP1142 vs EXP1140), fixed by the quota.
Remaining DWM cost: 14099 own-job waits, 10.8 s in 382 s (0.7 ms each),
paging waits ~0.7 s; Notepad still creates a surface per frame (RichEdit
D2D DC render target).
Next causal target: the remaining Notepad drag/typing gap below 60.

## Earlier: 2026-10-10 05:40Z

Best validated launch: exp/1141-build 0a6904fa = integration KMD (phase 5a
+ counters, phase 5b, escape JobEvent b40a7c29, GDI-pending c24ec6ce) +
UMD destroy drain, UMD one submission in flight, m1n1 98cdabae. EXP1141:
Notepad drag 51-54, typing 57, charmap 60, same-size drag 52-54, no
Settings blank, app devices close clean, 0 rejects/TDR.
Two submissions per context in flight (UMD e4398e22, reverted again on
integration): EXP1137 regressed (timer-polled private-scene waits, EXP1138);
with b40a7c29 (EXP1139) Notepad drag reaches 59.7-59.9 (98 % one-period)
but GDI windows stay regressed (charmap drag 44 vs 60, same-size drag 42
vs 53). EXP1140 charmap capture: no dominant wait; candidates: the copy
escape waiting for the process's own in-flight job (R157 rule, no
documented hazard) and retire ordering.
Next: single-in-flight charmap capture as the comparison, then decide on
the copy-escape rule.

## Earlier: 2026-10-10 04:05Z

Best validated launch: exp/1136-build 51c33ae7 (phase 5a + submit-path
counters + 2ebf1a96 destroy drain) with m1n1 98cdabae. EXP1136: app devices
close cleanly (0 device-terminal/runtime-set-error lines), flips as EXP1135.
In test: EXP1137 = phase 5b (875de913: second G4 job per context, two
private marker slots per context) + UMD two-in-flight (59ec6c38).

## Earlier: 2026-10-10 03:40Z

Phase 5a (KMD cross-context queue) is in: e9474244 + 8e2e43b3 (deferred
path also while the worker finishes; Wom1SubmitPath1135 counters).
EXP1134/EXP1135: functionally clean, flips unchanged; AwaitWork waits went
from ~105/s (EXP1130) to 1 per run, 10 % of submissions deferred. The GPU
is idle when 90 % of submissions arrive: the limit is each process's
serial wait for its own previous job. Next: phase 5b (two jobs per context:
FenceOutstanding -> oldest + count, private preempt/cancel per scene) and
then UMD two-in-flight (e4398e22) on top. App DestroyDevice drain fix
2ebf1a96 (EXP1133 device-terminal) is committed, not yet run.

## Earlier: 2026-10-10 02:20Z

State: GPU-visible baseline after each run. Best validated launch:
exp/1132-build f7ef24a7 (b3e2d5bf + display ring + UMD lifetime lines +
0dd8bb24 forget freed slot + 421d5110 ring filter) with m1n1 98cdabae.
EXP1132: no black screen on Settings close (no visibility-off/ModeChange,
DWM keeps flipping, no DWM device rebuild); Notepad drag 52.2-53.3,
typing 56.3-57.2; no TDR/rejects.

Proven since 23:55Z:
- EXP1130 ETW: DWM's composition thread waits ~4-5 ms per frame for the
  previous submission to retire (UMD retire_held / PresentationRotate /
  CopyFrontToBackBuffer) and runs ~7 ms CPU per frame.
- Settings-close blank root cause (EXP1130 ETW + EXP1131 receipts): DWM's
  PresentationDestroy deallocates a presentation resource, then the native
  BO's Direct screen slot evicts the dead handle; dxgkrnl marks the device
  as removed (DxgkEvictInternal -> VidSchMarkDeviceAsError) and DWM runs
  CD3DDevice::ProcessDeviceLost: full device rebuild, visibility off ->
  ModeChange primaries, normal flips after 0.9-1.3 s. Fix 0dd8bb24 (slot
  forgets a handle the runtime deallocated), RED/GREEN test.

EXP1133 (02:50Z): re-applying UMD two-in-flight (e4398e22) on the
single-slot KMD is REJECTED again (Notepad drag 53 -> 41, charmap drag
60 -> 44, two-period frames 13 -> 43 %); reverted (5ecae261). Overlap needs
a KMD queue where one context's renders and presents queue in fence order.

New evidence (offline, EXP1130 ETW): Notepad's per-frame shared texture is
created by RichEdit's D2D DC render target (riched20 ->
D2DDCRenderTarget::BindDC -> DCPresenter::BindDC -> CD3DSurface::Create),
and each creation waits synchronously for paging (AgxWin32GpuvaBind ->
wait_paging_at).

Open, causal order:
1. App DestroyDevice terminalizes (EXP1133 device-terminal): stage Closing,
   the kernel context is destroyed first, then AgxWin32AsahiContextRetire
   refuses because a batch is still active (unsubmitted); 48-63 BOs stay in
   UMD bookkeeping per destroyed device. Invisible; memory leak in AFH.
2. Multi-job: KMD queue depth 2 (submit stops blocking; worker binds the next
   entry) then UMD two submissions in flight (re-apply e4398e22). Site map:
   docs/superpowers/plans/2026-10-09-multi-job-firmware-queue.md "Site map" (per-context Private/Preempt/CancelFence and
   FenceOutstanding must become per entry).
3. Notepad per-frame shared surface (~50/s).


## Current as of 2026-10-09 23:55Z

State: GPU-visible baseline after each run. Best validated launch: driver
exp/1126-build b3e2d5bf (EXP1122 + 85e2b4b9 coalesced paging + f1d5ba47
allocation receipt + b9e33767/96e21de1 UMD measurement + ffbc0772
EvictOnlyIfNecessary + 66e57c27 retained local grants) with full-owner m1n1
96a0c1ef (fa3356ee + local-reserve broker fast path; macho becec9cd,
built natively: make -j8 IOMFB_FULL_OWNER=1 AGX_LOCAL_RESERVE_V2=1
TOOLCHAIN=/opt/homebrew/Cellar/llvm/22.1.8/bin/ LLDDIR=/tmp/agx-lld-dir/,
rustc on PATH; the same command reproduces fa3356ee bit-for-bit).
EXP1127 flips: Notepad drag 52.6-53.4, typing 56.9-57.1, cursor 55-60,
charmap 60, same-size drag 51.5-52.5. No TDR/rejects/HV exceptions.

Proven since 22:55Z:
- EXP1125 ffbc0772: DWM's borrowed-surface release no longer forces VidMm
  to page Notepad's shared surface out (VIRTUAL_TRANSFER 0/s).
- EXP1126 66e57c27: unmapped shared local grants stay registered
  (REG_SHARED/REVOKE ~3500/s -> <200/s).
- EXP1127 m1n1 96a0c1ef: UPD_LEAF 13 -> 4.3 us; broker 162 -> 76 ms/s.

Open, causal order:
1. Notepad still creates a 1032x581 shared surface per frame (~50/s, ~27 ms
   lifetime): VidMm fill + 2-4 UPDATE_PAGE_TABLE per surface (~70-80 ms/s).
   Why DComp/XAML does not reuse it is unknown (driver capability?).
2. Single render slot / multi-job phases 4-7.
3. Same-size SetWindowPos drag 52; charmap and cursor at 60.


## Current as of 2026-10-09 22:55Z (read this first)

State: GPU-visible baseline after each run. Best validated launch is still
exp/1122-build af4245a3 (EXP1122). EXP1123/EXP1123R/EXP1124 (exp/1124-build
365fe5c5 = EXP1122 + 85e2b4b9 coalesced local paging + f1d5ba47 allocation
receipt + b9e33767 UMD measurement) are functionally clean; Notepad drag is
40-42 in all three coalescing runs vs 46-47 in EXP1122 (not attributed).

Proven since 21:50Z:
- Coalescing: a 2.4 MiB fill is one record/pass (fills 425 -> 38 builds/s).
- EXP1123 run 1 ended in an abrupt whole-machine reset (no HV, Windows or
  firmware evidence, Kernel-Power 41 only); the identical rerun EXP1123R
  passed after 25 min idle -> intermittent platform reset (thermal unproven).
- Per-frame churn source (EXP1124): Notepad creates a 1032x581 B8G8R8A8
  RT|SR MISC_SHARED texture (~37/s, not a swap-chain buffer) through
  AdmissionUmdCreateResource; VidMm zero-fills it, maps it into Notepad and
  DWM, then pages it LOCAL_TO_SYSTEM ~34 ms later because DWM's borrowed
  Direct screen buffer drops its EXP978 residency reference with a plain
  Evict (D3DDDI_EVICT_FLAGS: "finished, evict at first opportunity"); dxgkrnl
  destroys it 3-5 ms later.

Open, causal order:
1. EXP1125 (ffbc0772): EvictOnlyIfNecessary on the borrowed release; expect
   the per-frame VIRTUAL_TRANSFER and system-page remaps to disappear.
2. Remaining per-surface cost: 592-PTE UPDATE_PAGE_TABLE maps/unmaps in the
   app and DWM processes (~1 ms each, 148 broker UPD_LEAF calls per update).
   A multi-group broker publication would need an m1n1 broker change (ask
   the user before changing m1n1).
3. Notepad drag 46 -> 42 with coalescing: needs one A/B (EXP1122 artifacts
   vs EXP1123) if it persists after item 1.
4. Multi-job phases 4-7.


## Current as of 2026-10-09 21:50Z (read this first)

State: GPU-visible baseline after each run. Best validated launch: driver
exp/1122-build af4245a3 (EXP1119 multi-job phases 1-3 + 083347d4 slot event
+ 5de7675f export rate + ca8fd4f3 census + 0df483b0 CPU-only paging-process
page tables) with m1n1 fa3356ee. EXP1122 flips: Notepad drag 46-47, typing
50-51, cursor 55-60, charmap 60, same-size drag 52-54. No TDR/rejects.

Proven since 19:05Z:
- Multi-job phases 1-3 (EXP1117-EXP1119): firmware-owned stamps, two
  per-job object slots, completion by event + stamp + retired entries.
- Submitters wait on a render-slot event (EXP1121, 1b7a92d9): queued
  cross-process gaps 9.7-10.5 ms -> 0.25-0.4 ms.
- VidMm churn (EXP1121 census): Notepad makes VidMm transfer
  (LOCAL_TO_SYSTEM) and fill whole 0x250000-byte allocations; the rate
  scales with frames (~38/s each at 50 flips/s, EXP1122).
- The paging process needs no GPU page tables (EXP1122, 0df483b0; MS "System
  paging process"): broker 385 -> 186 ms/s, Notepad +11-13 flips/s,
  Complete->JobEnd >1 ms stalls 11 -> 1 per 128 jobs.

Open, causal order:
1. Why VidMm transfers/fills a 2.4 MiB allocation every frame: EXP1123
   receipt Wom1AllocLife1123 (creator pid, type, class, flags, lifetime)
   joined with the census (scratchpad alloclife.py). Fix the owner layer.
2. Paging passes: EXP1123 coalesces contiguous local records (85e2b4b9);
   LOCAL_TO_SYSTEM destinations stay one MmMapIoSpace page per record.
3. Remaining broker time: user-process system-memory grants (REG_SHARED/
   REVOKE ~3400/s each while typing).
4. Multi-job phases 4-7 (docs/superpowers/plans/2026-10-09-multi-job-
   firmware-queue.md) after the churn is understood.


## Current as of 2026-10-09 19:05Z (read this first)

State: GPU-visible baseline after each run. Best validated launch: driver
exp/1115-build f814128e (EXP1111 contents + receipts 318d8cfa/bfb35d80/
b0bb4883 + be9b4ee7 dynamic TVB heap) with m1n1 fa3356ee. Flips unchanged
from EXP1111 (charmap 60, cursor 55-60, same-size drag 51, Notepad drag 34,
typing 38-39).

Proven since 15:45Z:
- Present lead (EXP1113, m1n1 10ad59c0 receipt): the DCP applies a swap only
  if swap_submit finished >= ~1.5 ms before vblank (firmware); m1n1's
  A407+A408 RPCs add ~0.25 ms. Per job: kick -> TA end ~175 us, 3D ~140 us,
  finalize until CPU-visible ~100 us.
- Private memory (EXP1114 receipt Wom1G3PrivateProcs): every process held a
  32-block (4 MiB) TVB heap = 64 of its 82-128 units; pool 884/1024 units.
- Dynamic TVB heap (EXP1115, be9b4ee7, Asahi ensure_blocks): fixed heap VA
  window, backed blocks = max min_tvb_blocks of the process's renders, grown
  idle, InitBM on growth. Firmware accepts 8/24-block managers; pool 522
  units, overflow 0, no regressions.

EXP1116 (two submissions in flight + 192-unit quota on the dynamic heap):
memory held (pressure 0, pool 540), but the KMD runs one packet at a time
and DWM's queued second submission took the slot ahead of apps (Notepad
drag 34 -> 24, typing 38 -> 31; same-size drag +4 %). Reverted (0298f267);
the 192-unit quota (30db3a4d) stays.

Open, causal order:
1. One job slot (KMD RenderPacket + one G4 arena/backend image + one
   firmware queue entry): TA of the next job cannot overlap 3D of the
   current one, and dxgkrnl serializes all processes through it. Next:
   source-first design of a multi-entry firmware queue (Asahi queue/mod.rs,
   workqueue.rs; m1n1 cmdqueue) with per-job images.
2. Scene size: full-screen scenes are 11 units (TPC 5, user buffer 2);
   Asahi shares one TPC per buffer and uses a 0x80 user buffer on one
   cluster.
3. Notepad (WinUI) frames ~34/s: two processes share the job slot.
4. Present lead: pre-issue swap_start (~0.12 ms) in m1n1.



## Current as of 2026-10-09 15:45Z (read this first)

State: GPU-visible baseline after each run. Best validated launch: driver
exp/1111-build ce249e01 (EXP1106 phase-locked vsync + EXP1109 trace
overhead removal + 16 MiB direct buffers) with m1n1 fa3356ee. Guest
environment: EXP801DxgBoot AutoLogger Start=0 (stage scripts require 0;
re-enable via evidence/EXP1108/EXP801DxgBoot-autologger.reg for crash
tracing).

Measurement protocol changed (EXP1108 correction): the old drag loads passed
the window size to SetWindowPos every step, which reallocates the GDI
redirection surface; use position-only moves (gdimove-nosize, npmove-nosize)
as the primary metric. EXP1111 (30 s each): charmap drag 60.0 flips/s,
cursor-only 55-60, same-size SetWindowPos drag 51, Notepad drag 38-39,
Notepad typing 38-39.

Proven since 14:05Z: phase-locked vsync (EXP1106, 60 notifications/s),
diagnostic overhead off DWM's thread (EXP1109: frame-arm registry receipt
rate-limited, UMD trace batched), mapped buffers up to 16 MiB direct
(EXP1111, no copy escapes for them).

Open, causal order:
1. Notepad (WinUI) frames: the app renders during a pure move; two processes
   share the single GPU job slot; ~half of its frames take two periods.
   Next: trace Notepad + DWM job interleaving and remaining escapes.
2. DWM frame GPU chain: ~6 sequential job round trips per frame (~4 ms of an
   8.5 ms frame); per-job firmware ~410 us (kick->TA end ~330 us).
3. m1n1/DCP present lead ~2 ms (presents 0-2 ms before vblank miss):
   synchronous A407/A408 DCP RPCs and per-present printf in the HV exit path.
4. a9c5c811 measured with EXP1109 (built in); host resets without PSCI
   (EXP1095, EXP1011): none since.


## Current as of 2026-10-09 14:05Z (read this first)

State: GPU-visible baseline after each run. Best validated launch: driver
exp/1105-build b0578618 (EXP1101 receipt build 4a050c93 + 57e4523d +
3c48bbb9 + 27ff51e5) with m1n1 fa3356ee. Burst job Submit->Notify 671 us
(EXP1092 1.18 ms), charmap drag 36-39 flips/s, Notepad 25-27, stable.

Proven since 11:15Z:
- 0d22b8bc aligned 8-byte copies of per-job firmware templates (EXP1099,
  BackendBefore->Kick 340 -> 58 us).
- 23f52b1b RegionC idle_to_off_delay_ms 2 -> 20 (EXP1100: GPU power-up no
  longer paid per frame; Submit->Notify 1155 -> 814 us).
- 1581e187 HwDataA P-state receipt (EXP1101: DWM jobs at state 1); a DVFS
  floor at state 3 (EXP1102) changed nothing: the ~330 us kick->TA end and
  the 78 us 3D phase are fixed firmware/GPU latencies. Reverted.
- Every G4 job rematerialized the 6 MiB write-combined template arena twice
  (~100 us each). 57e4523d + 3c48bbb9: the G4 bind skips Prepare on a
  pristine image (EXP1103 failed: StartDevice wrote the VM slot raw after
  Prepare; fixed by selecting through the image). EXP1104: Submit->Worker
  155 -> 51 us. 27ff51e5: the G4 release restores objects 0..42 and 63 from
  a StartDevice snapshot (EXP1105: JobEnd->Notify 109 -> 35 us).

Open, causal order:
1. The flip rate did not follow the per-job gains (~37/s throughout
   EXP1100-EXP1105): flips land on every 1st-3rd vsync; DWM spends ~4.9 s
   of a 15.7 s drag in CMonitorClock::WaitForNextTick (cs1100). Find what
   makes a frame miss its vsync (present/flip path, DWM pacing).
2. Worker hand-off after > 1 ms idle: Submit->Worker ~110 us (EXP1105 idle
   jobs) vs ~50 in bursts; the first job of each frame pays it.
3. a9c5c811 (UMD trace configuration read once) built but unmeasured.
4. Host resets without PSCI (EXP1095, EXP1011): watch for recurrence.


## Current as of 2026-10-09 11:15Z (read this first)

State: GPU-visible baseline after each run. Best validated launch: driver
source 43e68a36 (exp/1092-build: EXP1090 + O(1) BO handle lookup + nonpaged
vsync query) with m1n1 fa3356ee (local branch nvme-cq-partial-ack, not
pushed; build with brew llvm 22.1.8 + rust 1.97.1 + LLDDIR=/tmp/agx-lld-dir,
which reproduces the deployed 824ea32d byte-for-byte). EXP1098: Notepad load
24-26 flips/s, charmap drag 35.5-36.4, Settings renders, no rejects, zero
stornvme resets. Launch with WOM1_PCPU_PSTATE=12. Probe protocol from EXP1097
resets Notepad's saved session first (np-reset.ps1); earlier runs opened
more windows each time, so EXP1092-EXP1096 flips/s are not comparable.

Proven since 07:42Z:
- c4446277 write-watch staging (EXP1089), 01553885 direct dynamic maps
  (EXP1090, +30 %), 48eb2167 handle-indexed BO lookup (EXP1092).
- 49686f09: the vsync diagnostic escape bugchecked 0xD1 (pageable escape
  buffer written at DIRQL, EXP1091); snapshot now goes through nonpaged pool.
- m1n1 NVMe INTx: an 8-command synchronous batch raises INTx at its first
  CQE and the model notified once per assertion; partial CQ-head
  acknowledgements and EOIed-but-unconsumed notifications stranded CQEs
  until stornvme's 10 s reset (System 129, in every run EXP1089-EXP1097,
  before the EXP1091 and EXP1095 failures). fb4102c5 + fa3356ee renotify
  (partial ack; still asserted 10 ms after an EOI). EXP1098: zero resets.

Rejected:
- Two submissions in flight (c545a3d2, EXP1093/EXP1094, reverted 34e43386):
  completion waits fell 4.37 -> 3.03 s per 15.7 s, but DWM's 8 MiB private
  budget (66-unit buffer manager + 7-11 units per scene) thrashed, and a
  12 MiB quota exhausted the 64 MiB global pool (SystemSettings
  D3DERR_OUTOFVIDEOMEMORY). Revisit only with a smaller per-process buffer
  manager or a larger global pool.

Open, causal order:
1. Per-job GPU latency (EXP1092 Wom1JobTiming971, median Submit->Notify
   1.18 ms): BackendBefore->Kick3d 340 us inside AppleAgxBackendRuntimeSubmit
   (Resolve/Relocate/AppleAgxExp208BuildJob), BackendAfter->FirstProgress
   381 us, Submit->Worker 169 us, JobEnd->Notify 110 us. Next: a sampled
   profile (sample.wprp, scratchpad sample_agg.py) of the charmap drag.
2. a9c5c811 (UMD trace configuration read once) is built but unmeasured
   (EXP1095 ended in an unexplained host reset).
3. KMD WriteBinary on escape paths, staging hash (~6 % of DWM samples).
4. Host resets without PSCI (EXP1095, EXP1011): re-check after the NVMe fix.

## Current as of 2026-10-09 07:42Z (read this first)

State: GPU-visible baseline after each run. Best validated package: EXP1087
(exp/1087-build b55e214f = EXP1083 source + KMD private scene cache, LRU):
charmap drag 20-24 flips/s, Notepad load 12-15, Settings renders, stable.
Launch with WOM1_PCPU_PSTATE=12 (run_uefi.py 0df36bc6).

Proven since 05:19Z (EXPERIMENTS.md EXP1081-EXP1087):
- a360abf6 BO cache LRU admission (EXP1081): no per-frame texture re-creation.
- 69912e68 + 85d6fd5b async render completion with first-map retirement
  (EXP1082/1083): stable, small gain (one submission in flight).
- 0df36bc6 P-cluster P-state 12 at launch (EXP1084, cpubench 1.5 -> 1.0
  ns/iter on P-cores). Windows parks the P-cores and keeps DWM on E-cores;
  forcing DWM onto P-cores halves its rate (EXP1085): DWM is latency-bound.
- EXP1085 Wom1G3PagingProfile: 376 ms/s of synchronous m1n1 broker traps,
  ~35 private-storage pages mapped and revoked per job; ACQUIRE also waited
  for the GPU (R157). cac08ced + 29f47ae4 keep reported scenes mapped and
  reuse them (LRU): 91 % hits, broker 360 -> 77.5 ms/s, +40 % flips (EXP1087).
- Build branches exp/108x-build hold the exact hardware sources (write-watch
  c4446277 not yet on hardware).

Open, causal order:
1. EXP1088: eight cached scenes (remaining ~15 misses/s).
2. Write-watch staging (c4446277): 31 GB/run of staging hashes.
3. Several submissions in flight (ACQUIRE no longer waits on a cache hit).
4. Remaining broker traffic, Draw/Present costs, transient ResourceMap
   ERROR_BUSY (FlushStatus branch), firmware recovery path.



## Current as of 2026-10-09 05:19Z (read this first)

State: GPU-visible baseline after each run (rec1080h rollback). Best validated
package: EXP1079 (9a066306): stable, no TDR; charmap drag ~12-14 flips/s.

Unit correction (EXPERIMENTS.md, EXP1076 correction): AppleAgxVsyncTrace `t`
is 100 ns, not QPC 24 MHz. Every earlier flips/s figure derived from it is
2.4x too high (EXP1063 "~17-19" was ~7-8; EXP1069 ~4.2; EXP1077 charmap ~11).

Proven since 03:20Z (EXPERIMENTS.md EXP1074-EXP1080):
- e4da9190 resumes a halted AGX firmware after a Timeout/Fault event (Asahi
  recover(); EXP1075 bugcheck 0x116). Not yet exercised on hardware.
- 88e24942 BO cache 128/64 MiB; 9a066306 keeps the UMD trace handle open.
- EXP1078: DWM composition is CPU-bound (~58 % running), not GPU-bound.
- EXP1080 per-DDI timing (75d986d1), charmap drag, DWM per 2 s: UMD 37-59 %
  of wall; CreateResource ~14 x ~18 ms, DestroyResource ~14 x ~10 ms, Draw
  ~200 x 1.6 ms, Present ~55 x 3.6 ms. Phases (70 s): paging wait 7.7 s for
  new 2.9 MB textures, upload 7.5 s, completion wait 13.7 s (1.75 ms x
  112 submits/s, synchronous).
- Cause of the resource churn: the winsys BO cache refused every release once
  full and never evicted; a360abf6 admits by LRU eviction (Mesa agx_bo_cache
  policy). EXP1081 verifies.

Open, causal order:
1. EXP1081: BO cache hits replace DWM's create/destroy paging waits.
2. Synchronous completion wait per submission (~14 ms per frame) -> several
   submissions in flight (EXP1066 device-destroy signal defect must be solved).
3. Draw/Present fixed costs; Notepad GDI-interop ping-pong copies.
4. Hardware check of the firmware recovery path when a timeout recurs.




## Current as of 2026-10-09 03:20Z (read this first)

State: GPU-visible baseline after each run (rec1073h rollback). Best validated
package: EXP1072 (74580051): zero/scratch pages, keep-active batches, lease at
retire; DWM stable, no TDR, Notepad/Settings render without speckle.

Proven since 01:10Z (EXPERIMENTS.md EXP1069-EXP1073):
- 9a9a3020 zero/scratch pages end the GPU hangs (EXP1069 validated).
- BatchBegin drained every other open batch: 66 % of DWM submissions
  (EXP1070 perf reasons). b77d9373 keeps them open; 74580051 returns the
  scene lease at retire (EXP1071 exhausted it). DWM submissions per present
  13.4 -> 10.9 (EXP1072).
- Under the mouse load the flip rate (~10/s) is bound by Notepad's frame
  time (SetWindowPos waits for its redraw), not DWM; Notepad's GDI-interop
  surface (2.4 MB, CPU-mapped) is copied down/up through escapes on every
  submission (~3000 measure-xfer per run).
- KMD completion detection cost one 1 ms tick per job; 8ef1eb63 polls every
  20 us for 2 ms: Submit->Notify 2.31 -> 1.67 ms, GPU job ~0.73 ms (EXP1073).
- EXP1073 GPU stall without TDR = registry I/O in the paging DDI under
  MmRotatePhysicalView (live dump, agx_livedump f5f97491); fixed by 6029de0f
  (paging-DDI receipts written by a work item). EXP1074 verifies.

Open, causal order:
1. EXP1074: fine poll + 6029de0f stable under the same load.
2. Notepad GDI-interop ping-pong copies (CPU-mapped GPU-written slot).
3. Per-job fixed costs: Submit->Worker ~0.28 ms, kick prep ~0.35 ms,
   VidSch notify->wake ~1 ms; then multi-in-flight submission.
4. TDR recovery (firmware restart path); libagx zero-page constants.


## Current as of 2026-10-09 01:10Z (read this first)

State: GPU-visible baseline after each run (rec1068h rollback). Best package
for rendering: EXP1067 (4d7d3f0c), but it times out the GPU at ~90 s uptime;
EXP1063/EXP1064 remain the stable reference (~17-19 flips/s).

Proven since 23:25Z (EXPERIMENTS.md EXP1065-EXP1068):
- agx_update_vs crash on layout-less draws: NULL input layout binds empty
  vertex elements (b1327c6e, EXP1066).
- SV_VertexID read k + id: lowering ran after info gathering, so the PARAMS
  push range was never uploaded; 4d7d3f0c regathers (EXP1067 all PASS).
- Queued-only submission gave no overlap and crashed D3D11 at device
  destruction: rejected, reverted (e8edf351).
- GPU hang = a draw reading an unbound vertex buffer (EXP1068 case B; case C
  SV_VertexID-only passes). Asahi points unbound VBs, null textures/PBEs,
  XFB offsets and query counters at AGX_ZERO/SCRATCH_PAGE_ADDRESS, which
  Linux binds in every VM; Windows bound neither. Fix 9a9a3020: per-device
  zero/scratch BOs, resident in every batch, VAs published per thread at
  draw entry and BatchBegin. EXP1069 verifies.
- The TDR reset cannot recover the engine: AdmissionPlatformRuntimeReset
  returns STATUS_DEVICE_HARDWARE_ERROR, bugcheck 0x116 (separate defect).

Open, causal order:
1. EXP1069: nolayout probes B/A must complete without a watchdog; DWM must
   survive the trace load past 90 s; speckle in Notepad/WinUI re-checked.
2. TDR recovery (firmware restart path) and DWM c00001ad loop after device
   removal; private scene storage STATUS_INSUFFICIENT_RESOURCES (EXP1067).
3. 60 fps: 2-4 submits/frame at ~5 ms each (4 ms synchronous completion
   wait); needs several submissions in flight without the EXP1066
   device-destroy signal defect, and lower per-job cost (kick prep 346 us).
4. Precompiled libagx kernels keep the Linux zero-page constant (null
   texture query, GS/tess draws); DWM does not use them.


## Current as of 2026-10-08 23:25Z (read this first)

State: GPU-visible baseline after each run (rec1064h rollback). Best package:
EXP1063/EXP1064 (701c49e6 / 33004b7f): zero first faults, DWM stable, Settings
renders, Notepad menu labels/toolbar/text render, ~17-19 flips/s.

Proven since 22:36Z (EXPERIMENTS.md EXP1062-EXP1064):
- App contexts were poisoned forever by the native draw entry: zero-count
  draws (61530e9b, EXP1062) and draws refused before batch work (701c49e6,
  EXP1063: dropped with a kind-6 receipt instead). Menus now render.
- The dropped draws are app-bound NULL render targets (EXP1064 receipt:
  SetRenderTargets 1 slot, NULL handle, no DSV): no output by D3D rules.
- DrawIndexed(0) is now a no-op instead of E_NOTIMPL (33004b7f).

Open, causal order:
1. Some Notepad windows still show speckled menu bars / glyph blobs (pixels
   scrambled within blocks: suspect a texture layout or upload mismatch on a
   CPU-updated texture; compression is off, the staging hash covers every
   byte). Needs a per-texture CPU-write vs GPU-read receipt.
2. DWM upload/completion phases; broker internal scans (m1n1 Phase A).
3. Occasional multi-second completion wait at app start.

## Current as of 2026-10-08 22:36Z (read this first)

State: GPU-visible baseline after each run (rec1061h rollback). Best package:
EXP1061 (d3a5ce63): zero rejects, DWM stable through trace + Settings +
Notepad probes, 89 DWM submits/s, ~18 flips/s, Settings renders completely.
Full-owner m1n1 m1n1-824ea32d-mailbox, r143 unchanged.

Proven 2026-10-08 night (EXPERIMENTS.md EXP1059-EXP1061):
- Settings/ApplicationFrameHost crash (c0000005 memcpy) needed two fixes:
  fb077b89 CPU maps of Direct presentation slots through a private shadow
  (copy-escape download/upload), and cfb8f725 KMD copy escape admits
  classless GPU-local presentation allocations (EXP1059 predicate 39).
- Intermittent CreateBo failure = per-device buffer table exhausted at 256
  live buffers (EXP1060 receipts: active 256 for DWM x3 and an app); DWM then
  crashed in agx_fast_link writing to the NULL BO (the black flashes). Fixed
  by d3a5ce63 (4096 buffers, high-water-bounded scans), EXP1061.

Open, causal order:
1. Notepad/WinUI menu artifacts (noise or missing text): the app context is
   poisoned at the native draw entry (agx_state.c:5421, first fault on the
   2nd draw of a batch, also in two other processes); every later flush is
   dropped. 61530e9b makes zero-vertex/instance draws no-ops and splits the
   entry checks; EXP1062 verifies or names the refusing check.
2. DWM upload/completion phases; broker internal scans (m1n1 Phase A).
3. Occasional multi-second completion wait when an app starts (EXP1056
   5.66 s, EXP1059 7.52 s; absent in EXP1061).


## Current as of 2026-10-08 21:00Z (read this first)

State: GPU-visible baseline after each run. Best package: EXP1055 (dcd6bbc3 +
mailbox, graph indices, idempotent re-attach, 1 ms clock): ~15 flips/s. Full-owner m1n1 is
now m1n1-824ea32d-mailbox (7367d7da), r143 unchanged.

Proven 2026-10-08 evening (EXPERIMENTS.md EXP1050-EXP1052):
- Broker mailbox (m1n1 824ea32d, root 7fa353a2, EXP1051/1052): one trapped
  doorbell per call; per-call floor 22 us -> 2.6 us, UPDATE_LEAF 29.7 -> 9.7
  us; DWM flips x1.9 and submits x1.67 vs EXP1043.
- Paging profile (EXP1052): UpdatePageTable 27.7 s of BuildPagingBuffer in
  ~4 min, broker only 6.7 s; paging queue/worker/DPC latencies small (0.5 ms,
  31 us, 138 us, 4 us). The cost is KMD graph list walks per 16-KiB group and
  per 4-KiB system PTE -> 409580b6 (O(1) indices), to be measured in EXP1053.
- EXP1050 staging-blit Settings fix rejected (DWM device error); reverted.
- SetRootPageTable poison (EXP1050 DWM, EXP1051/1052 load app) is step 5 =
  AttachPrivate refusal with broker status 0; reason code in EXP1053.

- O(1) graph lookups (409580b6, EXP1053): 16 MB map ~510 -> ~45 ms.
- SetRootPageTable poison fixed (7cf12a52, EXP1054): exact re-attach accepted
  while another context runs; no DWM black flashes, zero rejects.
- 1 ms system clock while the GPU runtime runs (bba6a51a, EXP1055): job
  completion was noticed one 15.6 ms tick late; kick->complete 11.1 -> 1.4
  ms, DWM 16 -> 66 submits/s, flips 385 -> 1184 per ~76 s (~15/s).

Open, causal order:
1. DWM upload phase (2.2 ms/submit) and pid-10840 batch rejection (EXP1055).
2. Broker internal scans (m1n1 Phase A: backing hash, per-backing refcount
   for referenced()) once the KMD side no longer dominates.
3. Settings/AFH presentation update (scene ManagerGeneration lifecycle).
4. Notepad menu artifacts / dark-looking WinUI surfaces.


## Current as of 2026-10-08 17:05Z (read this first)

State: stable baseline (EXP1049, 17:20Z): normal GPU-visible profile, APPL0002
Code 28, no AppleAgx package (EXP1046 package removed 16:55Z, verified
durable in the hidden profile: Preflight PASS, Package Absent).

Proven 2026-10-08 (EXPERIMENTS.md EXP1049, dumps in
/Users/pavel/J313-evidence-archive/2026-10-08/EXP1049-dumps):
- The dumped 0x133 (DPC_WATCHDOG, cumulative) is the rec1046 NORMAL recovery
  boot (exp392, SSDT 0x24B) with the EXP1046 package still bound: dxgkrnl
  connected the nine raw AGX level lines 880-888 of the legacy G2 _CRS, and
  GSIV 886 = AIC 575 = AGX ASC mailbox send-empty (Asahi t8103.dtsi; a FIFO
  status level, Linux keeps it IRQF_NO_AUTOEN) took 18.9 M ISRs on CPU0;
  storport retries starved until the DPC watchdog; next boots could not read
  the registry. r143 full-owner exposes only the synthetic edge 889 (EXP1014
  dump), so this hazard is specific to the normal recovery profile.
- Rollback defect: recover.sh launched the normal profile after the hidden
  cleanup boot failed (no SSH, package still bound). Fixed fail-closed in
  .local/experiments/EXP1046-present-update/evidence/rec1046h/recover.sh
  (template copied by prepare.sh): stop unless hidden cleanup reports
  Staged 0 / Sys false / Umd false.
- Guest RTC does not advance across resets; Windows event times are not
  wall time (boot attribution must come from host logs).

Open, causal order:
1. Crash 1: EXP1046 full-owner 0x133 (same P1-P3), dump overwritten; that
   package still carried the EXP1044/1045 paging-path registry diagnostics
   (now reverted). Re-test EXP1046's UMD change (6e9c1ad0) only on a clean
   package, with dump-first recovery.
2. Performance: paging churn (EXP1045: 72% invalid PTE writes) and per-op
   broker cost (EXP1044: ~30 us/op) -> broker redesign (Phase A m1n1 hash
   index + no TLB invalidate on invalid->valid; Phase B batched ABI).
3. Notepad (WinUI 3) labels/menu artifact; Settings (presentation update).
4. Legacy recovery firmware still exposes 575/577 level lines: never boot
   it with an AppleAgx package bound.


## Current as of 2026-10-08 11:00Z (read this first)

State: Air on the EXP1039 full-owner boot (package installed for same-boot
probes); recover with guest `shutdown /r` then
.local/experiments/EXP1039-rgba-alloc/evidence/rec1039h/recover.sh.
Last validated code: b2e57eaa (EXP1039). Probe set (tools/agx-text-selftest,
copied to C:\Users\pavel): glyph, gec, text, inst, texfmt, font, swap
(swap needs the interactive task arm-swap2.ps1).

Proven 2026-10-08 (EXPERIMENTS.md EXP1031-EXP1039):
- Indexed draws: restart index by index size (3718cc40, EXP1031).
- Dynamic-buffer shadow published at Unmap (36841918, EXP1033): first D2D
  glyphs and the black startup interval fixed.
- Multi-draw GPUVA batches (a3bbb8f9, EXP1035): flips 12 -> ~260-400 per
  150 s; median frame interval 150-270 ms.
- G4 colour class carries bytes per pixel (20df4061, EXP1036): R8/A8/RG8
  render passes bind; KMD bind minimum = W*H*bytes.
- Lowered A8/RGB32 CPU access translates the client format (e622d648,
  EXP1037): texfmt 12/12.
- RGBA8 displayable back buffers accepted as non-scanout allocations without
  WRITTEN_PRIMARY (c4ec15b8+b2e57eaa, EXP1038/1039): WinUI flip swap chains
  no longer remove the device.
- Rejected: Mesa-only R8 admission (EXP1034: KMD 4-byte bind -> device error).

Open, causal order:
1. Frame latency: per DWM submit a synchronous completion wait (phase 5,
   ~25-36 s per 230 s) and paging waits (~33 s). Candidate: skip the CPU
   wait when no held slot needs a download (umd_gpuva_windows.c submit()),
   after proving holds/copies are not released on that assumption.
2. Notepad (WinUI 3) tab/menu labels missing; explorer XAML text works;
   fonts and RGBA8 swap chains excluded (EXP1039). Parked.
3. DWM output lags seconds behind window changes (same cause as 1?).
4. TDR reset unsupported (0x116 after a hang).

---- older state below ----

# J313 GPU — composed G4 desktop; staging/paging costs cut; predicate57 open


## Current as of 2026-10-07 04:30Z (read this first)

State: Air recovering to the ordinary GPU-visible baseline after EXP1000
(hidden-first rec scripts; issue a guest `shutdown /r` before recover.sh).
No accepted graphics package; every experiment package is removed after its run.

Proven tonight (EXPERIMENTS.md EXP994..EXP1000, all self-test gated):
- Draws rasterize with default state (489076d3, no-DSV depth disabled).
- Lazy download of unmapped private slots (d5c5eb36, EXP995): DWM downloads
  ~0.9 GB/window -> 0.
- Winsys BO cache (57213796, EXP997): paging waits 2571/52 s -> 345/12 s.
- Poisoned process no longer withholds a finished fence (b3a8d162): fixes the
  EXP996 0x119 (SchedulerFaulted 0x40D6B via stuck completion).
- No registry I/O in the successful paging path (4465d720, EXP998):
  18 ms/paging wait (was 34).
- Chunked 64 KiB uploads (048eaae7, EXP999): upload bytes 818 -> 309 MB;
  DWM 3.5 submits/s; upload time is now wait-bound (copy escape waits for
  global paging quiescence and any active job), not byte-bound.
- Rejected: BO churn as first-order cost (EXP994 analysis); residency
  eviction as predicate57 cause (EXP1000: MakeResident refresh accepted, retry
  still fails; reverted 56035cf0).

Update 2026-10-07 07:20Z (EXP1001-EXP1008):
- Fail-fast copy QUERY (7fdf94d1, EXP1005): the EXP988 PTE wait never
  recovered (0/70) and cost 18-33 s each; removed.
- predicate57 = VidMm sends NO UpdatePageTable for some fresh mappings
  (EXP1006/1007 rings: neighbours populated, failing VA has no record; zero
  KMD early-exit rejections). Rejected repairs: MakeResident refresh
  (EXP1000), scheduled touch (EXP1003; kept, harmless), residency before Map
  (EXP1004), same-VA re-map (EXP1006), 128 KiB minimum (EXP1008, reverted:
  failures follow the smallest BO kind, DWM regressed). Recent failures are
  mostly USC-window (0x11_xxxxxxxx) BOs. Next: verbose DxgKrnl/VidMm ETW
  for one failing allocation (VidMm's own residency/PTE decision).

Open defects, causal order:
1. predicate57 (see update above). Kills batches -> seterror -> app/DWM
   device errors; ~6-80 per 4 min depending on retry rate.
2. Copy-escape gates are global (any active job, global paging quiescence,
   1 ms sleeps, pte_wait 1 s re-entering the quiescence loop): ~5 ms/slot and
   multi-second stalls. Candidate: per-process job gate + per-allocation
   paging gate (canonical copies are process-private).
3. Borrowed (shared) staging is locked+hashed every submit (~33 s/228 s);
   long-term: shared D3D surfaces without CPU access as Direct GPU-local.
4. Hardware cursor plane absent.

## Previous: Current as of 2026-10-05 20:55Z (read this first)

State: Air is in the clean ordinary GPU-visible baseline (REC-EXP976: hidden
Code45 cleanup of exact package976, then ordinary Code28/CPU8/SSH, durable
Preflight PASS, PackageAbsent, no PageHeap/IFEO, DisableDDisplay default).
No accepted graphics package.

Proven today (see EXPERIMENTS.md EXP970..EXP976):
- Correct pixels: wallpaper, icons, taskbar, Run dialog and Device Manager
  render correctly through UMD->KMD->DCP (operator photos EXP970/EXP974).
- BeginJob latency fixed: indexed G3 graph/envelope/CopyPte lookups
  (e3137c61, 6f534cca) and diagnostic upload rehash gated off (f832f5cf):
  DWM BeginJob 78 ms -> 0.09 ms, render DMA 131 ms -> 15 ms median.
- DWM crash root cause (EXP975 PageHeap): d3d11 DeallocateCB read a freed
  hRTResource passed by AdmissionUmdDeallocateResource; runtime-named
  deallocations were deferred past DestroyResource (opened/shared retirements
  and deferred presentation records). Fixed in 37d5423d (synchronous
  release inside DestroyResource, after flushing the immediate context);
  offline RED 4 stale -> GREEN; EXP976 hardware: DWM alive 22.5 min, zero
  dwm crashes, zero deallocate/retirement failures. VALIDATED.
- EXP974 'fast live desktop' was the non-composed GDI fallback after DWM died.

Open defects, in causal order:
1. Per-submit CPU staging (UMD transfer_held/transfer_slot): every DWM submit
   uploads all CopyHeld non-Direct slots in 64 KiB KMD copy escapes, waits for
   the GPU synchronously, then downloads GPU-written slots. EXP974: upload
   ~55 ms, download ~32 ms, paging wait ~14 ms, submit cadence 0.5-1 s ->
   desktop updates take seconds. This is the main performance blocker.
2. Explorer QUERY predicate57 (PTE explicitly invalid, i.e. allocation not
   resident) during the out-of-submission copy: VidMm only guarantees
   residency while the device's contexts are scheduled (MS 'Residency
   overview'). Intermittent (EXP969/970/976 yes, EXP976E no). Kills Explorer's
   device -> black wallpaper/taskbar until Explorer restarts.
3. DWM reject-batch kind1 (Begin) site 0x72 + seterror ResourceCopyRegion
   after ~12 min in EXP976 (no crash) — not yet analysed.
4. Hardware cursor plane absent (no SetPointerShape/Position) — cursor
   invisible when DWM is not drawing it.
5. Ordinary recovery boot after a full-owner run resets twice (EXP975/976);
   the GPU-hidden emergency profile cleans up reliably.

NEXT (decided, not started): remove per-submit CPU staging. Design target:
render/sample GPU-local allocations directly (as Direct primaries already
do) so no CPU copy happens outside a submission; this addresses (1) and (2)
together. Start offline: inventory which slot classes are staged and why
(CPU-visible imports in the aperture, 4 KiB vs 16 KiB UAT leaves), read the
Asahi/Mesa AGX BO model and the WDDM GpuMmu system-memory segment contract,
then write the WINDOWS/AGX/TRANSLATION/UNKNOWN plan before any hardware run.
Supervisor (Claude) runs experiments directly; Codex only on request.
Before any GPU run: operator-ready gate; after: exact rollback (prefer the
hidden emergency cleanup if the first ordinary recovery resets).
Unpushed: branch integration/ad04-windows-compiler is ahead of
origin/feature/j313-gpu-acceleration (ca71f549); push needs operator OK.

## Previous: as of 2026-10-05 19:24Z

EXP974 source62027081/package974 (INF59141ff3, SYS6a579cb2,
UMD917062ab, CATf0424d3e) indexed `AdmissionG3CopyPte` through the
existing graph slots and broker buckets; DWM UMD QPC phases were measurement
only. The real G3 replay visit-count test was RED on the old lookup and GREEN
in both 16/64-KiB profiles; affected suites8/8. Pinned WDK ARM64 build had
0 warnings/0 errors after a const-signature correction. Full host suite1212
retained the known unrelated22 failures/42 errors/2 skips.

Full-owner Event133758/LastBoot18:30:26Z reached Code0/CPU8/pinned SSH and
survived >33min. DWM PID1224 early BeginJob median0.0885ms, but 84 UMD
SubmitCommandCb phases had median0.847s adjacent interval. UMD median upload
54.869ms (45 copy escapes/2.95MB), download31.907ms, paging wait13.623ms;
these medians do not sum to a frame duration. DWM crashed at18:33:17Z:
Application1000 c0000005, d3d11 CBlendState hash erase during dwmcore
PostRender. Exact dump SHA73eedfdc and cdb decode under
`.local/experiments/EXP974-copy-pte-phase-r1/evidence/interim/dump/` show an
invalid low-address write0x581e51e8. Writer and relation to indexed CopyPte
remain UNKNOWN. Dwm[] at t25. DCP A408 count t2=19 and t25=21.

Operator18:39Z saw fast Win+D wallpaper/Run/Device Manager input response,
with live typed text, but taskbar and Start region black and mouse cursor
invisible. Host-verified photo SHA3d3e4717 in EXP974/evidence/operator/
corroborates the partial panel. DWM had already crashed, so the later live
updates likely used Windows fallback; they do not establish a working DWM
path. The second operator request at18:55Z had no answer before final freeze.
EXP974 is REJECTED as a stable/correct desktop; CopyPte speed effect remains
INCONCLUSIVE against the 0.8s cadence because there is no matched baseline
phase receipt. No accepted desktop package exists.

Exact974/oem5 was removed only from ordinary Event133861 Code43. The first
ordinary recovery boot stalled at IRQ route count8 and required documented
SIGINT CPU snapshot/SIGTERM reset; identical second boot reached Code43/SSH.
Fresh immutable ordinary Event133957/LastBoot19:21:04Z passed durable Preflight
Code28/CPU8/SSH/Staged0/PackageAbsent (manifest SHA23e0bc79). Leave this
clean GPU-visible guest running. Evidence and before/after recovery entries
are in `investigation/EXPERIMENTS.md` EXP974 and REC-EXP974A..E.

Next causal target is the recurring DWM low-pointer corruption during D3D11
object release. Supervisor proposed a separate diagnostic-only PageHeap run
with the same package identity to fault at the first write; this remains a
hypothesis and is NOT staged or authorized by this EXP974 verdict. Follow the
phase-boundary new-thread rule before that work. Preserve the separate ordinary
recovery stall as platform issue; do not conceal it in the Windows driver.

## Current as of 2026-10-05 17:54Z

EXP973C launched the existing zero-warning package973 (source f832f5cf,
INF 6dff0fa2, SYS 0fd2cbec, UMD 46df0c04, CAT 5afce5e4) after fresh
ordinary Event133354 Code28/CPU8/PackageAbsent and exact hash verification.
Full-owner Event133454 reached Code0/CPU8/pinned SSH. The paired EXP971
ring measured DWM PID1232 BeginJob median 0.0867 ms over 43 entries,
max 0.1142 ms; the initial ring had 50 DWM entries at median 0.0678 ms.
This confirms the preregistered <5 ms timing checkpoint and localizes
EXP972's residual 19.13 ms BeginJob cost to the production upload rehash.
Short ETW, UMD, ring and host log are frozen with sizes/SHA in
`.local/experiments/EXP973-upload-rehash/evidence/exp973c/` and
`investigation/EXPERIMENTS.md` EXP973C.

The OPERATOR REQUEST for mouse movement and Win+D was posted at17:30:22Z.
The answer arrived at17:41:54Z, just after the ten-minute deadline: only
taskbar icons were visible; mouse, keyboard and Win+D changed nothing visible.
The supervisor's ETW decode measured DWM render DMA median15 ms/max19 ms
(EXP970 median131 ms/max326 ms), but submissions only every 0.5–1 s. The
remaining measured frame interval is before submission; its exact UMD phase
is unmeasured. DCP exact swaps and cached snapshots do not prove correct
physical pixels. No accepted stable graphics package exists. EXP974 targets
indexed KMD AdmissionG3CopyPte lookup, with UMD per-submit phase timing as
measurement only. The separate ordinary recovery stall remains unresolved;
it did not recur in this rollback.

REC-EXP973C-A/B/C ordered out of Code0, booted immutable ordinary EXP377/392
into fresh Event133556 Code43/CPU8/SSH, removed exact oem5/package973 there,
and booted immutable ordinary again. Event133658/LastBoot17:48:38.4181780Z
passed durable Preflight Code28/CPU8/SSH/Staged0/PackageAbsent. Evidence
`.local/experiments/EXP973-upload-rehash/evidence/rec973c/`. Leave this
clean ordinary guest running.

## Current as of 2026-10-05 16:15Z

EXP972 indexed graph/envelope package972 (source e3137c61, INF e42737f6,
SYS fc684661, UMD 045a869f, CAT 6176940f) booted full-owner Code0/CPU8/SSH.
The paired 8-second ring measured DWM BeginJob median 19.13 ms (38 jobs) and
Explorer 27.07 ms (12 jobs); the initial ring had DWM median 29.15 ms (39
jobs). This is below EXP971's DWM 78.07 ms median, but above the preregistered
<5 ms checkpoint. The indexed lookup helped, but did not fully explain the
remaining BeginJob cost. ETW, UMD, ring and host log were frozen under
`.local/experiments/EXP972-indexed-lookup/evidence/`. DCP swaps occurred, but
the operator's requested mouse/Win+D physical report did not arrive; EXP972
visual correctness is UNKNOWN. No accepted stable graphics package exists.

Two identical GPU-visible ordinary recovery launches stalled at IRQ route
count8 before SSH. SIGINT captured CPU/IRQ snapshots and SIGTERM returned the
machine to proxy. The immutable GPU-hidden emergency boot reached Code45/CPU8/
SSH; hash-gated exact972/oem5 cleanup removed the package. Fresh ordinary
EXP377/392 boot Event133255/LastBoot16:14:46.987852Z passed durable Preflight
Code28/PackageAbsent/CPU8/SSH. This ordinary guest is the current recovery
control. See EXP972 and REC-EXP972A..D in `investigation/EXPERIMENTS.md`.

Next causal target: measure the remaining BeginJob phase before changing code.
The supervisor identified pre-existing linear `AdmissionG3CopyPte` and other
TableShadow walks as a possible later EXP973, but EXP972 did not isolate them.
Keep the recovery stall separate from the GPU timing verdict.

EXP971 receipt-only KMD timing source399e47ee/package971 reached Code0/CPU8/
SSH, but the operator saw no immediate panel redraw after mouse movement and
Win+D. DWM BeginJob median78.07ms/max263.94ms in the first ring and median
220.78ms during the input window; Explorer BeginJob median30.90–34.70ms.
Backend submit was typically below3ms. ETW independently reproduced slow
render versus fast paging; exact ETW/ring fence join was inconclusive.
Source inspection and Claude's 14:45Z review identify per-page linear
`GraphTranslateVa`/range and KMD logical-envelope list walks as the strongest
next cause. Plan and evidence: `investigation/analysis/EXP971-latency-verdict.md`.
EXP972 is one indexed-lookup correction with the EXP971 timing ring retained;
preserve all per-page validation. Exact971 was removed in ordinary Code43,
and REC-EXP971C passed durable Code28/PackageAbsent/CPU8/SSH at14:52:56Z.
No accepted stable graphics package exists.

### Previous EXP970 decision

EXP970 repeated exact package969 and proved correct physical desktop content:
the operator saw wallpaper, desktop icons, taskbar, and watermark after mouse
dirty rectangles and Win+D redraw. The image updates seconds late; full redraw
took about 30 s. ETW970E measured render DMA Explorer 33–48 ms and DWM
65–326 ms (median 131 ms), with paging near 0.2 ms. The earlier Explorer
predicate57 eviction hypothesis did not recur and is not the current cause.
Target: per-job KMD/firmware phase latency, one receipt-only EXP971 run before
any behavioral fix. Plan: `investigation/analysis/EXP971-job-phase-timing.md`.

REC-EXP970A..D failed before Windows at CPU1..7 PMGR start across two m1n1
images. The operator's full power cycle restored ordinary recovery. REC-EXP970E
booted CPU8/SSH/Code43 exact970, ran hash-gated cleanup970, then a fresh
ordinary EXP377/392 boot passed durable Preflight Code28/PackageAbsent with
one inert APPL0002. Keep this guest as the recovery control. No accepted stable
graphics package exists. Why continue the current path: physical pixels and
cross-process composition are proven; the measured DMA latency is the nearest
remaining lifecycle boundary and EXP971 can isolate a phase without changing
behavior. The older material below is retained as historical context, not the
active state.

## Historical state through EXP969

## Objective and rules
Long-term objective: a correct stable physical Windows desktop. The operator ordered STOP for today after EXP969; do not build, stage or launch another GPU experiment until explicitly resumed.
Accepted/stable graphics package: NONE. Selected clear/copy pixel workloads pass;
correct DWM composition and physical presentation remain unverified. User photo proves real fragments,
not a blank panel. Keep short falsifiable tests and collect immediately on failure.
No subagents or messages to other chats. Preserve foreign dirt and existing evidence.
Read this file first after reset; use only referenced ledger entries/plans.
Every hardware build/run/recovery: BEFORE + ACTUAL in EXPERIMENTS.md.
Every implementation: verify, commit, then CHANGES.csv row with full commit hash.
Do not equate Code0, Present S_OK, SDK exit0, or a zero cached sample with visual success.

## Workspace
Worktree /Users/pavel/public_windows/.worktrees/integration-ad04-windows-compiler
(physical /Volumes/pwdev/public_windows/.worktrees/integration-ad04-windows-compiler).
Branch integration/ad04-windows-compiler. ROOT=/Users/pavel/public_windows.
Artifacts ROOT/.local/experiments, not worktree/.local.
Foreign dirt: m1n1_windows/rust/vendor/rust-fatfs, mu;
untracked drivers/apple-agx/render-admission/pauls@192.168.1.24.ps1 and
investigation/analysis/EXP895-standard-blt-stimulus-plan.md.

## Current — EXP969 stopped and exact package removed
No accepted/stable physical graphics package. EXP969 source commit
42257a270ffbc4aeab81812473878103316a9ecd, package969
INF e1ee11ab/SYS4d159b1b/UMD853832e0/CATe1d1a31b, changed only the
bounded retry of both final KMD paging-quiescence checks. RED/GREEN replay
covered the second-check race and retained R161 no-drain STATUS_DEVICE_BUSY.
The pinned WDK build had 0 warnings/errors. Hardware boot Event132014/
LastBoot19:06:57Z reached Code0/CPU8/SSH; DWM1240 and Explorer5324
remained the same processes through the frozen evidence window. Guest-frozen
UMD log6440 lines/842841B SHAa6e89da35aba1ea9d22935fe31debcc0ad053f314394c0d9492a3944984b4714
matched the host copy: DWM Present S_OK appeared and there was NO DWM
reject-copy-escape/slot/batch/DrawIndexed line. KMD registry had NO
Wom1G3CopyPagingQuiescence or CopyTransferFailure receipt. This means no
predicate62/3000ms bound hit with RecordsUnsubmitted>0 was OBSERVED; the
failure-only receipt does not expose successful per-copy WaitMilliseconds,
so those durations are UNKNOWN. Do not claim a stable desktop: the operator's
physical panel answer was still pending at stop. A separate Explorer PID5324
copy QUERY failed predicate57/STATUS_INVALID_PARAMETER on an invalid logical
PTE at VA0x50000, allocation0x40000980, GraphProcessId10; UMD rejected
Explorer Draw. Its relation to panel pixels is unproven.

Exact969 was removed in ordinary GPU-visible Code43. Fresh ordinary recovery
Event132218/LastBoot19:19:50Z passed durable Preflight Code28/CPU8/SSH,
PackageAbsent, no AppleAgx service/module/signer/arm or phantom. Evidence
EXP969/evidence/evidence-manifest.json SHAeab73f41a66e66f1929b087a190ff47f74b96527e199fdf958a54be173d4f954,
EXP969/evidence/rec969b/durable-preflight.json SHA6808a43838cf7ccb370873284c43e4d3409cd1473bd3d8750775edcc2c1b3c78,
and EXP969 ACTUAL/REC-EXP969B ACTUAL in EXPERIMENTS.md. Leave this clean
ordinary guest running and STOP. On explicit resume, first obtain the
operator's EXP969 physical panel report. If the panel was black or corrupted,
source-first next causal target is Explorer predicate57 GPUVA residency
and DWM input/composition versus physical scanout; keep the residual
post-final-check paging-build race distinct. No new package is prepared.

Reference: EXP967 measured DWM predicate62/STATUS_DEVICE_BUSY at WaitMs0
with RecordsUnsubmitted7, all submitted queue/worker/fault fields0. EXP968
retried the first late check but still failed at WaitMs0/unsubmitted13
because the second final check could return BUSY immediately. EXP969 made
both final checks retry. R161 EXP874 proved bypassing paging order entirely
caused a deferred transfer to overwrite an upload and a 0x116; never remove
that guard. EXP966's predicate62 wait duration was NOT measured; old
"after3000ms" wording was corrected in EXPERIMENTS.md and CHANGES.csv.

## Referenced prior experiments
EXP965 source aac78bd15f79706f54066f9e9fd3e2bc6aa9455c cleared only
SupportKernelModeCommandBuffer because current RenderKm handles ColorFill only
and aperture is not declared cache-coherent. Focused tests19/19 and pinned WDK
package965 build0 warnings/errors. After exact964 cleanup (emergency hidden
Code45 was needed following three ordinary early PSCI resets), durable ordinary
Code28/CPU8/SSH preflight PASS on boot16:47:03Z. EXP965 exact package965
INF05736342/SYSa80fad22/UMD3e127795/CAT042fd4d9 launched full-owner,
Code0/SSH/DWM1220. Operator saw a brief artifact then a FULLY BLACK physical
panel: worse than EXP964's visible taskbar icons. Explorer AppHangB1 restarted
twice at16:57-16:58Z while DWM1220 stayed alive. Two Report.wer archives were
preserved; no historical hang dump exists. Post-restart Explorer7096 live dump
SHA6c8a243 shows Desktop main thread idle in NtUserWaitMessage; do not infer
the old hang cause from it. EXP965 GDI-cap suppression is REJECTED as a
standalone desktop fix. Exact965 was removed in ordinary Code43 and fresh
ordinary boot Event131006/LastBoot17:18:23Z passed durable Code28/CPU8/SSH,
PackageAbsent. Existing G3 receipt from EXP965 has UnpublishedGroups[0]=149376
and [1]=0: segment1 aperture nonpublication is not evidenced; the stronger
then-hypothesis was incomplete system-segment0 groups of four Windows 4KiB PTEs.
EXP966 later found only one incomplete group in DWM at frame arm and shifted
the causal target to the observed copy/paging refusal. EXP965
ledger/analysis/evidence and operator note in CODEX-STATUS.

No accepted visual desktop package. Operator reset REC-EXP958A and ordinary
recovery reached exact958 Code43/SSH. Cleanup958 succeeded, removed oem5 and
ordered reboot; one intermediate PSCI reset occurred. Identical ordinary
relaunch reached SSH, durable preflight PASS/Code28/CPU8 at11:30Z.

EXP959 source c9dacb1, package959 30.0.959.0 INF615859a9/SYS0851f9a9/
UMD34411206/CATa1900174 (receipt only) staged on that exact clean boot,
then full-owner m1n1-exp954/Mu-r143 booted Code0/CPU8/SSH/Explorer/disk/USB.
DCP exact latches through swap37; snapshots mostly zero (later1067/4096000
nonzero), no physical pixel success. DWM1220 crashed once, restarted1568.
UMD trace shows two Flush stage3 E_INVALIDARG (PID1220/1568). EXP959's new
umd-deallocate-failure receipt was silently quota-suppressed: both DWM PIDs
had exactly 128 non-measure records before the failures, and the diagnostic
function caps all non-measure/non-reject stages at 128. Therefore classification
is INCONCLUSIVE; neither local guard nor pfnDeallocateCb is excluded.
Dump dwm.exe.1220.dmp SHA061742cd analyzed on builder: AV reading
0x000000003c146258 in uDWM!CCachedBorderBrush destructor while x23 held
0x000001d03c146240 and x22 held its truncated low32. Earlier EXP958D full
dump showed analogous high32 loss in two uDWM visual vtable pointers. Writer
unknown; this repeated pattern is the strongest current corruption boundary.

REC-EXP959A stalled ordinary; documented host SIGTERM captured CPUs and reset
to proxy. One identical ordinary relaunch reached exact959 Code43/SSH. Exact959
cleanup and ordered reboot gave durable Code28 PASS at12:10:08Z; no package
carried into EXP960. Manual reset was not needed.

EXP960 quota correction commit f38a7f0a2bc514ab0115e67b58f5d60da2f7ae05
is verified offline (RED/GREEN focused test, UMD 11/11); pinned WDK ARM64
package960 built 0 warnings/errors, INF f8878e19, SYS7ed0377c,
UMD4400ca06, CAT0f51cd58, receipt SHAfa384ffc. Exact package staged after
clean Code28, full-owner booted Code0/CPU8/SSH. Corrected receipt proves
Windows pfnDeallocateCb returned E_INVALIDARG for full hResource
0x0000019ad19fe880 before DWM Flush stage3 E_INVALIDARG; local guard is
excluded for this first error. Later same handle received 0x88760870
(device removed). DWM1236 fail-fast dump SHAef18f82a is 0x889800d0
"DWM failed during present"; this run did not reproduce low32 pointer AV.
DCP latches/zero snapshots are not valid desktop pixels; no accepted screen.
Microsoft D3DDDICB_DEALLOCATE docs confirm hResource form/NumAllocations=0
we pass; callback invalid parameter cause remains to resolve from exact
Create/Open/Destroy provenance and runtime ownership. Keep independent low32
pointer corruption as separate writer-unknown issue.

EXP961 reused exact package960 after full rollback to query the existing KMD
DWM DDI probe. It did not reproduce UMD callback E_INVALIDARG in a short run;
the probe retained only latest successful DestroyAllocation among 756/4784
calls, so verdict INCONCLUSIVE. Exact961-local cleanup and durable Code28 PASS.

EXP962 source commit66565a2a adds only existing bounded G1b failure receipt
for KMD DestroyAllocation (ID6), package962 built0 warnings/errors. Full-owner
reproduced UMD stage2 E_INVALIDARG for full runtime hResource
0x000002bc95d26600 before Flush stage3. G1b registry receipt Count0 before,
during and after the error; KMD DDI probe observed 1788 DestroyAllocation
calls, latest status0. Thus no failing KMD DestroyAllocation return was
observed in this boot. Inference: Direct3D runtime/dxgkrnl rejects the
resource handle before KMD or after a successful KMD call; exact internal
ordering not proven. Do not modify KMD hResource logic on speculation.
New DWM dump SHA f6474f7c has another c0000005 low32 uDWM code-pointer
read (0x00000000fc5abf50) in CBaseObject::Release. It repeats EXP886/958/959
corruption but the writer remains unknown; do not claim callback refusal
caused it. DCP latches/cached-zero samples still do not prove physical pixels.

EXP963 source27a45271 added failure-only UMD retirement origin. Hardware
first failure was Origin1/CreateResource, Primary0, Shared0,
KernelResource0, KernelAllocation0x80001140, full runtime handle
0x000001f84e9f8120. Repeated failures have the same class. This strongly
points to wrong deallocation form for a device-associated nonshared
allocation, separately from the writer-unknown low32 DWM corruption.
EXP963 exact package was removed in Code43 and durable Code28 PASS on
boot14:06:00.8827080Z after one ordinary recovery PSCI reset.

EXP964 source commit6fc753fa uses the Microsoft documented allocation-list
`pfnDeallocateCb` form only for nonshared nonprimary CreateResource with
KernelResource0 and nonzero KernelAllocation. Shared/primary/resource-backed
forms and failure requeue are unchanged. RED/GREEN and UMD/R148 suites passed;
pinned WDK build964 0 warnings/errors, exact INF bdb6e604, SYS d3288aaa,
UMD184f78c9, CATcb05ad69. EXP964 full-owner is ACTIVE on Air, Code0,
CPU8/SSH/Explorer/DWM1232/disk/USB5. At14:44:02Z, >30min after boot
14:13:12Z, no DWM Application1000 errors, same DWM PID, 13154 UMD lines
with zero deallocation or Flush E_INVALIDARG errors, KMD DWM graph3
submit441/complete440/Present44 status0. DCP exact
latches continue; cached zero samples cannot establish physical pixels.
Console GDI CopyFromScreen timed out15s/result267014 without PNG; task was
removed. Operator photo around16:00Z (EXP964/evidence/operator-photo-1600Z.jpg
SHA30df23e51bb0dfb628f12132ddfe966b085e120b86d7bbbac0323ef38ef0d458)
shows a BLACK wallpaper and taskbar background but several visible taskbar
icons. This is partial composition, not a blank panel and not a correct
desktop. Hypothesis only: GDI/redirection surfaces may be black while GPU/
DComp/XAML icons work. Do NOT call this a stable desktop or accepted package.
The current EXP964 visual-verification run remains live;
cleanup964 exact scripts/manifest are staged and guest-hash verified, ready
for Code43 rollback if pixels are wrong. Do not start a new GPU package while
this one is installed. Evidence EXP964/evidence/30min-identity.json SHA44ba64de,
umd-30min.log SHA58bf98cc, dwm-ddi-30min.txt SHA3b2676d6,
desktop-capture/before.json and ledger.

Next causal target: source-first Windows GDI/redirection surface contract ->
our CPU-visible/aperture allocation and GPUVA sampling -> AGX. Derive one
smallest receipt/test offline, then roll back exact964 to durable Code28 before
new package hardware. Audit host launcher/proxy first: at16:11Z Windows SSH
was alive Code0/CPU8 on original boot but no host run_uefi owner was found.
Supervisor asked operator to press Win for Start-menu discriminator; result
pending. Do not guess DCP stride/format: current m1n1 IOMFB descriptor matches
its source reference. Keep recurring low32 DWM pointer writer separate.
WHY CLEAN RECONSTRUCTION: old admission comparisons do not explain current
DWM pointer truncation or separate platform recovery hang.

## EXP941 findings and rollback
Source382a333d65368453879155a21895bbef280ba126 changed native GPUVA+DXGI1.1
successful CreateDevice policy to S_OK; software/base retain NO_REDIRECTION.
Current Win11/D3D11 scope; D3D9 interoperability is not implemented/proven.
Same ce544 windowed SDK3940: actual Present1 and device-after S_OK, previously
OCCLUDED. stdout971B SHA df80d167478f4b545f3ceb464b8112cc6c3d880ad207256716286f9545c93055.
Wrapper267014 terminated before manifest; later host receipt verified stdout
and process absence. Do not claim a bounded successful process exit.
Fullscreen3732 still pending12s; paired SDK/DWM1244 snapshots, then onlySDK killed.
Matched941PDB decode EXP941-paired: SDK waits DXGI proxy->DWM ALPC; DWM at
privateRELEASE->ContextRetire->UpdateSubresourceUP. Moving snapshots are not proof
of a permanent deadlock. All owned tasks removed.

Operator photo5A3341E7 confirms repeated wallpaper strips and scanlines on941.
Photo archived internal EXP941-crash/operator-photo.jpg SHA
 d33fdff73e787583c6cdf77e5dca289e519ce1fe9d4749dddfe953f4fd2fbf64.
Same boot124399/gen242524939 was still alive4hourslater. DWM186submit184complete
plus3/3, Present0/Virtual0, source address assigned once0x1500110000 ->PA8e0110000.
Existing120/300/600s snapshots confirm sameDCPswap9/DVA102a0000 with0/537413/539383
nonzero pixels. Not blank; pixel correctness of offscreen copies is insufficient.

GDI CopyFromScreen timedout30s/noPNG; task removed. Afterwards DWM1244 crashed
10:01:46Z c0000005 in dwmcore!CComposition::PreRender+2e4 reading00000000f1fb9f70;
new DWM2356. Crash preceded by UMD Flush E_INVALIDARG, causality not established.
Crashdump3355744B independently verified SHA
6db8f4e415f059a62a0c91b11751d20de0ab13db90a188d5e65ce000b7273f99, internal EXP941-crash.
New1a8 Source10 is a diagnostic live dump, NOT a real bugcheck; retained WERqueue
copy and decoded EXP941-crash/kernel-decoded.txt. Do not blame it for earlier photo.
Visual hold app0f8d237c (fullhashGit), binarybd68cae693fd276082ec4b230ca39bbd75b3cac427ae925677b8150bf3cd54dc:
SDK2492 failed EnumOutputs0=887a0002/exit6 BEFORE drawing. No green frame produced.
Active consolePavelSession1/noRDP session; Code0/resolution persisted nonetheless.

Original941 final fourfiles+ETL frozen/verified internal EXP941-original with
ROOT941/original-final symlink; hostgate903ad13e7e3b745c1416ef07ab36b513c28e85777d3ab77591eb88570576b060.
Ordered restart10:11:03 delayed; Win32ShutdownFlags6 returned1115. SYSTEM/SOFTWARE
and volume C: flush PASS10:14:47.830Z. Windows finally issued PSCI RESET itself.
Planned SIGTERM was NOT sent (set-e stopped on SSH timeout); only SIGINT snapshot
and continue occurred. Normal377392 recovered,941/oem5 removed, ordered clean
restart10:19:12.129Z; durable Code28 gate above passed. No forced host reset.

## Earlier proven invariants
935 a1d58340: unbound RTV Clear binding corrected, hardware clear executes.
936 4e7bba96: direct-primary rotation compares KernelAllocation, rotation S_OK.
937 2c66edbb: RunFragment headermerge/tilecount offsets68/6c/78 corrected;
this alone did not fix pixel corruption.
938 8e947fc4: ISP_MTILE_SIZE y/x3e8/3ea corrected (old3e0/3e2); same fullGPUcopy
changed204800 bad pixels to0/4096000. Shared GPU clear/open/cross-process also passed.
939 coordinate pattern0ab19bc4: all4096000 unique coordinates matched, not just green.
939 bb2fbabf: CPU-only FrameArm software entry; owner guards retained.
940 ec734378: private RELEASE software entry with safe deferred reclamation;
retains queued/submitting/job/lease holds, quarantine and broker checks.
AuxFBInfo dimensions still16 atb8/bc and6e0/e4: known omission, unbuilt commit
2aaf9e38 reverted90db80be before938. No new evidence identifies it as current cause.
DCP basic uncompressedBGRA descriptor matches m1n1 reference. Asahi pix_size1
comment is for compressed/multiplanar surfaces; do not guess a stride/format fix.
One938 shared clear exited7 with unlogged device reason; later receipt-only retry
passed. Earlier unexplained failure remains, not erased.

## Controls and recovery
Air pavel@192.168.1.37 key /Users/pavel/.ssh/air; LogLevel=ERROR BatchMode=yes
ConnectTimeout=5 ConnectionAttempts=1; knownhosts ROOT/.local/experiments/EXP641-standard-present/air_known_hosts.
Builder pauls@192.168.1.24 key /Users/pavel/.ssh/windows_builder.
USB /dev/cu.usbmodemC02HDNCCQ6L41 andL43. Check both control planes before physical
requests. SIGINT exactowner snapshots+continues; SIGTERM snapshots+resets. No nested
proxy calls in handler, no rawUSB with liveowner. SSH timeout alone is not a GPU hang.
Normal recovery ROOT/.local/experiments/EXP810-g4-package817/recovery:
 m1n1-exp377.macho SHAfae3444cc289cf52ea12b81b9db8f3d8bf24bd084f899a751321d2048d9a525a
 J313_EFI-exp392.fd SHA16c177182e96b63eac852dcfb185cebba9c1d91943c6402106a640848ddc5e06
GPU visible/broker disabled. Hidden385 emergency only. Exact cleanup every package.
Fullm1n1 EXP933-hvc-return-contract/m1n1-exp933.macho
 SHAbdcf87154043535f4bcfcba0aa04e30c97d4877dd3ba9c87e18b6af287158395
Mu EXP928-selected-primary-lifetime/firmware/J313_EFI-r143.fd
 SHAe54c009847e64a4b2b327f54385eedb94f5a9e5fd3b459fd6101b07af4c023fc
Signer E9BE15BD2A184BFABA0C8035B3C620C58037A241; certSHA97145866a1530003077eacd8457f1a7a644d662423278fd94e450f903c85cbda.
SDK ce544e78e8f12e0d3e71f8b9f34c6c57f79c8490125fedb5ff13514ac4a4529b.
Dumphelper046e4063d23e8cb3451d5dd61e160ceb58018e1e2a92b5925b1babc250bb6301.
Frameprobe EXP928/AppleAgxBltProbe.exe28f636f19efb2ba165a59c70a7ff7c117a04a6401cc911ec40948f7a553b7bf1.

## Build/evidence constraints
Source manifest scripts/g3_build_source_manifest.py --root WORKTREE --commit HASH
--manifest ROOTNEW/source-manifest.json --archive ROOTNEW/source.zip.555inputs.
Pinned SDK26100/v14314.44.35207; exact sign/hash/ARM64/PE-PDB gates each build.
PDB check /tmp/j313-hvc-abi-test/bin/python and /opt/homebrew/opt/llvm/bin/llvm-pdbutil.
External launch gate>=5GiB.939/940/941 UMD copies now verified original-path symlinks
to internal archive; mapping EXP942/evidence-relocation.json. No evidence deleted.
Large ETLs/PDBs internal /Users/pavel/J313-evidence-archive/2026-10-03.
Unrelated existing test limitations: frontendprepare reference path/dirtyMesa;
virtual-submit replay missing AdmissionMemoryRuntimeLocalView/Inner declarations.
Do not claim whole-suite GREEN or fix unrelated harnesses.

## 2026-10-06 late: predicate57 boundary (EXP980R-EXP982)
- Proven (EXP982 Wom1G3LeafHistory v2 + UMD reject-va-history): the UMD VA bookkeeping is clean (1:1 reserve/map/free/deallocate, no stale or replaced canonical VA). The failing copy QUERY targets a VA mapped 25 ms earlier (Map E_PENDING, paging fence waited) for which the KMD never received a valid UpdatePageTable; the allocation's only valid mapping is in its creator process (4.5 s earlier).
- Visible effect: the failing escape returns DEVICEREMOVED to the client; DWM/shell devices enter error state, composition stops -> only the first icons appear, desktop black.
- Rejected: dispose order (2511a332, EXP980R); stale canonical VA in UMD (EXP982).
- Active hypothesis (to verify against Microsoft WDDM GPUVA residency docs before code): the CPU-time copy escape reads PTEs outside the window where WDDM guarantees them (VidMm only guarantees residency/PTEs while the device's GPU work is scheduled), so a copy issued between Map/MakeResident and the first scheduled submission can see unpopulated PTEs.
- Next causal target: move staging<->canonical copies to execution time of the device's submission (KMD performs them when VidMm guarantees residency) or otherwise bind them to a WDDM-guaranteed event; design offline first.

## 2026-10-07 night: draws never rasterize in the G4 D3D path (EXP989 + self-test)
- Fixed tonight: written-only downloads (6c5a4a65, consumers no longer overwrite shared staging); PageTableUpdateRequireAddressSpaceIdle cleared (b9f56bd8, removes VidMm process-idle per table update and most predicate57).
- Deterministic on-device test tools/agx-render-selftest (b21a4371): clear PASS, cross-device shared clear PASS, every draw FAIL (no fragments) at 16..256 px. Root cause of the black desktop is the draw path, not residency/copies.
- Active target: why TA/VDM geometry produces no fragments in mesa -> G4 native render -> KMD template builder -> firmware. Candidates to check offline first: UMD-written buffer-manager page/block lists (process ranges 0-2), VDM stream/encoder relocation (object 37), vertex helper/PPP, TA command fields vs Asahi fw/vertex.rs; then a KMD receipt of BM/TA counters after one self-test draw.
- Recovery profile unchanged; package removed after each run.

## 2026-10-07 22:00Z update (EXP1011-EXP1023)

- Proven root cause of the predicate57 copy failures and D3D device re-creation loops: the KMD refused well-formed `SubmitCommandVirtual` calls (`STATUS_INVALID_PARAMETER`, EnvelopeState predicate 13) while the backend worker was still returning (runtime predicate 8) or the previous job was running (predicate 4); dxgkrnl then marks the device in error (ETW `VidSchErrorFailedSubmitCommandVirtual`) and VidMm stops paging it. Fixed by bounded backpressure (`AdmissionPlatformRuntimeAwaitWork`, 10 s; 37c5d77a + 68fe110e). Validated EXP1015/EXP1021: 0 submit/copy failures, DWM/explorer stable, self-test repeated failures=0.
- Performance: VidMm CPU mappings of staging read at 60-90 MB/s irrespective of Cached/aperture/AccessedPhysically (EXP1016-1020 rejected); ordinary process memory 2.5 GB/s. Unshared staging now lives in VirtualAlloc memory (6a0ae7f2, EXP1022): staging hash 2.46 GB/s, DWM submit gap 34 ms.
- Current best package: EXP1022 (6a0ae7f2). Operator: desktop after Win+D, visible rate ~0.5 fps, missing text in Start/taskbar, mis-rendered icons.
- Active next causal targets: (1) visible frame path (only 16-64 DWM presents / 24-39 DCP swaps per ~4 min while render runs; m1n1 per-swap snapshots removed in EXP1023 gave 24->39 swaps only) — instrument submit->present/flip->SetVidPnSourceAddress->DCP latch->VSync timeline; (2) glyph/text rendering correctness; (3) initial black screen until Win+D; (4) open: 0x10E dump at EXP1014 rollback restart (archived), WATCHDOG TDRs in EXP1017/EXP1018.
