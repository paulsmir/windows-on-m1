# J313 GPU: CS 1.6 plays fullscreen on the AGX OpenGL ICD (standard benchmark 43.2 fps; without the Steam overlay UI paced at the 60 Hz vsync cap)

## Current as of 2026-10-17T01:07Z (read this first)

Installed: PACKAGE1343 (c99fde69 = integration driver sources; WOW64 UMD
out84 on closure 104, GL ICD out61, m1n1 5b857603). FL 11_0 default.
- PROVEN (EXP1340-EXP1343): D3D GPU timestamps for render and compute.
  The KMD keeps each job's firmware start/end by (process, command GPU VA)
  -- render: the 3D work's template objects 28/29 (start objects can be
  stale, the winsys clamps them); compute: every envelope compute job runs
  the timestamp microsequence and the firmware writes its own
  timestamp_pointers (sidecar block 0x00/0x08; the requested words stay 0)
  -- and returns them by escape; the winsys writes them (ns) into the
  batch's struct agx_timestamps slot when queries are attached, also to
  batches already submitted (Dispatch11's barrier); D3D TIMESTAMP is
  Mesa's query, TIMESTAMPDISJOINT 1 GHz, not disjoint. agx_timestamp_smoke
  PASS ARM64/x86 (compute 36/146 ms, render 50/171 ms tracking the work);
  no regression.
- Open: D3D12.
- Scratchpad tools vanish: restore from .local/scratchpad-tools-backup.

## Current as of 2026-10-16T23:36Z (read this first)

Installed: PACKAGE1341 (1597dd77: EXP1340's GPU-timestamp sources + a
receipt; WOW64 UMD out82 on closure 102, GL ICD out61, m1n1 5b857603).
- PROVEN (EXP1340): render GPU timestamps -- the KMD keeps each job's
  firmware 3D start/end (template objects 28/29) by (process, command VA),
  the winsys fetches them by escape into Mesa's slots (ns), D3D TIMESTAMP
  is Mesa's query and TIMESTAMPDISJOINT reports 1 GHz, not disjoint;
  t1 - t0 tracks render work (23/67/147 ms) within the CPU interval; no
  regression.
- OPEN (EXP1341): compute timestamps never requested -- Dispatch11's
  memory_barrier flushes the compute batch before the TIMESTAMP End, which
  attaches to active batches only. Next: attach to submitted batches and
  fill slots from the KMD record at cleanup; compute needs its firmware end
  without a flush-time request (timestamp microsequence for every compute
  job, never yet run on hardware).
- Scratchpad tools vanish (buildumd.sh, clone.py, manifests.py,
  patch-cleanup-umd32.py on 2026-10-16): restore from
  .local/scratchpad-tools-backup (synced 22:45Z) before a pipeline.

## Current as of 2026-10-16T20:59Z (read this first)

Installed: PACKAGE1339 (88f26491 = integration driver sources; WOW64 UMD out80
on closure 100, GL ICD out61, m1n1 5b857603). FL 11_0 is the default.
- PROVEN (EXP1338): vertex-stage scratch (RunVertex uses_scratch at
  ta+0x568, 4871ea13).
- PROVEN (EXP1339): compute helper programs / compute scratch (f48c6ac3:
  winsys no longer refuses drm_asahi_cmd_compute.helper; the G4 parser
  admits a mapped USC program + mapped scratch argument, cfg only with a
  program and possibly 0; KMD passes them; RunCompute 0xb0/0xb8/0xc0).
  agx_scratch_smoke cs/vs/fragment PASS n=8..96 on ARM64 and x86; every
  regression as EXP1338.
- Open: GPU timestamps (still refused at the winsys/G4 gates; Asahi needs
  microsequence Timestamp ops around WaitForIdle plus a KMD-owned buffer
  the firmware writes); D3D12.

## Current as of 2026-10-16T19:15Z (read this first)

FL 11_0 IS THE DEFAULT on integration (bcb0e9f8, EXP1337: every regression
matches the FL 10_0 baseline EXP1326; APPLE_AGX_D3D11_FL11=0 opts a process
out). Installed: PACKAGE1337 (d94ed918 = integration driver sources).
- The FL 11_0 desktop fixes (all validated, EXP1324-EXP1337): see the
  previous section; plus D3D 11 query types (1eab272c).
- Diagnostics kept in the UMD (receipt-only): reject-null-call,
  reject-cxx-throw/stack, reject-abort-raw/stack + abort minidump.
- Open: vertex-stage scratch; compute helpers; D3D12; GPU timestamps.

## Current as of 2026-10-16T18:33Z (read this first)

Installed: PACKAGE1336 (7a73857b: FL 11_0 default branch; rolled back by the
next pipeline). Integration HEAD keeps FL 11_0 opt-in until EXP1337.
- PROVEN (EXP1336): the FL 11_0 desktop runs -- system GL smoke PASS x4, DWM
  never restarts, every regression as EXP1326 (FL 10_0) except STATS. The
  FL 11_0 chain fixed on the way (each RED->GREEN, validated by EXP1336):
  broker registry 4096 (m1n1 5b857603, EXP1324/1326); ForcedSampleCount 1
  (2a15290f); NO_OVERWRITE on dynamic CB/SRV buffers (7e8c1405); shared
  A8_UNORM/RGBA8 Texture2D + resource data v3 DXGI format (e6d1beb9); the
  WDDM 2.0 DXGI 1.2-1.4 entries filled after the DXGI 1.1 ones -- Present1
  had forwarded to NULL (0297dd3f); multi-surface Present1 (87718b63);
  ClearView rectangles via the Asahi blitter, Asahi has no
  clear_render_target (f837d747; clear_texture fallback 86d24a14); WDDM 2.0
  ResolveSharedResource/redirection (6d25a3d1); SM 4 switch through the
  DXBC translator -- tgsi_to_nir aborted on Terminal's ps_4_0 (fd1d5e16).
- Receipt-only diagnostics now in the UMD: reject-null-call (AV at 0),
  reject-cxx-throw/stack, reject-abort-raw/stack + <trace>.abort-<pid>.dmp.
- OPEN: D3D 11 query types (D3D11DDI_QUERY_PIPELINESTATS, SO STREAMn) were
  refused -> device removed (STATS smoke); fixed in 1eab272c, EXP1337 =
  HEAD + FL 11_0 default; if the set passes, land the FL 11_0 default.
- Other open: vertex-stage scratch; compute helpers; D3D12; GPU timestamps.

## Current as of 2026-10-16T13:02Z (read this first)

Installed: PACKAGE1322 (9a306fe1 KMD/ARM64 UMD, WOW64 UMD out63 on closure
083, GL ICD out61). FL 11_0 is opt-in (APPLE_AGX_D3D11_FL11=1; 6fb5de84).
- PROVEN today: FL 11_0 smoke on x86/ARM64 (EXP1313+); SO statistics exact
  with attachment-less renders (EXP1316, f07ec6b7); SGX core topology 8/0xfe
  (EXP1317); FL 11_0 corpus complete, pixel_diff 0 (EXP1318: 4_x programs
  with D3D 11 opcodes or global flags take the translator); shader scratch
  (EXP1322, f6d56647: the agx_build_pipeline projection had returned pipeline
  address 0 for every spilling shader -- dwmcore.dll_003d8800 now OK/OK, no
  corpus hang left, agx_scratch_smoke n<=96 PASS); BuildPagingBuffer never
  returns ALLOCATION_BUSY for UpdatePageTable/FlushTlb (EXP1321, aec9a109).
- OPEN (FL 11_0 default blocked): with 11_0 reported by default the desktop
  makes a broker page-table update fail (V5 STALE, EXP1320/1321); the
  process is poisoned and pool granules then stay Busy (UNMAP_RANGE loop,
  m1n1 console flood, boot stalls). Next: EXP1323 = HEAD + the FL 11_0
  default on a branch, with Wom1G3BrokerFailures (96674816) naming the op.
- Other open: vertex-stage scratch (refused at the gpuva gate, RunVertex
  uses_scratch not encoded); compute helpers refused; D3D12; GPU timestamps.

## Current as of 2026-10-16T08:19Z (read this first)

Installing: PACKAGE1315 (acbfd8b4 KMD/ARM64 UMD, WOW64 UMD out55 on closure
075, GL ICD out59).
- CONFIRMED offline (EXP1314): the FL 11_0 corpus crash is d2d1's ps_4_0
  with raw/structured buffers (D3D 11 opcodes 0xa2/0xa7), which only the
  FL 11_0 runtime admits; Mesa's SM 4 parser indexes opcode_info/
  opcode_xlate[D3D10_SB_NUM_OPCODES = 0x70] with an assert only. EXP1315
  routes such programs to the translator (854e396c) and runs the whole
  corpus at FL 11_0 (corpus-af, crash-restarting) as the checkpoint.
- CAUSE FOUND (offline): SO statistics 0 = the stats smoke's SO pass (no RTV,
  no DSV, null PS) is dropped at the native draw gate (reason 8); the KMD
  needs attachment 0. Next (EXP1316, D3D-G11): attachment-less renders --
  the device scratch page as attachment 0 (Asahi's unbound-output sink),
  builder admits NONE colour without ZLS, the frontend sizes the
  framebuffer by the viewports.
- Open: dwmcore.dll_003d8800 hangs on both paths; FL 11_0 default after
  EXP1315.

## Current as of 2026-10-16T07:15Z (read this first)

Installed: PACKAGE1313 (5c062072 KMD/ARM64 UMD, WOW64 UMD out53 and GL ICD
out59 on closure 073).
- PROVEN: every FL 11_0 case passes on x86 and ARM64 (agx_fl11_smoke8,
  EXP1313 same boot: U atomics 256/1515/1920 + append 86, T tessellation,
  I indirect draw/dispatch, B BC7). The append "defect" of EXP1306-EXP1312
  was the smoke's own size-mismatched CopyResource (a7623461); counter,
  CopyStructureCount and their destination were correct (EXP1311/1312).
- FL 11_0 still opt-in (APPLE_AGX_D3D11_FL11=1). SM 5 shaders always use
  the DXBC -> NIR translator; SM 4 keep TGSI by default (EXP1296: the
  translator as default faulted DWM).
- Running: corpus at FL 11_0 (corpus-af, same boot) -- the 64 SM 5 corpus
  shaders that FL 10_0 refuses, before making FL 11_0 the default (DWM
  would request it).
- Open: SO statistics written 0; dwmcore.dll_003d8800 hangs on both paths.

## Current as of 2026-10-16T05:30Z (read this first)

Installed: PACKAGE1310 (a690ad72 KMD/ARM64 UMD, WOW64 UMD out50 and GL ICD
out58 on closure 070).
- PROVEN in EXP1310: 4x MSAA everywhere (5b081276: TA tiles_per_mtile counts
  tiles, not utiles -- 4x renders had covered half/quarter of the target);
  API sample masks exact.
- OPEN (FL 11_0 default blocked by): CopyStructureCount returns 256 for 86
  appends; the appended slots are compact (counter advanced correctly) and a
  sentinel shows the copy writes 256 = w[0] of the raw words buffer -- the
  copy source (hidden counter identity/address) is the next target. The SR
  0x3b hypothesis is rejected.
- Everything in the 04:30Z section below still holds except the MSAA item.

## Current as of 2026-10-16T04:30Z (read this first)

Installed: PACKAGE1309 (72ff448b KMD/ARM64 UMD, WOW64 UMD out50 and GL ICD
out58 on closure 070). FL 11_0 stays opt-in (APPLE_AGX_D3D11_DDI +
APPLE_AGX_D3D11_FL11); WDDM 2.0 D3D11 DDI admits FL 11_0 devices.
- PROVEN since EXP1304: typed UAVs as indexed images (EXP1306, 4bed4b08);
  AGX immediate flags 64-bit under the MSVC ABI -- every subgroup
  reduction/scan was AND (EXP1308, 47998c0d: FL11 atomics 256/1515/1920,
  PSInvocations per pixel); indirect draws and tessellation on the native
  draw gate (EXP1309, ad3f3351: FL11 I and T ok); memory floor -- segment-3
  growth only while HighMemoryCondition is set (EXP1307/1308: PRESSURE gives
  E_OUTOFMEMORY, guest alive; HighMemoryCondition clears at 709-773 MiB
  available on 6 GiB); no cross-process disclosure in fresh allocations
  (EXP1307 write/scan).
- OPEN (FL 11_0 default blocked by): append counter ends at 256 for 86
  appends -- values correct, count = 32 per SIMD group: the uniform-atomic
  count in a divergent branch (SR 0x3b total_active_threads) returned 32.
- OPEN MSAA (EXP1309 msaadbg/msaactl): 4x draws reach the samples, but the
  lower-left triangle reads at the upper-right probe (180 degrees), large
  primitives (full-screen quad/triangle) are absent and the resolve is
  black; render-command sample fields equal Asahi's (measure-render-msaa).
- Other open: SO statistics written 0; dwmcore.dll_003d8800 hangs on both
  shader paths (corpus, 5 firmware recoveries); TDR v2 drill not rerun.
- Process: guest-watchdog.sh with every pipeline; fixphantom.py in
  prepare.sh removes non-present APPL0002 devnodes during rollback; never
  send shutdown while recover.sh runs.

## Current as of 2026-10-15T13:17Z (read this first)

Next: EXP1297 (newexp-kmd1297.sh, before1297.txt; KMD/ARM64 UMD from
49543b42, UMD32 out36 = closure 058, GL ICD out56): TGSI default, D3D11
interface default (DWM on the D3D11 table), timestamp queries, F/C/P
checkpoints carried from EXP1296. Installed: none (rec1296h done).
- EXP1296 REJECTED the translator as the default: DWM's translated shaders
  make GPU jobs time out (Wom1FirmwareRecovery 2435, kind 4 = firmware
  EventMsg::Timeout, not a page fault; DWM device removed), ending in
  bugcheck 0x116 from AdmissionDdiResetFromTimeout. Translator opt-in again
  (0a77b52c). Offline: texture/sampler indices, relative cbuffers, ICB
  sizes (64 B, no scratch) and the control-flow translation checked, no
  cause found -> the next causal target is the hanging shader itself:
  agx_corpus_smoke (being written) draws every corpus shader through TGSI
  and the translator in one process and names NIR-only hangs.
- Secondary defects found: agx_transfer_map dereferenced a failed staging
  allocation (fixed 0dd1e86c); a fault storm ends in 0x116 instead of
  device removal (KMD reset path; analysis in scratchpad codex-tdr-report:
  the KMD reads SGX fault info 0x17030 but not at the event).
- WHY CLEAN RECONSTRUCTION: n/a (current boundary is a translator
  correctness defect, localized by hardware differential, not archaeology).

## Current as of 2026-10-15T12:20Z

Running: EXP1295 (newexp-kmd1295.sh; KMD e93bb539, UMD32 out30), next
EXP1296 (newexp-kmd1296.sh, before1296.txt; KMD/UMD from b7504a5a).
- EXP1295 CONFIRMED (before its memory tests): compute on 10.x hardware
  through the D3D11 interface (cs_4_0 structured/raw/chained dispatches
  exact, x86 + ARM64); the D3D11 interface itself (8caf0e8e: the UMD runtime
  accepts D3D11.0) with every smoke PASS; every NIR smoke PASS including
  sample (5043da49: outputs stored once at the end).
- Since (offline, compiled in closures 053/055): the translator is the
  default shader path (937de32b; APPLE_AGX_D3D_SHADER_TGSI=1 opts out); hull
  and domain shaders (D3D-H4-tessellation.md) translated and wired;
  FL 11_0 pieces: typed/pixel/counter UAVs, CopyStructureCount, indirect
  draws/dispatch, BC6H/BC7, and a per-process FL 11_0 switch
  (APPLE_AGX_D3D11_FL11) with agx_fl11_smoke as its checkpoint.
- OPEN: MSAA interior pixels resolve to the clear colour (EXP1294:
  compression ruled out; the per-sample probe runs in EXP1296); STATS:
  PSInvocations 132 for 4096 pixels (one per SIMD group: suspected
  subgroup reduction of Asahi's uniform-atomic optimisation; EXP1296 case U
  discriminates) and SO statistics 0; PRESSURE intermittently hangs or runs
  16+ min (memory tests now last); FL 11_0 lists 8x MSAA, which AGX lacks
  (reported as 0 quality levels; whether the runtime enforces it is
  measured by EXP1296 F).

## Current as of 2026-10-15T10:14Z

Running: EXP1293 (newexp-kmd1293.sh; KMD 5be15abc = code 7dc938f2, UMD32
out25). Installed before it: PACKAGE1292 (rolled back by the pipeline).
- EXP1292 CONFIRMED: DWM faults 0 (2e8e1118 shader fields), 4x renders
  submit (44682cf6), Map of every format (41/42 exact, the 42nd saturates
  as D3D specifies), adjacency, VS-only stream output; CS 47.0 fps.
- OPEN: 4x MSAA resolves interior pixels to the clear colour while edges
  mix correctly; EXP1293 runs it with ASAHI_MESA_DEBUG=nocompress (c389e103)
  to test the lossless-compression hypothesis (one variable).
- D3D-H3 (investigation/analysis/D3D-H3-dxbc-nir.md): own DXBC parser
  (fxc-listing equal on 37 fixtures) and DXBC -> NIR translator (SM 4.x
  VS/PS/GS, SM 5 CS/UAV/TGSM/atomics/GS streams); 437 of 452 Windows system
  shaders (d2d1, dwmcore, dwmscene) translate and validate; wired behind
  APPLE_AGX_D3D_SHADER_NIR (per process); EXP1293's NIR block is its first
  hardware run. Next: NIR default -> D3D10.1 DDI (upstream SUPPORT_D3D10_1
  wiring exists) -> D3D11 DDI (H2).
- Recovery note: rec<X>h/recover.sh is not idempotent (a second run after a
  successful removal stops at the hidden phantom); see REC-EXP1291.

## Current as of 2026-10-15T08:41Z

Installed: PACKAGE1290 (D3D FL10_0; the next pipeline's rec1290h rollback
removes it). CS 46.6 fps, DWM faults 0; every earlier smoke unchanged with
the D3D-G10a/b generality (EXP1290).
- MSAA reporting SOLVED: the runtime creates devices under the cast-family
  policies (0x10, 0x20) and the screen caps probe (0x40).
- EXP1290 defects, fixed offline (EXP1291 candidate): every 4x render was
  refused at KMD submit branch 9 (the scene ACQUIRE records 1 sample and
  AdmissionG4PrivateGeometry compared it; scene storage is sample
  independent) -> 44682cf6; Map of a staging texture outside the bring-up
  format list removed the device (format matrix stopped after its first
  format) -> bfd3fed7.
- EXP1291 = the two fixes + adjacency (gs_smoke2 adj), statistics
  (stats_smoke), VS-only SO (so_smoke2 v): newexp-kmd1291.sh, closure 041,
  UMD32 out20.
- Pre-existing offline failures (not this work): test_g3_vidmm_replay (stale
  shim, 44), test_g4_primary_import_alignment, test_g4_r105_open_normalization.

## Current as of 2026-10-15T07:53Z

Installed: PACKAGE1289 (D3D FL10_0; MSAA reporting off by default; the next
pipeline's rec1289h rollback removes it). CS 46.6 fps, DWM faults 0.
- CONFIRMED (EXP1288/1289): predication, general SO, pixel-shader exports
  at the end, every D3D-G9 sampling op, depth-only prepasses (D3D-G7).
- MSAA: the runtime's undocumented rule, from six refusals: levels need
  MULTISAMPLE_RENDERTARGET on a typed format; read-only cast-family members
  answer neither; a member may not exceed its typeless parent. Corrected
  policy ad01c0e3, to be probed in EXP1290.
- D3D-G10 (investigation/analysis/D3D-G10-generality.md): G10a/G10b
  committed (targets, views, clears, copies, gate formats, KMD 8/16-byte
  classes, screen caps behind probe bit 0x40); EXP1290 = agx_format_smoke.
- Pipeline helpers vanish from the scratchpad: copies in
  .local/tandem/gpu-pipeline and .local/agx-lab/bin; resume from the probe
  step without reinstalling (resume1289b.sh pattern).

## Current as of 2026-10-15T07:02Z

Installed: PACKAGE1288 (D3D FL10_0 works, MSAA reporting off by default;
the next pipeline's rec1288h rollback removes it). CS 46.0 fps, DWM faults 0.
- EXP1288 CONFIRMED: predication (render condition before the batch),
  general stream output (SoSetTargets per DDI; three-slot SO still refused by
  the runtime), pixel-shader exports at the end (outputs to temporaries) and
  the D3D-G9 explicit-LOD/gradient/comparison/size sampling ops.
- EXP1288 measured the runtime's MSAA reporting rule (undocumented): every
  cast-family member answers its typeless parent's levels; no
  MULTISAMPLE_RENDERTARGET on read-only members; count 1 never asked.
- Open: depth-only draws dropped by AgxWin32AsahiBatchDrawAllowed (needs a
  colour buffer); the sampling tool's implicit-LOD op is invalid (non-uniform
  flow). FL10_1 needs 4x on every RT format, FL11_0 also 8x (AGX max 4x):
  investigation/analysis/D3D-H-ddi-plan.md.

## Current as of 2026-10-14T23:12Z

Installed: PACKAGE1287 (D3D works with MSAA off; the MSAA switches are
default off; the next pipeline's rec1287h rollback removes it). m1n1 fa45bc58
r2, INF 1235 copy, x86 ICD out56 (CS 45.5 fps, DWM faults 0).
- EXP1287 CONFIRMED (C): the multisample caps/quality reporting alone makes
  the runtime refuse D3D devices (0x887A0020); the screen's multisampling is
  not a cause. The runtime's rule is undocumented in the pages read; the
  EXP1287 answers cover only some members of each cast family and give 0
  levels for SampleCount 1 (the DDI page requires 1).
- EXP1287 found four defects (fixes = EXP1288 candidate, one checkpoint
  each): predication (CPU render condition retires the batch the draw wrapper
  entered: evaluate it before taking the batch), depth-only prepass (zero-byte
  tile-buffer sample refused; Mesa/Asahi allow 0), SoSetTargets (EXP-era
  one-VB|SO guard), fragment exports inside control flow (tgsi_to_nir stores
  before every RET; lower outputs to temporaries as HK does).
- Confirmed (EXP1284, again EXP1287): occlusion queries, D3D depth-stencil,
  8 MRT, GS SO, every regression.

## Current as of 2026-10-14T22:02Z

Installed: PACKAGE1285 (D3D BROKEN: every D3D device fails, DWM restarts;
the next pipeline's rec1285h rollback removes it). m1n1 fa45bc58 r2, INF
1235 copy, x86 ICD out56 (GL/CS unaffected, 45.2 fps).
- EXP1285 REJECTED: the D3D-G5 frontend's MSAA caps disagreed with its
  quality levels (depth formats: levels without MULTISAMPLE_RENDERTARGET;
  RGBA8/RGBA16F: MULTISAMPLE_LOAD without sampling), and the runtime
  refused every device after CreateDevice (0x887A0020); dwmcore 0x889800b0.
  Fixed 16b900e7 (test_d3d10_msaa_caps_consistency). Depth-only, MSAA,
  predication not reached.
- EXP1286 candidate (KMD/ARM64 from HEAD after 16b900e7, UMD32 out14 /
  closure 034): caps fix + MSAA + depth-only + predication + stream output
  generality (D3D-G8 a2072cc0) + render-target initial data (036c5dcd).
- Confirmed so far (EXP1284): occlusion queries, D3D depth-stencil, 8 MRT /
  32x16 utiles. Plan for FL 10_1 / 11_0: investigation/analysis/D3D-H-ddi-plan.md.

## Current as of 2026-10-14T21:18Z

Installed: PACKAGE1284 (integration 12d6a6fd + UMD32 out9/closure 029; the
next pipeline's rec1284h rollback removes it). m1n1 fa45bc58 r2, INF 1235
copy, x86 ICD out56.
- EXP1284 CONFIRMED on both D3D UMDs: occlusion queries (D3D-G3a, exact
  counts; KMD isp_oclqry_base JP1 +0xd0 / JP3 +0x4d8 + query-function fix
  f9014fa3), D3D depth-stencil (D3D-G4: D24S8 stencil, D32, D16, 1024x768,
  depth-only clears), tile buffer by Mesa's rule (D3D-G6: 8 RGBA8 targets,
  32 B/sample, 32x16 utiles). Regressions unchanged (CS 46.05 fps).
  Predication (D3D-G3b) not reached (tool defect, fixed 93e9a742).
- Implemented, EXP1285 candidate (package built: KMD/ARM64 704fb754,
  UMD32 out11/closure 031): D3D MSAA 2x/4x + resolve (D3D-G5: 5939e809,
  dfc039cc, DDI-bit fix f36213d7; GL ICD keeps AGX_DBG_NOMSAA), depth-only
  passes (D3D-G7 88186697: NONE colour class, depth buffer as attachment 0),
  predication rerun.
- Known UMD gaps: render targets with initial data (E_NOTIMPL), copies of
  multisampled resources, SO beyond one POSITION output (plan: map TGSI
  output semantics to NIR slots for every pipe_stream_output), timestamp /
  pipeline-statistics queries.
- NEXT: EXP1285, then SO generality (D3D-G8), then the D3D11 DDI / FL 11_0 /
  SM5, ARM64X UMD.

## Current as of 2026-10-14T20:50Z

Installed: PACKAGE1283 (integration 17a15a3d + UMD32 out7/closure 027; the
next pipeline's rec1283h rollback removes it). m1n1 fa45bc58 r2, INF 1235
copy, x86 ICD out56.
- EXP1283: every regression step as EXP1282 (CS 46.98 fps, GS/MRT PASS,
  DWM faults 0). D3D-G3a occlusion queries REJECTED by a UMD projection
  defect: native-asahi-batch-lifecycle.py deleted agx_init_query_functions,
  so pipe->create_query was NULL (CreateQuery killed the process) and
  active_queries false. Fixed in f9014fa3 (test_d3dg3_query_functions); the
  KMD heap path (05268308: isp_oclqry_base JP1 +0xd0 / JP3 +0x4d8, 256 KiB
  writable) is still unexercised.
- Implemented, not yet on hardware (EXP1284 candidate, separate checkpoints):
  D3D-G3b predication (364bffc2), D3D-G4 D3D depth-stencil (464ad92b: ZLS for
  the D3D UMD, no 1 MiB bound, ClearDepthStencilView depth/stencil subsets),
  D3D-G6 tile buffer by Mesa's rule (6516cec6: 8-byte multiples, utile fits
  32767 bytes; 8 RGBA8 targets = 32x16 utiles), MSAA KMD encoding (5939e809,
  neutral while the screen keeps AGX_DBG_NOMSAA).
- NEXT: EXP1284 (queries rerun qx/qa, predication px/pa, depth x24/x32/a24/a16,
  MRT x8/a8 plus the EXP1283 set), then the MSAA UMD (D3D only; GL keeps
  NOMSAA, EXP1162), depth-only passes (winsys requires a colour target),
  SO generality, the D3D11 DDI / FL 11_0 / SM5, ARM64X UMD.

## Current as of 2026-10-14T18:48Z

Installed: PACKAGE1282 (integration f6913c92 + UMD32 out6/closure 026; the next
pipeline's rec1282h rollback removes it).
m1n1 fa45bc58 r2, INF 1235 copy, x86 ICD out56, WOW64 UMD32 out5 (closure 025).
- ONE UMD SOURCE (EXP1279 CONFIRMED): KMD/ARM64 UMD are built from the
  integration tree (scratchpad mkicd.py with WOW_ICD/WOW_UMD), not
  build/exp1211-kmd-compute; DWM/ARM64 apps now run the same Mesa batch
  lifecycle as the x86 ICD/UMD32 (CS 46.7 fps, desktop, BIG, pressure,
  long-job + hang all as before, same DWM PID).
- D3D-G1 (compute inside a render submission): v5 envelope admitted, bound
  and re-proven on hardware (EXP1278), but the CPU-sequenced prelude
  DEADLOCKED (0x116): the shared Queue notifier threshold counts TA+3D+compute.
  D3D-G1b (e9f48fe7, implemented): compute, TA and 3D in one submission, the
  TA queue led by a G13 firmware Barrier on the compute stamp (Asahi
  queue/mod.rs pattern, captured WorkCommandBarrier layout). EXP1280
  CONFIRMED: D3D10 geometry shaders and stream output run on the GPU on both
  the WOW64 UMD32 and the ARM64 UMD (agx_gs_smoke gs/so, 240 frames read
  back, no stale layout, no recovery, no TDR). Installed: PACKAGE1280.
- Process: keep each experiment's KMD PDB in its directory (EXP1278's was
  overwritten; analysis used the identical-source build with SYMOPT_LOAD_ANYTHING).
- The integration INF now runs the KMD from the driver store (7929519e,
  InfVerif Universal); installed packages still use the 1235 INF until a
  driver-store install experiment.
- D3D-G2a (EXP1281 CONFIRMED): the firmware fragment attachment list is the
  submission's (real sizes, depth/stencil). D3D-G2b (EXP1282 CONFIRMED): MRT,
  2 and 4 RGBA8 targets on both D3D UMDs (tile buffer still 8/16 bytes per
  sample in the KMD template).
- NEXT: D3D-G3 queries (isp_oclqry_base in JobParameters1 work+0xd0 and
  JobParameters3 work+0x4d8, occlusion cap, d3d10umd OCCLUSION/PREDICATE/SO
  statistics), then MSAA, stream-output generality, wider tile buffers, the
  D3D11 DDI / FL 11_0 / SM5, ARM64X UMD for x64 apps.

## Current as of 2026-10-14T16:55Z

Installed: PACKAGE1277 (build tree 5137c009 = M2 complete + G4-r1 + G4-r2a +
G4-r2b; the next pipeline's rec1277h rollback removes it). m1n1 fa45bc58 r2.
- M2 (flexible memory) COMPLETE (see 16:00Z entry).
- G4 (GPU recovery) COMPLETE for every hang the firmware detects (EXP1277):
  G4-r1 the KMD deadline never stops the firmware (4 jobs past 500 ms
  completed); G4-r2a 3D progress interval 900 ms (valid 0.1-1.3 s jobs run);
  G4-r2b a dropped job is DXGK_INTERRUPT_DMA_PAGE_FAULTED -> only that app's
  device is removed (DXGI_ERROR_DEVICE_HUNG), DWM/system continue, no TDR,
  no bugcheck. Remaining (not observed): a hang the firmware does not detect
  reaches dxgkrnl's TDR, where ResetEngine/ResetFromTimeout cannot restart
  retained firmware (0x116) -- firmware-preserving ResetEngine is later work.
- Open side task: paging_windows.c still uses the reserved
  DXGK_INTERRUPT_DMA_FAULTED (EXP896 0x119) -- proposed as its own session.
- Resubmission defect: no occurrence in EXP1265-EXP1277 (no
  Wom1G4SubmitFailure); stays evidence-driven.
- NEXT: the D3D phase (investigation/analysis/D3D-phase-plan.md step 3,
  groundwork: compute work inside render submissions, MRT, MSAA, queries,
  then the D3D11 DDI / FL 11_0 / SM5).
Tools: agx_longjob 229c110c (fastest-of-three calibration, 'hang' step),
agx_mempressure 458c5881 (budget lines, RESTORE readback).

## Current as of 2026-10-14T16:00Z

Installed: PACKAGE1275 (build tree 728f0e47 = M2 complete + G4-r1; the next
pipeline's rec1275h rollback removes it). m1n1 fa45bc58 r2 (b720f8e0).
M2 (flexible memory) COMPLETE on hardware:
- M2-f5 reserve at creation (EXP1265), M2-f6 segment 3 from the RAM-sized
  m1n1 broker (EXP1273: 2688 MiB on J313), M2-f7 General BO bound (EXP1269),
  M2-f9a TVB window cap (EXP1270), M2-f10 D3D canonical copies in segment 3
  (EXP1271), M2-f11 UMD residency trim (EXP1274/EXP1275: budget refusals
  trimmed and retried, restored content verified 0xffbf8040).
- M2-f8 stays a hardware limit (CPU-visible class BOs at segment-3 exhaustion).
G4 (recovery) is the active target:
- G4-r1 (3ed6a8ee) installed, INCONCLUSIVE: its 500 ms deadline is never
  reached.
- G4-r2 OPEN (EXP1275 long-job step): the firmware kills a render that does
  not yield to a context switch within cl_kill_timeout_ms (Asahi 50 ms;
  recoveries 5, kind 4 Timeout, max 48 ms). The KMD then resumes the
  firmware but completes the dropped render to Windows as success (silent
  loss). Next (offline): the KMD path that requests a context switch during
  a running job, the preemption granularity reported to dxgkrnl, and the
  Windows DDI that reports a dropped packet as faulted.
- Then the resubmission defect, the D3D groundwork and FL 11_0
  (investigation/analysis/D3D-phase-plan.md).

## Current as of 2026-10-14T15:00Z

Installed: nothing (REC-EXP1272 done: Package Absent, APPL0002 Code 28);
EXP1273 running (M2-f6 retry). M2 status:
- M2-f5, M2-f7, M2-f9a VALIDATED; M2-f10 VALIDATED (EXP1271: D3D canonical
  copies in segment 3; 8 x 64 MiB targets render; pressure 46 x 32 MiB).
- M2-f8 hardware limit (CPU-visible class BOs at segment-3 exhaustion).
- M2-f6: EXP1272 INCONCLUSIVE (my m1n1 build lacked AGX_LOCAL_RESERVE_V2;
  Mu PrePi "reserve receipt v3 required"); build script fixed (88429cd0),
  m1n1 fa45bc58 r2 macho b720f8e0; EXP1273 = KMD dc9b14e7 + r2.
- M2-f11 implemented (f9d2704f, not run): UMD residency trim loop on
  MakeResident E_OUTOFMEMORY (EXP1271 pressure ended in a refused batch).
  Next hardware run after EXP1273: build tree + f9d2704f.
- Then G4-r1 (3ed6a8ee), D3D.
Recovery note: a firmware stop after staging needs the staged-only cleanup
in the hidden profile (REC-EXP1272, memory firmware-stop-staged-only-recovery).

## Current as of 2026-10-14T14:10Z

Installed: PACKAGE1270 (build tree 23a6b18b = EXP1269 + M2-f9a; the next
pipeline's rec1270h rollback removes it). M2 status:
- M2-f5 VALIDATED (EXP1265), M2-f7 VALIDATED for creation (EXP1269),
  M2-f9a VALIDATED (EXP1270: 2048x8192 targets render; TVB capped at the
  32-block buffer-manager window, firmware partial renders, no fault).
- M2-f8 is a hardware limit (CPU-visible class BOs demote to 4 KiB system
  pages at segment-3 exhaustion).
- M2-f10 OPEN (EXP1270): the D3D UMD gives every resource above 16 MiB
  (ADMISSION_UMD_SYSTEM_DIRECT_BYTES) a GPU-local class canonical BO plus
  staging, and CPU-invisible class BOs were confined to segment 2 (312 MiB
  firmware reserve): 4 x 64 MiB or 7 x 32 MiB targets exhausted it (copy
  QUERY predicate 56, NoTable -> device reset). Fix f3c20566: segment 3 only,
  reserved at creation. Build tree 9da91ab6 for EXP1271 (KMD only).
- Then M2-f6 (m1n1 fa45bc58 + KMD e0b1b4b4) as EXP1272 (pipeline
  newexp-kmd1272-m2f6.sh, needs post-clone.sh for the macho swap), G4-r1
  (3ed6a8ee), D3D.

## Current as of 2026-10-14T13:04Z

Installed: PACKAGE1269 (build tree 7493dffd = c78a3d9d M2-f5 + 83984bc0
M2-f7; the next pipeline's rec1269h rollback removes it). M2 status:
- M2-f5 VALIDATED (EXP1265), M2-f7 VALIDATED for BO creation (EXP1269:
  32 MiB and 64 MiB render targets are created).
- M2-f8 is a hardware limit (EXP1266-EXP1268): segment-3 exhaustion demotes
  class BOs to 4 KiB system pages the UAT cannot map -> app DEVICE_HUNG.
- M2-f9 OPEN (EXP1269): rendering into a target above ~2048x2048 fails in
  the private scene acquire (escape returns D3DERR_OUTOFVIDEOMEMORY
  0x8876017C; UMD trace reject-private-escape: 1, W 0x800, H 0x1000/0x2000,
  0x2020); the private scene storage comes from m1n1's fixed 64 MiB private
  local pool (HV_AGX_GPUVA_V5_PRIVATE_BACKINGS) and the TVB heap blocks
  (APPLE_AGX_G3_PRIVATE_HEAP_BLOCKS). Next: find the exact refusing check
  (PRIVATE_CAPTURE site in gpuva_g3_windows.c private acquire) and size the
  scene storage per render, from flexible memory.
- Winsys: the backend also fails after a refused private acquire (26a03fde
  only covers BO creation).
- Then M2-f6 (m1n1 broker table sized from RAM), G4-r1 (3ed6a8ee), D3D.

## Current as of 2026-10-14T10:52Z

Installed: PACKAGE1268 (rejected M2-f8c; the next pipeline's rec1268h
rollback removes it). M2 status:
- M2-f5 VALIDATED (EXP1265): reservation at CreateAllocation, equals use.
- M2-f8 CLOSED AS A HARDWARE LIMIT (EXP1266-EXP1268, reverted 6c1177ea):
  at segment-3 exhaustion VidMm makes a CPU-visible class BO resident in
  4 KiB system pages; the 16 KiB-page UAT cannot map them; the submission is
  refused and the app gets DEVICE_HUNG (system stays up). dxgmms2 forces an
  aperture into CPU-visible sets, and an aperture in the set permits
  system-memory residency whatever its size or preference. Mitigation:
  segment-3 capacity (M2-f6). Future: VidMm 64 KiB system pages, or a KMD
  shadow for demoted BOs.
- EXP1269 running/next: M2-f7 alone (build tree 7493dffd = c78a3d9d +
  83984bc0), General BO bound = segment 3; BIG step 8 x 64 MiB RTs.
- Then: winsys refusal fix (26a03fde, UMD), M2-f6 (m1n1 broker table sized
  from RAM, capacity register for the KMD), G4-r1 (3ed6a8ee), D3D.

## Current as of 2026-10-14T10:15Z

Installed: PACKAGE1267 (build tree 3ca71538, M2-f8b, rejected; the next
pipeline's rec1267h rollback removes it). M2 status:
- M2-f5 VALIDATED (EXP1265): CreateAllocation reserves segment-3 pages;
  reservation equals use (idle ~200 MiB, CS peak ~680 MiB); CS 46.7 fps.
- M2-f8 OPEN, the critical gap. At segment-3 exhaustion VidMm demotes a
  CPU-visible class BO into the aperture. The GPU cannot map its 4 KiB
  backing, so the next submission is refused (G4 Parse Unmapped) and the app
  gets DXGI_ERROR_DEVICE_HUNG (EXP1265-D, EXP1267; the system stays up).
  dxgmms2 rule (primary source, EXP1267 entry): CPU-visible allocations must
  include an aperture (WDDM >= 3.0). Segment 3 alone was refused (EXP1266);
  not preferring the aperture still demotes (EXP1267). Next M2-f8c: a
  dedicated aperture segment smaller than the 64 KiB allocation unit in the
  class-BO sets, so admission holds and demotion cannot happen.
- M2-f7 implemented (83984bc0, not run): General BO bound = segment 3
  (was the 16 MiB legacy arena; EXP1265-C refused 2048x4096 RGBA8).
- UMD: a refused BO no longer fails the winsys backend (26a03fde, not run).
- M2-f6 open: m1n1 broker table 2 GiB caps segment 3 at 1792 MiB.
Tools: agx_mempressure.exe (leave/max/height args), agx_longjob.exe; runner
scripts mempressure.ps1/arm-mempressure.ps1, longjob.ps1/arm-longjob.ps1.
Pipelines newexp-kmd1265..1267.sh (pressure step at the end).
G4-r1 (3ed6a8ee, deadline no longer stops firmware) is still not run.
Order: M2-f8c -> M2-f7 + winsys fix -> M2-f6 -> G4-r1/r2 -> D3D.

## Current as of 2026-10-14T09:13Z

Installed: PACKAGE1264 (unchanged). Correction to the 08:30Z entry: M2's core
is validated (EXP1262-EXP1264), but two gaps keep M2 open (user asked to close
memory before D3D):
- M2-f5, backing under memory pressure (investigation/analysis/
  M2-f5-backing-guarantee.md). Offline: a segment-3 commit failure inside
  UpdatePageTable poisons the owning process, and for the paging process that
  is system-wide. Fix e663766a reserves pages at CreateAllocation and returns
  STATUS_NO_MEMORY there. Build tree c78a3d9d (= 45234b93 + e663766a) is for
  EXP1265 with agx_mempressure.exe; not run yet.
- M2-f6, broker capacity: m1n1's static broker table (131072 x 16 KiB = 2 GiB)
  caps segment 3 at 1792 MiB on every Mac (the J313 RAM formula would give
  ~3.75 GiB). Next after EXP1265.
G4-r1 (3ed6a8ee, implemented, not run): the 500 ms KMD deadline no longer
stops the firmware (the EXP1241/EXP1242 0x116 cause). It needs its own
experiment (agx_longjob.exe) after M2.
Order: EXP1265 (M2-f5) -> M2-f6 -> G4-r1/r2 -> resubmission defect -> D3D.

## Current as of 2026-10-14T08:30Z

Installed: PACKAGE1264 (KMD build tree 45234b93, 30.0.1264.0) on m1n1 23d30a32
+ Mu 361a2134; guest OS with KB5127648.
M2 (flexible memory) is COMPLETE on J313 (EXP1262-EXP1264):
- Segment 3 is backed on demand from 2 MiB slabs: idle ~0.2 GiB, CS peak
  ~0.68 GiB, given back after use.
- Its declared size follows Windows' graphics-memory formula for the machine:
  1792 MiB on J313, capped by m1n1's 2 GiB broker.
- Segments 2+3 are the LOCAL budget group and the aperture is NON_LOCAL
  (DXGI LOCAL budget 1788 MiB).
- CS 46.2-46.3 fps, equal to the fixed pool on the same OS; every functional
  check passes.
Tools: agx_vidmem (DXGI/KMT accounting) and agx_segstats, in the scratchpad
and in .local/scratchpad-tools-backup.
Next causal targets:
1. The resubmission defect (branch 7 subsite 0x201). The cancel-caller
   receipt is in the build; it needs a refusal to occur.
2. G4-recovery/TDR.
3. D3D 11/12 (survey the UMD/DDI state first).

## Current as of 2026-10-14T07:48Z

Installed: PACKAGE1262 (KMD build tree e49b48aa = integration a15d2aea slabs +
8861c4a2 cancel-caller receipt, 30.0.1262.0) on m1n1 23d30a32 + Mu 361a2134;
the guest OS has KB5127648.
M2-f3 is complete on J313 (EXP1262, investigation/analysis/M2-f3-demand-pool.md).
Segment 3 (1 GiB declared) is backed on demand from 2 MiB slabs:
- Idle: ~0.19 GiB, against 1 GiB pinned before.
- CS benchmark: grows to 0.66 GiB and is given back after the game.
- Speed: CS 46.07 fps, equal to the fixed pool on the same OS (46.36).
- 0 failures, 0 Unmapped IPA, every functional check passes.
- Rejected on the way: churn as a cause (EXP1258), 16 KiB chunks (EXP1261).
- KB5127648 alone costs ~1.2 fps against the pre-update EXP1255.
Next causal targets:
- the M2 follow-ups as separate variables: segment 3 size (demand-backed,
  so it can follow Windows' TotalSystemMemoryAvailableForGraphics, capped by
  the broker's 2 GiB of system records) and budget groups;
- the resubmission defect (branch 7 subsite 0x201; the cancel-caller receipt
  is in this build);
- G4-recovery;
- D3D 11/12.

## Current as of 2026-10-14T04:29Z 

Installed: PACKAGE1261 (16 KiB-block demand pool, rejected; package left for
the next rollback) on m1n1 23d30a32 + Mu 361a2134; guest OS has KB5127648.
M2-f3 demand pool: proven to work in EXP1257-EXP1261 (idle ~0.2 GiB vs 1 GiB
pinned, peak ~0.68 GiB, returned after use, 0 failures, 0 Unmapped IPA).
The fps cost was isolated. EXP1259 is the fixed pool on the same OS: 46.36 fps,
so the OS update costs ~1.2. The remaining cost comes from Windows' per-chunk
page allocation:
- per granule: 43.9 fps;
- run commits: 45.1 fps, valid (EXP1260);
- 16 KiB chunks: 43.6 fps, rejected (EXP1261).
Active: M2-f3e, 2 MiB slabs (integration a15d2aea, build tree e49b48aa with the
receipt-only cancel-caller 8861c4a2); EXP1262 is prepared (before1262.txt,
newexp-kmd1262.sh).
BLOCKER: the Windows builder 192.168.1.24 refuses SSH/RDP since ~02:40Z
(ping TTL 64: the host is up, the VM is probably stopped); the user was
notified. A bounded wait is in the background.
Then: segment 3 up to ~1.75 GiB (broker 2 GiB of system records) and budget
groups as separate variables; the resubmission defect (EXP1260: slot
cancelled, not preempted; the cancel caller is now in the receipt);
G4-recovery; D3D 11/12.

## Current as of 2026-10-14T01:20Z

Installed: PACKAGE1257 (KMD build tree f55d89a3 = integration 6d34a70f,
30.0.1257.0) on m1n1 23d30a32 (macho b122bc5f, pool window MAP_AT/UNMAP_RANGE)
+ Mu 361a2134. The guest OS gained KB5127648 at 2026-10-14T00:55Z.
M2-f3 proven (EXP1257, investigation/analysis/M2-f3-demand-pool.md):
- Segment 3 (1 GiB) is backed on demand, one 64 KiB granule at a time.
  A granule is committed before the first PTE referencing it is published.
  It is released only when it has no owner placement, no paging-process PTE
  and no unexecuted record, and the window unmaps it.
- Idle desktop: 195 MiB committed instead of 1 GiB pinned. CS peak 691 MiB,
  back to 220 MiB after the game. 0 failures, 0 Unmapped IPA; all checks pass.
- CS 44.3 fps vs 47.6 (EXP1255). Commit/release churn: VidMm reuses freed
  ranges at once, ~8k commits and releases per benchmark, 6.5 s of commit time.
Rejected: EXP1256 pinned-pool sizing policy (memory pressure, reverted).
REC lesson: a Windows servicing restart preempted rec1256h; see
REC-EXP1256-B and memory rollback-servicing-restart.
Next causal target: M2-f3b, aged release (a granule is released only after
2 s idle; periodic reclaim timer). Its fps separates churn from the OS
update. Then the larger segment 3 and budget groups as separate variables;
the resubmission defect (branch 7 subsite 0x201), G4-recovery, D3D 11/12.

## Current as of 2026-10-13T23:00Z

Installed: PACKAGE1255 (KMD 32fde092 = production pool + receipt-sized
reserve, 30.0.1255.0) on m1n1 3b2b9c06 (macho 283a2354, 384 MiB reserve) +
Mu 361a2134 (J313_EFI-m2f2c.fd 1c7d87d6, reserve ABI v3).
M2-f2c-1/2 proven:
- The AGX local reserve size is a receipt parameter (ABI v3) in m1n1, Mu PEI,
  the generated AGX0 _CRS and the KMD (EXP1254/EXP1254-B at 1 GiB).
- m1n1 selects 384 MiB on J313 (EXP1255): Windows RAM 6.28 GB (+640 MiB),
  segment 2 312 MiB (benchmark peak 160 MiB), every check passes.
Lab note: scratchpad helpers can vanish (run-probe.sh did); backup in
.local/scratchpad-tools-backup, maintained copy tools/agx-lab.
Next causal target: M2-f2c-3 (KMD pool size per machine, today fixed 1 GiB,
CS peak ~690 MiB); then CPU-invisible class BOs into the pool, the ACPI
declaration of the pool window, the resubmission defect (branch 7 subsite
0x201), G4-recovery, D3D 11/12.

## Current as of 2026-10-13T21:25Z

Installed: PACKAGE1253 (production KMD 9434b468 = integration c96a34e4
drivers/, 30.0.1253.0) on m1n1 b12d1952 (macho fe86bee1) + M1 FD 4d382bfa.
M2-f2 (flexible GPU memory for Mesa BOs) is complete on hardware:
- EXP1252: the 64 KiB-block fallback through the window (16,183 blocks,
  mapped in 107.9 ms); EXP1253: production build on a boot where Windows had
  no 1 GiB block -> fallback, chunks from 0x850470000 accepted after m1n1
  b12d1952 (boot inputs are guest RAM for GPU backing; EXP1251 had refused
  them), 59,592 pool groups published, 0 unpublished, CS 48.1 fps.
- Measured (agx_segstats, D3DKMTQueryStatistics): segment 2 (m1n1 reserve,
  952 MiB) peaks at 167 MiB in CS 1.6; segment 3 (pool, 1 GiB) at 668/688 MiB.
Next causal target: M2-f2c-1 -- receipt-sized local reserve (ABI v3 in m1n1,
Mu + ASL generator, KMD; ~20 pinned tests), S unchanged; then f2c-2 (m1n1
picks a smaller S, e.g. 384 MiB) and f2c-3 (pool size per machine). Design
and inventory: investigation/analysis/M2-f2c-reserve-size.md. Then the
resubmission defect (branch 7 subsite 0x201), G4-recovery, D3D 11/12.

## Current as of 2026-10-13T20:20Z

Installed: PACKAGE1250 (KMD 0cb63858, 30.0.1250.0) on m1n1 fc59c8cb (macho
31471866, M2-f2 pool window on M2-d) + M1 FD 4d382bfa.
Proven since 15:30Z:
- M2-f2b (EXP1249, contiguous pool): CPU-visible Mesa class BOs live in
  segment 3 (placement {3,1}); 60,016 pool 16 KiB groups published as System
  backings, 0 unpublished; every check unchanged (CS 48.2 fps).
- The single 1 GiB block is opportunistic (EXP1248: NULL in 0.1 ms; EXP1249/
  EXP1250: granted), so segment 3 now goes through a hypervisor window: m1n1
  fc59c8cb presents KMD-allocated guest RAM as one contiguous IPA window
  (J313 0xd00000000, 4 GiB; broker+0xe00 MAP_APPEND/UNMAP_ALL; design
  investigation/analysis/M2-f2-pool-window.md). EXP1249: the prepared
  window is inert. EXP1250: segment 3 CpuTranslatedAddress = window IPA
  (outside the RAM map, PopulatedFromSystemMemory) is accepted; broker
  publishes window IPAs (IPA != PA) at CS scale (62,084 groups, 0
  unpublished); every check passes.
Open: the 64 KiB-block fallback has not run on hardware (Windows granted the
block in EXP1250) -> EXP1251 diagnostic forces it. Known intermittent KMD
defect seen again in EXP1250: dxgkrnl resubmission of a preempted G4 job
refused (Wom1G4SubmitFailure branch 7 subsite 0x201, see below).
Next causal targets: EXP1251 (chunk fallback); M2-f2c (pool size per
machine, ACPI declaration of the window, CPU-invisible class BOs into the
pool, shrink the m1n1 reserve to scanout/tables/firmware); then the
resubmission defect and the G4-recovery items; then D3D 11/12.

## Current as of 2026-10-13T15:30Z

Installed: PACKAGE1245 (build 62502977: local-preferred placement + every
fix since EXP1238; 30.0.1245.0) -- EXP1245 CONFIRMED the stable baseline:
CS 1.6 benchmark 48.0 fps rendered, copy x2, D3D11, GL DXGI PrintWindow
MATCH x3, DWM faults 0, no G4 refusal, no class BO in system memory.
Proven since 12:55Z:
- VA-only render targets work end to end (EXP1242: probe + GL smoke PASS
  with system-memory colour attachments) after ad894162 (render packet
  destination) and bdd1e258 (EXP208 relocation target without CPU bytes).
- M2-c system-memory placement is NOT viable (EXP1240-EXP1244): VidMm maps
  CPU-visible class BOs with 4 KB PTEs ~99% of the time and backs every
  shape partly with scattered 4 KiB pages (Wom1G3SystemShapes, EXP1244);
  identical KMD descriptions, so no allocation shape is 16 KiB-safe. Effects:
  CS renders refused (G4 Parse Unmapped), compute copy GPU timeout -> 0x116.
- A backend build failure or GPU timeout leaves the fence pending and, with
  reset unsupported, ends in 0x116 (EXP1241, EXP1242 kernel dumps).
Decision: placement reverted to local-preferred (6e728357); M2 flexibility
moves to a KMD-populated memory segment (PopulatedFromSystemMemory, backing
taken from Windows at start, 16 KiB-contiguous) -- plan in
investigation/analysis/M2-vidmm-64k-decision.md (addendum 2026-10-13).
EXP1246 (pool probe, receipt only, every check unchanged): Windows grants
the KMD a 1 GiB physically contiguous write-combined block at StartDevice in
123.5 ms (MmAllocatePagesForMdlEx REQUIRE_CONTIGUOUS_CHUNKS, SkipBytes 0;
0x980000000, 1 GiB-aligned); 2 MiB / 64 KiB chunking also gets 1 GiB (3.9 s /
2.3 s). Installed: PACKAGE1246 (d874bbb2 = EXP1245 + probe).
EXP1247 CONFIRMED M2-f2a: the adapter runs with segment 3 (1 GiB block at
0x980000000, PopulatedFromSystemMemory, unused); every check unchanged.
Installed: PACKAGE1247 (2b347a8a). Design: investigation/analysis/
M2-f2-populated-segment.md (revision: third segment for Mesa class BOs).
Next causal target: M2-f2b -- CPU-visible class BOs prefer segment 3, i.e.
the third, CPU-visible PopulatedFromSystemMemory
memory segment backed by that KMD-owned block (CpuTranslatedAddress = block
base; segment offsets -> IPA = base + offset, 16 KiB-contiguous by
construction; UAT publication as registered system backings; CPU paging
executor and transfers for segment 3; CPU-visible class BOs prefer it; block
freed at StopDevice). Then shrink the m1n1 reserve (HV_AGX_LOCAL_BYTES 1 GiB
-> firmware/table needs) and size the segment per machine.
Robustness (investigation/analysis/G4-recovery.md): firmware Timeout/Fault
events are already recovered (EXP1242: 178 resumes); engine reset must stop
restarting retained firmware (always 0x116) and the KMD deadline must not
pre-empt the firmware's own detection; fault-injection checkpoint pending.

## Current as of 2026-10-13 12:55Z

Installed: PACKAGE1238 (KMD build 30f9e034: M2-a/M2-b receipt/M2-c step 1;
ICD out56 73cbdc6d without the GDI ring; UMD32 out4; INF Supports64KBPages;
30.0.1237.0) on m1n1 4da6528f (M2-d broker index) + M1 FD 4d382bfa,
full-owner from the integration worktree.
Proven:
- G2 complete (EXP1236-B, EXP1238): CS 1.6 standard benchmark fullscreen
  through the ICD's DXGI flip presenter, 47.4 fps, no ring compiled in
  (df09806b + 92f83041); windowed GL 59.6 fps PrintWindow MATCH.
- M2-d (EXP1236): m1n1 broker backings indexed by PA/IPA, 2 GiB system
  pages; REGISTER/REVOKE_BACKING 12.4/10.4 -> 3.5/3.3 us.
- M2-c step 1 (EXP1237): Mesa class BOs without AccessedPhysically admitted.
Running: EXP1239 (M2-c step 2: CPU-visible class BOs aperture-only = system
memory; expected first Wom1G3Leaf64KPtes > 0). If 64 KB pages do not come
(Leaf64KPtes 0 + GPU fault/0x116, EXP1026 class), decode dxgmms2's allocation
bit 21 / adapter cap bit 14 exclusions (M2-vidmm-64k-decision.md).
Next after EXP1239: non-CPU-visible GPU allocations with the aperture in
their set, aperture/local sizing, then local reserve shrink (M2-e); G2
performance follow-up: present cost (fence wait ~4 ms + copy) -- render into
the swap chain buffer / GPU-side wait.

## Current as of 2026-10-12 08:10Z

Installed: PACKAGE1235 (KMD/ARM64 UMD build eb951f1d = 7eb593d9 + M2-a
04bea5f5 + Wom1G3Leaf64KPtes receipt 5e76de65; INF with
MemoryManager\Supports64KBPages = 1 (097091ae); ICD out55, UMD32 out4;
30.0.1235.0) on the M1 set, full-owner from the integration worktree.
Proven: dxgmms2 gives an allocation 64 KB system pages only with the adapter
opt-in (Class and Video keys both carry it now) + SysMem64KBPageSupported +
an aperture in the segment set + 64 KiB-multiple size + not CPU-visible with
a CPU-visible local segment (investigation/analysis/M2-vidmm-64k-decision.md).
EXP1235: opt-in + M2-a safe (all checks pass) but no allocation qualifies
under today's placement (Leaf64KPtes never written; UnpublishedGroups[0] 913
from section-backed class0 CDD/GDI surfaces).
Next causal target: M2-d -- the m1n1 broker's system-backing registry must
hold UMA-scale backings (v5: 4096 x 16 KiB = 64 MiB, linear scans; R143
section "Grant and DART limits" asks for an indexed/range design, not a bigger
array), then M2-c (GPU allocations with the aperture in their set, aperture
segment > 256 MiB) as the 64 KB discriminator. CS bench still waits for Steam.

## Current as of 2026-10-12 07:20Z

Installed: PACKAGE1234 = EXP1233's package (KMD/ARM64 UMD 7eb593d9, x86
closure 024: UMD32 out4 b97adbe2, ICD out55 2ce3c9e7, 30.0.1231.0) on the M1
launch set (m1n1 6ee9cc4d, Mu 71c9b2d551 J313_EFI-m1.fd 4d382bfa).
Proven: EXP1233 -- x86 closure 024 behaves as out54/out3 (stack frames
gated); EXP1234 -- M1: no ramdisk => the window 0x860000000 + 1 GiB is
Windows RAM (TotalPhysicalMemory 4,534,439,936 -> 5,608,181,760 B, +1 GiB
exactly) with every GPU check unchanged. m1/ramdisk-window is merged into
integration (b7905c4a, submodules m1n1 6ee9cc4d / mu 71c9b2d551); EXP1235+
full-owner must use the integration worktree with the M1 macho/FD.
Next causal target: M2 (UMA). First offline question: how this guest's
VidMm (dxgmms2.sys, not dxgkrnl.sys) reads DXGK_ALLOCATIONINFO's
Alignment / MinimumPageSize+RecommendedPageSize union for a WDDM 3.2
driver -- the KMD writes Alignment 0x10000, i.e. Min 0 / Rec 1; DXGK_PAGE_SIZE
has no definition in WDK 26100 -- and when it issues 64 KB system pages
(SysMem64KBPageSupported is declared; EXP854/EXP1004 never saw a
Use64KBPages update). CS bench still waits for a Steam sign-in; then remove
the GDI ring. Redirected-blt track: dxgkrnl asserts
"!RedirectedBlt || hDestination == NULL" and routes DxgkPresentRedirected /
D3DKMT_PM_REDIRECTED_BLT separately (strings in this guest's dxgkrnl.sys).

## Current as of 2026-10-12 07:00Z

Installed: PACKAGE1232 (KMD/ARM64 UMD 7eb593d9, UMD32 fce5f2db, ICD out54
422f5eb5 = DXGI flip presenter, 30.0.1231.0).
Proven (EXP1232): G2 HW3c -- windowed GL on screen without the GDI ring:
the ICD blits the GL colour buffer into a shared D3D11 texture it opened,
waits ~0.8 ms for the blit, and its own D3D11 device (WOW64 UMD) copies it
into a flip-discard swap chain and presents; PrintWindow MATCH x3, 59.6 fps,
no DWM fault. APPLE_AGX_PRESENT=dxgi|redirect|ring selects the path.
Still open: legacy redirected (blt) presentation unserviced (see 06:30Z).
Next causal target: CS 1.6 standard benchmark on the DXGI path (fullscreen
GL), then x86 closure 024 binaries (stack-frame fixes for UMD32/ICD), then
remove the GDI ring, then M1 hardware run.

## Current as of 2026-10-12 06:30Z (read this first)

Installed: PACKAGE1231 (KMD/ARM64 UMD build 7eb593d9 = c92ac343 + the stack
fixes, UMD32 fce5f2db, ICD out53, 30.0.1231.0). Proven this session:
- 32-bit D3D11 on AGX, on screen (EXP1226, EXP1231): WOW64 enumeration
  (SupportsOnly64Bit from UserModeDriverNameWow), FL 10_0 device, flip
  swap chain, presents composed by DWM (screen and PrintWindow blue).
- DWM stack overflow fixed (EXP1231): the UMD reserved 32-89 KB frames
  on DDI paths; all moved to the heap; tools/pe_stack_frames.py gates
  images at 16 KiB (x86 UMD32/ICD still need the x86 closure rebuilt).
- opengl32 redirection protocol fully driven (EXP1227-EXP1230):
  PresentBuffers v2 -> dwmapi #100 surface -> DrvPresentBuffers(hSurface)
  -> PresentToRedirectionSurface nVersion 3 (only 3 accepted) -> D3DKMTPresent
  RedirectedBlt; the ICD writes the surface (readback proves it).
Open boundary: redirected (blt) presentation is unserviced -- dxgkrnl never
calls DxgkDdiPresent Blt (EXP1231-D2 probe: 0 this boot) and DWM composes the
surfaces empty, for GL and for DXGI blt-model D3D11 alike. Separate track:
DxgKrnl ETW (EXP801DxgBoot) to see how dxgkrnl services redirected blts for
this GPU-VA, SupportKernelModeCommandBuffer=0 driver (needed for D3D9 and
blt-model D3D10/11 windows).
Next causal target (G2): the ICD presents GL frames through a DXGI flip-model
swap chain it owns (D3D11 on AGX in the GL process), replacing opengl32's
redirection and the GDI ring; then CS fullscreen, then M1 hardware run
(m1n1 6ee9cc4d, Mu 71c9b2d551 FD 4d382bfa, worktree .worktrees/m1-ramdisk-window).

## Current as of 2026-10-12 03:25Z (read this first)

Installed: PACKAGE1225 (= EXP1224's: sys 3f6db5bb, UMD 23123147, UMD32
7430c7b9 in SysWOW64 under UserModeDriverNameWow, ICD 5aede15d, INF d4e258f7
form, 30.0.1218.0); EXP1226 (KMD c92ac343) is being installed.
Proven (EXP1225, offline trace of this guest's dxgkrnl.sys + pinned WDK):
32-bit processes never saw the AGX adapter because the KMD itself answered
DXGKQAITYPE_64BITONLYCAPS SupportsOnly64Bit = 1 (since 2d34b0ae). dxgkrnl
reads that cap once at adapter start and skips a 64-bit-only adapter in
every WOW64 enumeration (FilteredAdapterFromWow64Process); OpenAdapterFromLuid
from WOW64 still works and names the WOW UMD. So the WOW64 UMD registration
(EXP1223 driver-store form, EXP1224/1225 SysWOW64 form) was never exercised,
and EXP1224's hang cannot be the WOW64 UMD (HV interrupt-liveness class).
Fix (81d5ec62, build c92ac343): the cap follows the installed
UserModeDriverNameWow (driver key); every UMD-to-KMD wire record is
layout-identical for x86 and ARM64 (tests/test_apple_agx_wow64_private_abi.py).
Active hypothesis EXP1226: with the cap 0, x86 enumeration lists 0x4d236, the
32-bit D3D11 smoke runs on AppleAgxRenderAdmissionUmd32.dll, and opengl32's
PresentBuffers gets a redirection surface (G2 HW3b). Tools: agx_kmt_umdname.c
(x86/ARM64 enumeration diagnostic), agx_d3d11_smoke.c (x86 D3D11 smoke).

## Current as of 2026-10-12 02:45Z (read this first)

Installed: PACKAGE1222 (.local/experiments/EXP1222-g2-hw3b-pb-modules): KMD
build commit 19db0d1a on build/exp1211-kmd-compute (= EXP1217 KMD + the
Wom1G2PresentDdi receipt; sys 3f6db5bb, UMD 23123147), ICD out50 5aede15d
(integration e8319531), 30.0.1218.0. Timedemo playnoovl 50.3 fps, smoke 57.4.
Standing operator rule (2026-10-12): the bench is always free -- no hl.exe
gate, no confirmation; keep going autonomously until D3D 11/12 is done.
Proven on hardware this session:
- G3 CONFIRMED (EXP1219): GL swaps are paced on the display's vertical blanks
  (D3DKMTWaitForVerticalBlankEvent from the x86 ICD on the KMD's vsync
  timeline): smoke 59.6 fps, 119-120 waits per 120 swaps, 0 failures, swap
  intervals centred on the period; late CS frames present without waiting.
- G2 HW3 legacy path REJECTED (EXP1218): D3DKMTPresent(Blt, hWindow) from the
  ICD returns STATUS_GRAPHICS_VIDPN_SOURCE_IN_USE while DWM owns the source,
  before the KMD.
- G2 HW3b protocol measured (EXP1220-EXP1222): opengl32 hands the ICD
  PresentBuffers/PresentToRedirectionSurface; PresentBuffers nVersion 2
  creates a D3D11 device in the calling process (dwmapi, dxgi, d3d11 loaded)
  and fails with ERROR_MOD_NOT_FOUND because this adapter has no 32-bit D3D
  UMD (no UserModeDriverNameWow); nVersion 3 then calls DrvPresentBuffers
  back synchronously with no surface; nVersion 1 is refused.
The ICD keeps its ring (GPU copy + CPU copy + SetDIBits) as the shown path;
PrintWindow of a GL window matches its frame.
Next causal target: a WOW64 (x86) build of the D3D UMD registered as
UserModeDriverNameWow (needed by opengl32's redirection and by 32-bit D3D
games), then rerun the redirection present (EXP1220 protocol, nVersion 2).
Then remove the ring (G2 done), M1+M2 (memory; surveys in
investigation/analysis/M1-ramdisk-window-survey.md and M2-uma-fact-base.md).

## Current as of 2026-10-12 00:30Z (read this first)

Installed: PACKAGE1217 (.local/experiments/EXP1217-g2-present-gpu): KMD
1945e5da on build/exp1211-kmd-compute (sys 3c74c641, UMD 6693b714), ICD out45
c5955939, EXP1194 INF, 30.0.1217.0. Timedemo playnoovl 49.9 fps, smoke 56.7.
Proven on hardware since HW1 (EXP1214):
- HW2a (EXP1215): every process's KMD private range is reserved at
  BaseAddress 0x11FE000000 (top 32 MiB of the USC window; d3dkmddi
  BaseAddress); G4 private scenes render there (dump: 23 reserved, 0 refused).
- HW2b-1 (EXP1216): every GPU process gets a 128 KiB present arena (kernels
  installed, pages published) at context creation (26-29 made, 0 refused,
  VA 0x11fe020000); two KMD compute jobs per queue lifetime complete
  (persistent work-queue state); timedemo 50.2 fps (EXP1216b).
- HW2b-2 (EXP1217): GPU present routing installed, no regression, but
  inert: no client presents by blt in a virtual context (DWM flips, the GL
  ICD uses its GDI ring); Wom1G2PresentPath never written.
Unrelated hazard: guest interrupt-liveness hangs under the m1n1 HV (EXP1215
0x101: a CPU in user mode stops receiving clock ticks; EXP980 class) -- a
separate investigation was proposed.
Bench hygiene: macOS cleans /tmp -- /tmp/agx-lld-dir/ld.lld (m1n1
proxyclient linker, copy of .local/toolchain22/ld.lld) and session
scratchpad scripts can vanish; generated recover.sh restores the linker,
pipeline scripts are kept in .local/tandem/gpu-pipeline. The evidence
archive lives on /Volumes/SSD (symlinked from /Users/pavel/J313-evidence-archive).
Next causal target: G2 HW3 -- the OpenGL ICD presents through
D3DKMTPresent (Blt, hWindow; dxgkrnl redirects to DWM), so the GL process
submits present blts that the GPU path runs; then remove the GDI ring.

## Current as of 2026-10-11 17:10Z (read this first)

Installed: PACKAGE1214 (.local/experiments/EXP1214-g2-compute-notifier):
KMD 0ff4b470 on build/exp1211-kmd-compute (sys bb40b1ac, UMD 0091b823),
ICD out45 c5955939, EXP1194 INF, 30.0.1214.0.
Proven on hardware (EXP1214): G2 HW1 done -- the first KMD-built AGX compute
job (UMD AGX4 compute envelope -> KMD RunCompute in the compute sidecar,
VM slot 1, doorbell 6, event 2) runs and completes its fence:
glCopyBufferSubData COPY_BUFFER PASS 32773 bytes, then rendering continues
(30 frames glGetError 0); smoke 56.5 fps, timedemo playnoovl 49.0 fps.
Defects found on the way (all fixed with RED->GREEN): RunCompute unwired in
provider/composer/KMD (EXP1211, 34cf2e00); compute doorbell 6 refused
(EXP1212, d548536f); deadline poll refused ComputeSubmitted (EXP1213,
703e8acc); the job had a private notifier with a zero threshold pointer
(EXP1213 dump, 2110fd9e: the compute job now uses the Queue notifier --
template EventControl, object 11 -- and event_count = 2 * render sequence +
compute jobs since queue initialisation, Asahi queue/mod.rs).
Still true: a worker-side refusal or a timeout turns into an unrecoverable
TDR (0x116, reset defect, G5); keep compute checks on the submit side.
Known follow-up before multi-job compute: the compute work queue gpu_buf
(sidecar +0xc000, Asahi per work queue, persistent) is wiped with the
sidecar on every BuildCompute.
Next causal target: HW2a -- KMD private VA reservation
(DxgkCbReserveGpuVirtualAddressRange BaseAddress 0x11FE000000, top of the
USC window) with a receipt and a render regression; then HW2b (present blt
as a KMD-owned compute job built by AppleAgxPresentBuild), HW3 (ICD present
through D3DKMTPresent).

## Current as of 2026-10-11 15:10Z (read this first)

Installed: stable GPU-visible recovery profile (no AppleAgx package) after
EXP1211 (rec1211h RECOVERY_DONE, Preflight PASS).
G2 decision (operator, option 1): the present blt runs on the GPU as a
KMD-built AGX compute job; design docs/superpowers/plans/2026-10-11-g2-gpu-present-blt.md.
Implemented (host-tested): standalone compute jobs in the backend/G13 queue
provider (279e21fb), the AGX4 compute envelope (efac9430, version 4,
APPLE_AGX_G4_COMPUTE_ENVELOPE 112 bytes), KMD admission as a destination-less
compute packet that binds only the envelope and builds RunCompute in the
compute sidecar in VM slot 1 with the envelope's user-VM context-store
buffer (6ef5ea6e), Mesa compute-only batches submitted by the winsys
(62e100da), smoke copybuffer mode (26f7a2b9), CDM/USC encoder checked
against Mesa's packer (54b567b9).
Proven on hardware (EXP1211): the compute-capable KMD leaves render
unchanged (smoke PASS 57.6 fps, timedemo 49.8 fps); a copybuffer envelope is
admitted, bound, begun (JOB_BEGIN) and built for the firmware.
Proven defect (EXP1211 dump, fixed 34cf2e00): RunCompute was not wired
through the platform provider, composer and KMD runtime IO -- the backend
refused the fence in the worker, and the resulting TDR reset failed
(0x116). A worker-side refusal still turns into an unrecoverable TDR
(reset restarts the backend and fails, as in EXP1075): every compute-path
check must stay on the submit side.
Next causal target: EXP1212, the same copybuffer probe with the wiring fix
only (KMD = build/exp1211-kmd-compute + 34cf2e00, ICD out45 unchanged).
After the first standalone compute job: HW2 (KMD present blt: KMD USC
arena via DxgkCbReserveGpuVirtualAddressRange BaseAddress in the USC window,
own copy kernel compiled with the pinned AD04 Mesa compiler, encoder above).

## Current as of 2026-10-11 12:50Z (read this first)

Installed: PACKAGE1210 (.local/experiments/EXP1210-g4-preempt-overlap):
KMD 0ed494a3 (= 1151 KMD + preemption/resubmission receipts, 7486debc,
built as 54d3a4df on d8c1c5c7 so the UMD keeps the 1151 sources), UMD
7dbfa63b, ICD f504307c (EXP1209), EXP1194 INF, version 30.0.1210.0.
Roadmap: docs/superpowers/plans/2026-10-11-agx-driver-roadmap.md (G1-G5
OpenGL, M1/M2 flexible memory before Direct3D, 64-bit OpenGL, ANS, D3D).
G1 (EXP1197 resubmission refusal): the overlap race is rejected -- 0 of
8961 preemptions arrived while SubmitCommandVirtual was inside the driver,
and 2989 resubmissions were accepted (EXP1210). The refusal did not recur;
Wom1G4SubmitFailure now records the refused predicate mask and the
scene/slot state (g4rcpt.py in the EXP1210 directory) for the next one.
Next causal target: G2, present through D3DKMTPresent (Blt, hWindow) with
the KMD's DxgkDdiPresent blt -- first establish whether present_windows.c
executes the blt on the GPU or the CPU.
Guest C: 70 GB free (REC-20261011-DISK).

## Current as of 2026-10-11 10:55Z (read this first)

Installed: PACKAGE1209 (.local/experiments/EXP1209-cs16-swap-grid, ICD
f504307c on closure-022, INF OpenGLFlags 3 from EXP1194). Benchmark: play
43.2 fps, noovl 49.8 fps (the timedemo average is now capped by the
operator's gl_vsync 1: median frame 17.05 ms).
Proven since 10:15Z:
- Swap intervals are paced on a one-period grid (3de8fe45, EXP1209): 95%
  of the swaps one refresh period apart (stw rule: 51%, alternating), the
  present worker shows 92% of the frames (82%). APPLE_AGX_SWAP_PACING=stw
  is the diagnostic control.
- With the overlay the game thread is CPU-bound (20 ms); EXP1208 profile:
  the overlay's own code plus the GL 1.1 entry through the CHPE opengl32
  (x86 -> native -> x86 per call) dominate; the ICD draw path is ~0.3%.
Next causal targets: (1) the remaining 8% dropped shown frames (worker
11.9 ms per show inside a 16.7 ms period; GDI readback path, 10 ms of
uncached reads per frame -- zero-copy presentation is the architectural
fix); (2) KMD resubmission defect (separate task).

## Current as of 2026-10-11 10:15Z (read this first)

Installed: PACKAGE1207 (.local/experiments/EXP1207-cs16-shadow-pools, ICD
67e10a37 on closure-022, INF OpenGLFlags 3 from EXP1194). Benchmark
(EXP1184 harness, operator demo): play 41.6 fps, noovl 51.7 fps.
Proven since 08:45Z:
- x86 scalar stores into write-combined memory run at 20 MB/s under the
  emulator (1.3 GB/s cached; memcpy into WC 15 GB/s, EXP1206). Recording
  the batch pools and the encoder into cached shadows and copying them
  into the allocations at submission (bfb8679e, EXP1207) cut
  agx_encode_state 12.0 -> 1.7 us/draw and draw_vbo 21.9 -> 8.8 us/call;
  game-thread CPU noovl 15.4 -> 11.2 ms. Four submissions in flight
  (98a2bdd3) and the diagnostics opt-in (APPLE_AGX_ICD_PHASES=1) stay.
- The operator's config.cfg has gl_vsync 1: swaps are paced to 60 Hz
  (stw semantics), so without the overlay gameplay sits at the cap
  (median frame 17.1 ms) and the game thread sleeps ~5 ms per frame.
- The present worker shows 82% of the frames: 12.4 ms per shown frame
  (readback copy 10.5 ms + DIB 1.9 ms), ~43 shown fps.
Next causal targets: (1) with the overlay (operator's normal setup) the
game thread is CPU-bound at 18.4 ms; its 6.5 ms over noovl is not the
draw path (+59 draws) -- attribute it with one sampled overlay run;
(2) the present readback copy (frames actually shown); (3) KMD
resubmission defect (separate task, KMD owns it).


## Current as of 2026-10-11 08:45Z (read this first)

Installed: PACKAGE1200 (.local/experiments/EXP1200-cs16-staging-names, ICD
902170f7 on closure-017: real pipe fences, present ring polling copy fences,
winsys framebuffer, receipts; INF 29f6d97e OpenGLFlags 3). Benchmark after
EXP1194: play 32.6-34.8 fps (spread), noovl 47.7-47.8 (52-53 fps gameplay).
Frame split (EXP1197, play 26.9 ms): Asahi draw path 7.9 ms (388 draws x
20 us), flush 0.3, present 0.6, kernel waits 0.3, rest 18.2 ms (engine,
Steam overlay UI, Mesa GL front end). noovl 19.2 ms: draw 6.5, flush 1.6 +
present 1.8 (both mostly GPUVA retire waits: two submissions in flight,
three per frame), rest 9.5.
Learned since 05:40Z:
- Non-blocking fences (1baa075c) and the polling present ring (d21a45b7) are
  correct but gave no fps: the previous copy was always done at present
  time (EXP1195); the present time is the copy submission's retire wait.
- glthread still loses with the overlay UI (26.8 fps; its GL state queries
  drain the worker); +2% without it (EXP1196). Stays opt-in.
- Lightmap staging blits happen only at map load (EXP1200); the
  decompress-on-update rule was removed (f019f5be).
- KMD defect: dxgkrnl resubmission of a preempted G4 private job is refused
  (Wom1G4SubmitFailure branch 7 subsite 0x201, EXP1197) and faults the GL
  context; seen once in noovl after the per-frame waits were removed.
  Separate task proposed (KMD owns it).
Next causal targets: KMD resubmission fix, then GPUVA in-flight depth
(UMD SubmissionHolds<=1 and winsys Older must change together), CPU per
draw (20 us) and the Mesa front end under emulation.

## Current as of 2026-10-11 05:40Z (read this first)

Standard benchmark (EXP1187+): timedemo of the operator-recorded movement
demo cstrike\agxbench.dem (3771 frames, de_dust2; copy in
.local/experiments/EXP1187-cs16-operator-demo/evidence/agxbench-operator.dem)
via .local/experiments/EXP1184-cs16-bench/bench.sh (modes play, playnoovl =
SteamNoOverlayUIDrawing=1 for the benchmark's hl.exe only, playsample).
The scripted demos of EXP1184/EXP1186 were observer views (joinclass never
spawned the player): their 40.5 fps measured a static scene.
Results (timedemo fps): EXP1187 baseline 34.2-35.2 / noovl 44.3 (EXP1188);
EXP1194 34.6 / noovl 47.7 (gameplay frames 52 fps without the overlay UI).
Accepted package: PACKAGE1194 (.local/experiments/EXP1194-cs16-openglflags):
sys 407bffbc / UMD bbe106b3 + INF 29f6d97e (OpenGLFlags 3) + ICD 514a0070
(out31 on closure ICD-x86-closure-014-ndebug; receipts on with the trace).
Contracts learned (do not relearn):
- The operator's Steam overlay performance monitor draws quad by quad in
  immediate mode (gameoverlayrenderer 0x87d37-0x87ed7): ~22% of the frame
  (EXP1188); operator-side setting.
- Microsoft's opengl32 calls the ICD's glFinish before every swap-buffers
  unless OpenGLFlags bit 1 is set (Microsoft Learn "Loading an OpenGL
  Installable Client Driver"; EXP1193/1194). Set: 3.
- Mesa's WGL frontend waits for every frame (ST_FLUSH_WAIT) unless the window
  has a winsys framebuffer; the ICD now provides one (agx_wgl_framebuffer.c,
  EXP1191/1192).
- The native draw entry must legalize feedback loops/xfb before taking the
  batch (upstream order; EXP1189 fault, fix d3ae649e, closure-014).
- Fence-wait sites: measure-wgl-wait (+ measure-wgl-waitf_<module>) with
  the ICD relink map (relink-mapNN.ps1, /MAP; static functions only in the
  map, not in the PDB publics).
- The game-thread sampler under Prism attributes time to the last
  indirect-branch target: reliable per module, approximate per function.
Remaining costs: play is CPU-bound (26 ms/frame: ICD ~56% of the game
thread -- agx_build_pipeline/encode_state/pool, immediate-mode dispatch --,
overlay UI ~14-17%); noovl waits on GPUVA's two in-flight submissions
(retire_held 0.43/frame x 1.37 ms) and the present-ring sync (1.6 ms).


## Current as of 2026-10-10 21:35Z (read this first)

CS 1.6 plays fullscreen 2560x1600 on the AGX OpenGL ICD (operator sessions
EXP1167-EXP1177; no reject-batch, no first-fault, no TDR in any session).
Accepted package: PACKAGE1180 (.local/experiments/EXP1180-cs16-wrapper-exit):
PACKAGE1151 sys 407bffbc / UMD bbe106b3 + INF f7db56b7 + ICD 57e6a8a8.
Frame-rate history (gameplay medians from measure-wgl-present receipts):
EXP1169 two-slot ring 10.5 fps (show 71 ms: SetDIBits from the
write-combined staging map) -> EXP1171 cached copy in 4 slices 27 fps ->
EXP1173 present worker (3-slot mailbox) 37.3 fps; automated de_dust2 spawn
EXP1172 56.7 fps. EXP1175 UMD slot hints: correct, no measurable change
(37.5). EXP1177 glthread: stable but 29 fps -> opt-in only (AGX_GLTHREAD=1).
Where gameplay time goes (EXP1173 kernel receipts, EXP1174 sampler): kernel
calls 5-6% of the frame; game thread ~56% GL driver (agx_build_pipeline,
immediate-mode dispatch/vbo, pool alloc, GL_QUADS index generation), ~16-20%
Steam overlay/client loop, ~6% engine.
Contracts learned this session (do not relearn):
- Reading GPU staging memory from the CPU goes through a write-combined
  mapping: copy it in concurrent slices into cached memory before GDI
  (EXP1170/1171); never SetDIBits straight from the mapping.
- WGL glthread needs the frontend's set_background_context hook (EXP1176) and
  wrappers must not touch stw state once DllMain cleared stw_dev (EXP1177
  crash at game exit); both fixed.
- The automated CS harness closes only its own hl.exe (cs-run1172) and must
  not run while the operator is at the machine; an abort must also stop the
  guest cs-run/shots processes (EXP1171, EXP1178, EXP1181).
- Rollback can lose the serial lock to a lingering launcher; retry the
  recover script without a second shutdown (REC-EXP1177 note).
Open: TDR recovery for G4 private jobs (EXP1167 bugcheck 0x116 when a stuck
hl.exe faced a reboot); occasional small artifacts reported by the operator;
Steam overlay cost; zero-copy present (D3DKMTPresent/redirection) as the
"real driver" path; ARM64 ICD.
Guest C: free must stay >= 4 GB (stage gate); traces archived in
.local/experiments/guest-trace-archive-20261010 and -20261010b.

## Earlier: 2026-10-10 14:55Z

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
