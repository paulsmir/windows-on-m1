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

### 2.2 Dependencies (D3D12 -> AGX)
Target: order work only by what D3D12 orders (ResourceBarrier) and by
driver-internal hazards -- the same per-resource model Apple's kext uses
(per-resource read/write usage, stamps, MTLFence/Event; EXP2201, kext RE).
- **KEEP-OFF (C/I)** OV2 independence flag + ranges rule (resource-level, the
  D3D12 contract); OV3b deferred query-resolve copies only into an existing
  prelude (no new preludes); OV5 UMD TA-independence verdict; OV5 3D-queue
  compute-stamp Barrier (firmware accepts it, EXP2210); backend successor rule
  for a waived TA lead (56772879).
- **OPEN** OV5 TA waiver hangs the GPU (EXP2214, 0x116). Next evidence: compare
  our firmware work items for a C->R pair byte-for-byte with Apple's (kext RE of
  the work-command encoding), then a bounded reproducer.

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
- **BUILD** independent flip / DirectFlip so DWM stops composing GravityMark
  (RedirectedFlip, ~0.9 ms GPU + preemption per frame, EXP2212 ETW).

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
