# DWM composition lever: 0.85 ms/frame in windowed GravityMark HD

Architect note, 2026-11-09T19:30:27Z. Status: design + first discriminator; nothing implemented.
Scope: the "0.85 DWM composition" row of the HD gap budget (state 2026-11-08 STRUCTURAL GAP DECOMPOSED).

## 1. Evidence (all offline, existing captures)

E1. EXP2212 DxgKrnl ETW, 12 s steady state, windowed HD (`.local/experiments/EXP2212-dxg-etw/evidence/EXP2212-etw/dxg.csv`):
GravityMark presents 495 times as RedirectedFlip 1280x720; DWM presents 493 times as Flip; 493
MMIOFlipMultiPlaneOverlay events, 2560x1600, FlipWaitVSync, 1 plane; no IndependentFlip event (266). DWM renders and
flips the whole primary once per GravityMark present.

E2. EXP2273 `Wom1JobTiming971Hot` (last 62 jobs of the GM run, 7 frames, decoded with the KMD_SLOT_V4 layout of
tools/agx_gpu_frame_breakdown.py). The same pattern in 7 of 7 frames, times relative to the completion of
GravityMark's last render of the frame (medians; the stamp row is one frame):

| Event | Time |
|---|---|
| DWM render job arrives at the KMD (submit stamp) | +0.06 ms |
| GravityMark's next compute job arrives | +0.21 ms |
| DWM job kicked (depth one) | +0.32 ms |
| DWM job seen complete (kick -> complete 0.54 ms, includes detect) | +0.85 ms |
| GravityMark compute kicked (0.09 ms after the DWM completion) | +0.96 ms |

Depth-2 kick refusal recorded on every DWM job: **55** (cross-process). Without the DWM job the compute that arrived
at +0.21 ms needs ~0.05 ms to launch, so this frame type loses ~0.70 ms; EXP2268's independent estimate over all
frame types is 0.85 ms (launch 0.90 vs 0.05 ms with/without a foreign job ahead).

E3. The DWM job is never available early: it reaches the KMD 0.06 ms **after** the render it depends on completes
(dxgkrnl holds it on the present's fence). It cannot be pre-bound behind that render. The job behind it can.

E4. Split of the 0.95 ms: DWM's own GPU occupancy <= 0.54 ms (detect latency inside); boundary overhead 0.41 ms
(0.26 ms build+launch of the DWM job at depth one, 0.06 ms dxgkrnl, 0.09 ms launch of the next job).

E5. KMD source: `AdmissionJobWindowKickRefusal` refuses every pair with `m->Process != n->Process` (55) before any
class logic. The header comment "one VM slot lease: one process at a time" predates retained per-process VM slots
(V1, default on since EXP2176-2181). `runtime->RunningVmSlot` is still a single value that a successor inherits
(refusal 61 term, backend_platform_windows.c 4058-4095). All processes share one render and one compute firmware
queue; a job's VM is its context id.

E6. Display path: `AdmissionDdiSetVidPnSourceAddress` -> `AdmissionScanoutQueuePresent` is a real page flip of a
linear, scanout-capable allocation with the committed source geometry. Exclusive fullscreen sets the application's
mode and flips its primary (EXP2056: 1920x1080 scanned out centred). m1n1's IOMFB swap has several surface slots and
fills only surface 0.

## 2. Mechanisms

| | Mechanism | Class of requirement | Removes | Cost | Status |
|---|---|---|---|---|---|
| B | Exclusive fullscreen: application primary flipped directly | FULL GRAPHICS (display + render) | whole DWM job, if dxgkrnl grants the direct flip | none (exists) | **unmeasured** (EXP2056 was order-confounded, FHD only) |
| C | Cross-process pairs in the depth-2 window | RENDER_ONLY (submission) | boundary overhead (C1), then the wait for DWM's GPU time (C2) | KMD window + G3 successor path; C2 adds one UMD flag | design below |
| A | Windowed independent flip on a second hardware plane (MPO) | FULL GRAPHICS (display) | whole DWM job in windowed mode | m1n1 present ABI with two surfaces and rectangles, KMD MPO DDIs and honest CheckMPO, linear back buffers | BUILD, after B and C are measured |

Not a mechanism: making DWM's job cheaper. It is DWM's workload.

## 3. Mechanism C in the four sections

WINDOWS CONTRACT (RENDER_ONLY): dxgkrnl submits commands to one engine in order and expects completion notifications
in that order. It has already ordered cross-process dependencies before DxgkDdiSubmitCommandVirtual (E3: DWM's job is
held until the application's fence signals). Executing two submitted jobs concurrently is permitted when the result is
indistinguishable from serial execution. Two jobs of different processes can only conflict through an allocation
visible to both processes.

AGX/ASAHI CONTRACT: Asahi gives every file its own VM and queues and submits them without global serialization; the
firmware schedules work of different VMs concurrently, and a compute and a 3D work item run on different firmware
queues. Barriers wait on stamps, which are global memory, not per-VM state. Source-derived; never exercised on our
stack, where every cross-process boundary has been serialized.

TRANSLATION:
- C1 (class I, opt-in `Wom1CrossProcessPairs`): allow exactly one cross-process class, compute successor behind a
  running render (COMPUTE_ON_3D), with the existing lead Barrier kept. The wire constructs are the ones EXP2088
  validated; the only new fact on the wire is two context ids in flight. Needs: a per-job VM slot in
  `ADMISSION_JOB_WINDOW_JOB` (both nonzero and different, i.e. both processes hold retained slots), the 55 term
  narrowed to that case under a new capability bit, `AdmissionGpuvaG3BeginSuccessorJob` and the backend building the
  successor in the successor's process and slot instead of `RunningVmSlot`, and a receipt (pairs considered, kicked,
  refusals) on the throttled diagnostics-off export. Every other cross-process class keeps 55. Expected gain: the
  0.09 ms launch plus the completion-detect share, ~0.1-0.2 ms on the frames with this pattern. C1 is infrastructure:
  it is kept if correct, whatever the FPS.
- C2a (class O, on top of C1): waive the lead Barrier (the OV2 wait-value construct) when the compute job carries a
  new UMD verdict "private-only": every allocation it binds was created by this device on the non-shared,
  non-primary path. Such a job cannot conflict with any job of another process, so no knowledge of DWM's job is
  needed. Envelope flag admitted by the parser only under a device feature bit, as OV2's VdmBarrier NONE. Expected:
  the compute starts ~0.1 ms after the DWM kick instead of after its completion, up to ~0.5 ms/frame on these frames
  before contention.
- C2b (later): a foreign render behind a running private-only compute (the EXP2268 "foreign job ahead of R3" frames,
  35 %). Useful only with a successor that has no lead Barrier on either ring (the OV8 step 2 construct, not built).

WHAT IS STILL UNKNOWN:
1. Hardware: whether the firmware, as we initialise it, runs a compute work item of one VM slot while a 3D work item
   of another slot is in flight. This is the C1 discriminator.
2. dxgkrnl: whether exclusive fullscreen removes the DWM job (mechanism B), and therefore how large the lever is at
   all. This is F0 below, and it needs no code.
3. Offline, before C2a: how the pinned D3D12 DDI marks a shared-created heap (D3D12DDI_HEAP_FLAGS has PRIMARY 0x10 and
   no shared bit; the resource flags have CROSS_ADAPTER). Resolve from d3d12umddi.h and the UMD's existing
   shared-create/open path (agx_d3d12_shared_open.h); an unknown origin clears the flag.
4. The 35 % frame type is not in the 7-frame sample; C2b numbers come from a mid-run ring after C1.

## 4. First discriminator: F0 (no code change)

One variable: GravityMark HD windowed vs exclusive fullscreen, default package, production Mu, no switches.

WHY THIS HYPOTHESIS:
- E1: in windowed mode DWM composes and flips once per present; E2: that job sits between two GravityMark jobs and
  costs 0.7-0.95 ms of GPU timeline per frame.
- E6: the fullscreen path already flips the application's primary, so dxgkrnl may drop the composition there.
- EXP2056 could not tell (FHD, arms confounded with run order).

Procedure: one boot. A/B/B/A in PERF mode (10 + 60 s), A = windowed 1280x720, B = fullscreen 1280x720, after one
warm-up run. Then two attribution runs (one per mode) with a 12 s DxgKrnl ETW capture as in EXP2212: Present_Info
mode per process, MMIOFlip source size and flags, IndependentFlip (266), and DWM's QueuePacket count per GravityMark
present. ETW runs are not FPS evidence. Delete the ETL/CSV on the guest after fetch.

Readings:
- DWM packets per GM present ~0 in fullscreen and FPS up by 2.5-4 % (0.85 of 21.6 ms is 3.9 %): the cost is real and
  fully removable. B is the answer for fullscreen applications as it stands. Windowed: build C1, then C2a; A stays
  BUILD with a measured ceiling.
- DWM still composes in fullscreen: the direct flip is not granted. Next target is the display side (why: caps,
  format, geometry, CheckDirectFlip path), not C.
- DWM gone and FPS equal within the ABBA spread: the DWM job is not on GravityMark's critical path at HD; the row
  leaves the gap budget and neither A nor C2 is built for speed. C1 stays optional infrastructure.

Failure/recovery: a mode-set failure or a hang entering fullscreen -> guest watchdog, visible recovery as usual; the
package is the default one, so nothing is experiment-local except the ETW files.

## 5. C1 implementation notes for the implementer (only after F0 reads "real")

- Pure window logic first (render_job_window.c/h): new capability bit, `VmSlot` in the job descriptor, 55 narrowed;
  take the next free refusal number for "cross-process pair not eligible" (61 is taken). WHAT REAL BUG OR INVARIANT
  WILL THIS TEST CATCH: a cross-process pair of any class other than compute-behind-render, or with a zero or equal
  slot, is never kicked; with the capability off the function is byte-for-byte today's (55). Deterministic, offline.
- Audit before wiring, and list the findings in the commit message: every place that names "the" running process or
  slot for both jobs of the window: `RunningVmSlot`, `image->JobVmSlot`, `AdmissionPrepareUnmanagedInitBm` /
  `AdmissionUnmanagedInitBmStale`, the G3 job lease taken by BeginJob/BeginSuccessorJob, the broker call that
  activates a slot (must not deactivate the running job's slot), the sidecar set of the compute, the completion path
  that returns each job to its own process, and the kick fallback (S1-S4a) for a cross-process successor.
- Hardware C1: opt-in switch on, GravityMark HD windowed with diagnostics for one mid-run ring, expected kicked
  cross-process pairs > 0 with refusal 0 on the compute behind DWM, 0 resets in 3 starts, suite 119/0; then the perf
  gate with diagnostics off. Failure: firmware timeout or fault on the first cross-process pair -> unknown 1 answered
  "no" on our initialisation; record the firmware state and stop C.

## 6. Addendum 2026-11-09T19:34:30Z: unknown 3 narrowed offline (private-only rule for C2a)

Pinned header (.local/reference/wdk26100/d3d12umddi.h): D3D12DDIARG_CREATEHEAP_0001.Flags (D3D12DDI_HEAP_FLAGS) has no
shared bit; the API's D3D12_HEAP_FLAG_SHARED value 0x1 is not part of the DDI enum. What the UMD can observe:
- creation: its own allocate call, D3D12DDICB_ALLOCATE_0022 { hResource in, hKMResource out } and per allocation
  D3D12DDI_ALLOCATION_INFO_FLAGS_0022_PRIMARY (bridge: agx_d3d12_ddi.cpp BridgeAllocateCb, 688-717);
- open: every allocation that entered through pfnOpenHeapAndResource (D3D12DDIARG_OPENHEAP_0003, agx_d3d12_ddi.cpp
  2741-2805) belongs to another creator;
- resource flags CROSS_ADAPTER 0x4 (already tested at 2555) and SIMULTANEOUS_ACCESS 0x8.

Candidate rule, fail-closed: an allocation is private when this device created it through the allocate callback, the
callback returned hKMResource == 0, the PRIMARY flag was not set, the resource flags carry neither CROSS_ADAPTER nor
SIMULTANEOUS_ACCESS, and it was never opened. A job is private-only when every allocation it binds or references in
its allocation list is private. Still to confirm from Microsoft documentation before C2a is specified: that the
runtime returns a nonzero hKMResource for every shareable heap (labelled hypothesis until then). GravityMark's
compute jobs bind only device-created buffers and textures, so the rule is expected to hold for them; verify with a
receipt (private-only verdict per compute job) before any waive.

## 7. F0 result and corrections (2026-11-09T20:28:57Z, EXP2301 + source reading)

Result. Same boot, default package, HD PERF 10+60: windowed 43.77 fps, "fullscreen" 42.89 fps (-2.0 %). ETW in both
arms: GravityMark Present_Info = RedirectedFlip, one dwm Flip and 2.0 QueuePackets per GravityMark present,
MMIOFlipMPO source 2560x1600. DWM composes in both arms (reading ii of section 4).

Correction 1: arm B was not the specified one. ChangeDisplaySettingsExW failed ("can't set 1280x720 mode");
GravityMark fell back to a 2560x1600 screen-covering window with a 2560x1600 swap chain and a 1280x720 internal
render. The failure criterion of section 4 ("a mode-set failure") was met, so the -2.0 % is a 4.4x larger present
through DWM, not the cost or gain of removing DWM. The size of the lever is still unmeasured.

Correction 2: evidence E6 and the premise of mechanism B are unproven. EXP2056's CommitCalls 3 at 1920x1080 shows
a mode change, not an application flip: the D3D11 UMD that DWM loads answers pfnCheckDirectFlipSupport with FALSE
(mesa/d3d11/agx_wddm2_ddi.cpp:677), so no application buffer has ever been flipped to the panel by this driver.

Display-side causes, in dependency order:

| # | Cause | Layer | Evidence | Class |
|---|---|---|---|---|
| D1 | No non-native display modes: bench m1n1 83a9cf74 lacks CAP_SCALED_SOURCE (bit 8), ScaledModes 0, 1 of 11 modes offered | m1n1 (branch codex/m1n1-scaled never ported to the brokerlock/ansdirect line) | Wom1DisplayModesReceipt, scanout_windows.c:916, hv_agx_scanout_broker.c:141, EXP2056 | C (regression) |
| D2 | Direct flip refused by the UMD by design | Mesa D3D11 WDDM 2.0 table | agx_wddm2_ddi.cpp:677 | FULL GRAPHICS, BUILD |
| D3 | Flip-model back buffers are not scanout-capable (RGBA8, tiled/compressed pool, Displayable 0) | UMD allocation + KMD segment | EXP1038, direct_flip_contract.c | FULL GRAPHICS, BUILD |
| D4 | KMD flips only the fixed primary geometry on plane 0; no CheckMultiPlaneOverlaySupport | KMD display | display.c:1294, lifecycle.c ~806-814 | FULL GRAPHICS, BUILD |

Decision. D1 is a contract regression and is fixed first, on its own (m1n1 only, one hardware checkpoint). D2-D4 are
one design (truthful direct flip / independent flip of an application buffer) and need the Microsoft contract
written down before any code: which present paths DWM hands to dxgkrnl, what DISPLAYABLE requires of the
allocation, and what the KMD must accept in SetVidPnSourceAddress(MPO3). C1/C2a stay parked: F0 did not show that
serialization behind DWM is the row's cost, and a granted direct flip removes the DWM job instead of overlapping it.
F0 is repeated with a real 1280x720 mode after D1.
