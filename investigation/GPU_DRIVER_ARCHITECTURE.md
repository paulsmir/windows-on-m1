# AppleAgx GPU driver: target architecture and change policy

Owner: GPU driver architect (fork 6). Living document; every architectural decision
links here. Status words: **DONE** (in the default build, hardware-validated),
**KEEP-OFF** (merged, correct, opt-in until its consumer or proof exists),
**BUILD** (designed, not implemented), **OPEN** (needs evidence).

## 1. Change policy: what stays in the driver

A change is classified *before* it is measured, and the class decides what an
FPS-neutral result means.

| Class | Definition | Keep when | Default |
|---|---|---|---|
| C — contract | Makes the driver follow a Microsoft (WDDM/D3D12/DXGI) or AGX/firmware contract it violated | correct + no regression, regardless of FPS | on |
| I — infrastructure | Mechanism a later optimisation needs (receipts, queue/barrier encodings, trackers, gates) | correct + tested + zero cost when off | off until its consumer lands |
| O — optimisation | Exists only for speed | measured gain beyond noise in both ABBA pairs | on only with gain |
| D — diagnostic | Receipts, tools, probes | useful + zero cost when off | off |

Rules:
- An FPS-neutral **C** change is kept and enabled. Neutral means "the contract
  was not on the critical path of this workload", not "useless".
- An FPS-neutral **I** change is kept (off) when it is correct; it is removed only
  if it is wrong or blocks something.
- Only **O** changes are rejected for lack of gain.
- Every package passes the performance gate (`tools/agx_perf_gate.py`, 2 %),
  so keeping C/I changes cannot hide a regression.

## 2. Target architecture (production WDDM 3.x driver for G13)

### 2.1 Submission and scheduling
- **DONE** GPUVA (DxgkDdiSubmitCommandVirtual) path; one KMD worker; depth-2 job
  window with compute-on-3D, compute-on-compute (prelude) and TA-on-3D/compute
  classes (EXP2088-2134); Q2c: compute on its own Asahi queue (EXP2189).
- **DONE** preemption state machine incl. the Claim->Commit race fix
  (15b47216, hardware check pending).
- **DONE (C)** a refused depth-2 kick falls back to depth one instead of
  resetting the engine for refusals before the firmware sees it (S1-S3,
  be8c6f7f + 55ea8d68 for the boundary-preemption race; EXP2222: 1415/1415
  fallbacks, no reset; investigation/analysis/DEPTH2-KICK-FALLBACK.md);
  **DONE (C)** S4a: a SubmitSuccessor refused before RunSuccessor gives the
  built successor back and runs it at depth one (20e34f05; EXP2224: 1380/1380
  build_undone fallbacks incl. 2 inside a boundary preemption, no reset;
  EXP2225 perf gate PASS on the S4a default package
  ); **OPEN** S4b (QueueFailed
  with the provider guard before PublishPreparedWork) needs the same proof on
  the provider/queue-runtime side; S4c/S5 stay reset (possibly visible).
- **DONE** firmware->host rings (stats/ktrace/fwlog) drained like Asahi
  (gpu.rs) after every job, default on since 125bddd1 (EXP2217 drain proof,
  EXP2219 perf gate PASS; Wom1FwRxDrain = 0 disables).

- **KEEP-OFF (C), steps 1-2 DONE (EXP2272/2273: core freed, FPS neutral, detect is firmware/HV latency)** interrupt-driven job completion (ASC mailbox recv-not-empty AIC 578 = vINTID 887 -> ISR/DPC ->
  worker) replacing the predictive sleep+spin poll on the critical path (Wom1JobPoll: 320 us mean late wake per
  sleep, 49 % CPU busy; ~0.3-0.5 ms/frame). Design: investigation/analysis/KMD-IRQ-COMPLETION.md.
  **BUILD (C) default-on (architect spec 2026-11-09, same note, "Default-on plan")**: before the mailbox line enters
  the production Mu the ISR must dismiss the level line in every state (not ready, not armed, unmapped, ring full);
  then IRQD1 (inert on the current Mu) and IRQD2 (candidate Mu, same-boot ABBA) flip the default.
  **MERGED 2026-11-09 (212f09e0e + arm-order correction 84b91a7cb)**: Wom1IrqCompletion defaults on, inert without
  the ACPI line; ISR/DPC reach the runtime only through Context->MailboxIsrRuntime (close after the ASC stop,
  reopen = arm, then publish). Not MSVC-compiled yet; hardware IRQD1 then IRQD2 pending (fork 6).

- **BUILD (C), DESIGNED 2026-11-09** timeout-only TDR recovery: when the firmware never retires a job,
  DxgkDdiResetFromTimeout's quiesce returns STATUS_DEVICE_HARDWARE_ERROR and
  Windows bugchecks 0x116 (EXP1964/1965, EXP2100, EXP2214, EXP2283-2286). Design:
  investigation/analysis/TDR-FIRMWARE-RESTART-RECOVERY.md (7bedfb1fa). Asahi has no firmware restart (crash
  latch, timeout FIXME); the reference is macOS (stopFirmware/bootFirmware). "Cannot be restarted" was never
  tested on hardware (EXP589 never ran). ResetFromTimeout = stop the ASC and prove silence;
  RestartFromTimeout = boot and re-publish. Stages: 1 (D) reset receipt v5; 2 (I) restart mechanism, off;
  **H1** idle warm restart of a healthy firmware (unknown U1); 3 (C) warm path + withheld-kick stall drill (H2);
  4 (C) cold path with a new m1n1 broker op (H3); 5 default on.
  **Stage 2a MERGED 2026-11-09 (fe119f211)**: shared-layer warm stop/start/restart entry points, called by
  nothing. U4, U8, U9 settled from sources (design note section 9): the warm publish must not send a second
  ACTIVATE; macOS halts cooperatively and resumes in two phases, so T2 ranks the two-phase resume before H2.
  Next: stage 2b (KMD primitives, AdmissionPlatformRuntimeRestart, drill escape) and stage 1 receipt v5, then H1.

### 2.2 Dependencies (D3D12 -> AGX)
Target: order work only by what D3D12 orders (ResourceBarrier) and by
driver-internal hazards -- the same per-resource model Apple's kext uses
(per-resource read/write usage, stamps, MTLFence/Event; EXP2201, kext RE).
- **KEEP-OFF (C/I)** OV2 independence flag + ranges rule (resource-level, the
  D3D12 contract); OV3b deferred query-resolve copies only into an existing
  prelude (no new preludes); OV5 UMD TA-independence verdict; OV5 3D-queue
  compute-stamp Barrier (firmware accepts it, EXP2210); backend successor rule
  for a waived TA lead (56772879).
- **KEEP-OFF (C/I), CLOSED 2026-11-09** OV5 TA waiver: the GM-start 0x116 was a
  kick-timing race (the waived successor's doorbell reaching the firmware before
  the running compute had started; EXP2283-2293), fixed by the compute-start gate
  (defer the waived kick until the compute's firmware start timestamp is nonzero,
  6f18cfb8; class C). Stable: 0 resets in 12 GM starts with diagnostics off
  (EXP2293-2297). ABBA EXP2294-2297 (waiver on top of the OV2 lead, diag off):
  +0.02 % / -0.28 %, inside noise -> no gain, stays off. Only R8 is waivable
  (R2/R4/R6 verdict 10: VB read vs UAV write, OV6b); the measured overlap
  (EXP2282: 601 of 11261 pairs, median 0.54 ms) is too small to show in fps.

- **CLOSED 2026-11-09 (GravityMark)** OV8 full independence: Apple's 2.2 ms/frame
  overlap is C4||R5 and C6||R7 with the whole render ahead (split HD capture),
  but GravityMark-D3D12 orders both pairs itself (transition UAV->PS_SRV before
  R7, UAV barrier before R5 on the same 7.4 MB surface; EXP2298/2299). Under the
  D3D12 contract the overlap is not ours to take. OV2/OV5/OV8 stay KEEP-OFF
  (C/I) for applications whose barriers permit them.

### 2.3 Memory and residency
- **DONE** G3 private scenes, UAT, broker; read-only opens answered as data and
  proved for reading only (025ab1d6, EXP2209: CS 1.6 45.5 fps).
- **DONE** tiled + lossless-compressed RT/DS/SRV like Asahi/Apple
  (shader-image targets decompress, as on Apple G13).
- **BUILD (parked)** G3 lock split (option A, IRQL-modelling tests mandatory).
- **DONE / KEEP-OFF** HV stage-2: above-RAM isolation (HVS2ISO001) and in-RAM
  carveout unmap (HVS2ISO003); Mu carveout reservation (HVS2ISO004, GPU line).

### 2.4 Present and display
- **DONE** DXGI flip-model presenter in the GL ICD; ICD built from the package
  source (EXP2205).
- **OPEN (lever size unmeasured)** DWM composition of GravityMark (RedirectedFlip, EXP2212 ETW): 0.85 ms/frame
  (EXP2268); EXP2273 hot ring: the DWM job arrives 0.06 ms after the application's render completes, costs <= 0.54 ms
  of GPU plus 0.41 ms of depth-one boundary overhead, refusal 55 on every DWM job. Design, F0 result and corrections:
  investigation/analysis/DWM-COMPOSITION-LEVER.md (section 7).
  - F0 (EXP2301): DWM composes a screen-covering flip-model swap chain too; the 1280x720 mode set failed, so the
    fullscreen arm was a 2560x1600 present and the FPS bound is not measured.
  - **BUILD (C), regression** non-native display modes: the bench m1n1 line (83a9cf74) lacks CAP_SCALED_SOURCE
    (branch codex/m1n1-scaled: 66bf65c1, 8209e527, 74a4d8f4 never ported); ScaledModes 0, 1 of 11 modes offered,
    every mode change fails. Port first, m1n1 only.
  - **BUILD (FULL GRAPHICS), contract first** truthful direct / independent flip of an application buffer: the D3D11
    UMD answers CheckDirectFlipSupport FALSE by design, flip-model back buffers are not scanout-capable (EXP1038),
    the KMD flips only the fixed primary. Needs the Microsoft contract written down before code.
  - **PARKED (I/O)** C1 cross-process compute-behind-render pair, C2a Barrier waiver: not justified by F0.
  - **BUILD (later)** windowed independent flip on a second hardware plane (MPO: m1n1 IOMFB surface 1 + KMD MPO
    DDIs); after the single-plane direct flip.

### 2.5 Shader compiler
- **DONE** compute, VS and FS code at Metal-oracle parity (instruction and load
  counts, EXP2070/2194, OV2 session 2026-11-06).
- **CLOSED 2026-11-08** compute kernel runtime: at scene-aligned parity with Apple (agx_gm_scene_align.py; C4 lighting +13 % is the DXIL-heavier kernel, workload-owned). The earlier +4.5 ms was a scene-sampling artefact. Remaining GPU-timeline gap = 4.0 ms structure: ~2.0 missing R||C overlap, 0.85 DWM composition (platform), 0.3-0.5 predictive completion poll (KMD), 0.45 launch/broker, 0.22 idle (state 2026-11-08 STRUCTURAL GAP DECOMPOSED).
- (superseded text) runtime behaviour of compute kernels (+4.5 ms/frame HD vs Apple):
  needs counters -- Apple's via Instruments (Xcode on the J313 macOS) and ours via
  the firmware stats ring first; RDE perf-counter RE only if those do not
  explain it.

### 2.6 Quality gates
- **DONE** D3D12 suite (119), performance gate (GravityMark HD PERF + CS 1.6),
  receipt cap test, declaration-order gate, per-package ICD build.
- **BUILD** gate attribution: per-position job spans (--jobs) alongside fps.

## 3. Decisions log (2026-11-06)
- OV3b, OV2 ranges, OV5 drill, backend successor rule: classified C/I, kept
  (off by default where they depend on a consumer not yet proven).
- Shared-store vectorize: class O, no gain (EXP2211) -> stays off.
- OV5 TA waiver: class O on top of I; hangs the GPU -> off until 2.2 OPEN is
  resolved; the mechanism (3D Barrier, parser, successor rule) stays.
- Firmware RX drain: class C, perf gate PASS (EXP2219) -> default on.
- Perf gate: first CS and first GravityMark run of a boot are cold; the gate
  uses a warm-up plus two same-boot repeats (EXP2218/EXP2219).

## 4. Decisions log (2026-11-07)
- Depth-2 kick fallback S4a: class C (a firmware-invisible refusal must not
  reset the engine), default on; EXP2224 drill proof; EXP2225 perf gate PASS.

## 5. Decisions log (2026-11-09)
- Interrupt-driven completion: class C, FPS neutral, frees the worker core -> to become the default once the level
  line is dismissed in every ISR state (source finding, KMD-IRQ-COMPLETION.md "Default-on plan").
- DWM composition row (0.85 ms): no build before F0; cross-process serialization (refusal 55) is a legacy of the
  single VM slot, to be narrowed as class I (C1) if F0 shows the row is real.
- TDR recovery: design accepted (firmware restart split across ResetFromTimeout/RestartFromTimeout); first
  hardware stage is H1 (idle warm restart), nothing is wired into the TDR DDIs before H1 answers U1.
- Mu for IRQ default-on: MUIRQ001-95263b97 is the candidate (MUANSCPPC002 + one commit + the flag; only the
  J313ABI SSDT differs, by interrupt 887). No new image.
- F0 (EXP2301): the fullscreen arm was a failed mode set (bench m1n1 lost CAP_SCALED_SOURCE with the move to the
  brokerlock/ansdirect line) -> lever size unmeasured; DWM composes because the UMD refuses direct flip by design.
  Order: scaled-source port (C, m1n1 only), direct-flip contract and spec (FULL GRAPHICS), then F0 again. C1/C2a parked.
- Merged into gpu/memory-latency (user decision "влить обе", 2026-11-09): ISR default-on branch (212f09e0e) and TDR
  stage 2a shared layer (fe119f211). Review correction on top: RuntimeStart arms before it publishes (84b91a7cb).
  Gate on the merged tree: 1091 tests, 0 NEW once the three test_g4_live_* suites read the q22 m1n1
  (Q22_M1N1_SOURCE=.worktrees/m1n1-q22; the m1n1_windows checkout 0d3a75b3 lacks live_additive). MSVC /W4 compile
  = the next package build.
- Bench m1n1 identity: ledger BEFORE "m1n1 0d3a75b3" (EXP2300/2301) is the gitlink checkout; the launched image is
  ansdirect-bugcheck4 83a9cf74 (correction appended to EXPERIMENTS.md).
