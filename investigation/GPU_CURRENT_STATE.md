# AGX continuation — READ FIRST — 2026-10-19 UTC

Worktree `/Volumes/pwdev/public_windows/.worktrees/integration-ad04-windows-compiler`,
branch `integration/ad04-windows-compiler`. Russian reports, autonomous continuous
loop; bounded polls and NO final during active experiments. Root alone owns
tracked edits/Git/ledgers, builder/guest/launcher. Preserve user dirty `mu`.
No attribution/push/PR/new chats/goals/automations. CS enabled. Read agx-d3d12-loop,
AGENTS.md/CLAUDE_WORKING_METHOD; only referenced ledger/evidence, no archaeology.
Existing c18 security-blocked UBFE/C21E task MUST NOT be retried/routed around.

## UPDATE 2026-11-08 22:29Z -- WAIVER RESET IS REPRODUCIBLE AND TIMING-DEPENDENT (EXP2283/2285 vs 2282/2284)
- With the R8 TA waiver: Wom1JobDiagnostics off -> Windows reset 2 of 2 boots (EXP2283 second GM run, EXP2285 ~15 s into
  the first); diagnostics on -> 0 of 2 boots (3 GM runs). Offline: JobDiagnostics gates only receipts/sampling (per-poll
  firmware stamp reads, timing notes, exports), no scheduling or encoding branch -> the mechanism is a race that the
  slower sampling loop hides. No stop code: ANS-DIRECT refuses the crash-dump instance, the HV prints only PSCI RESET
  regs/stack.
- Two readings to separate first: GPU hang (0x116 after a failed ResetFromTimeout: a firmware/TA contract defect in the
  waived path at tight timing) vs a KMD fault (0x7E/0xD1/0x3B: a software race, e.g. completion ordering when the TA
  ends before the compute and the worker is not slowed). Spec (handoff): a stop-code beacon -- KeRegisterBugCheckReasonCallback
  in the KMD writes code + 4 params into a broker-page slot that m1n1 dumps at PSCI SYSTEM_RESET; or the runner's
  KiBugCheckData read at reset. Then EXP2286 = EXP2285 config + beacon.
- The waiver stays opt-in; the ABBA for the R8 gain is blocked until the race is understood.

## UPDATE 2026-11-08 21:43Z -- EXP2283: WINDOWS RESET IN THE SECOND GM RUN WITH THE WAIVER (ABBA PAUSED)
- Arm on, no Wom1JobDiagnostics: GM a 42.8 fps, then ~10 s into GM b a PSCI SYSTEM_RESET (bugcheck path as EXP2270); no
  stop code or dump: on ANS-DIRECT the crash-dump storage instance is refused by design (1e7a690c4), so hangs leave only
  the receipt snapshots, full.log and the crashed boot's registry (Waived 1617 / Placed 1617, no Wom1GpuReset).
- Two differences vs the clean EXP2282 (no diagnostics; a second GM run in the boot). EXP2284 (fork 6, running) keeps the
  waiver and the two runs but turns Wom1JobDiagnostics on: a reset names the second-run state as the ingredient
  (TVB growth / retained slots / leftover depth-2 window) through the last snapshot; no reset names the diagnostics-off
  timing (a race). The ABBA for the R8 gain waits on this.

## UPDATE 2026-11-08 21:14Z -- GRAVITYMARK HD WITH THE R8 TA WAIVER DOES NOT HANG (EXP2282, fork 6)
- Switches Wom1Ov2NoHazardLead=1 Wom1Ov5TaWaiver=1 Wom1JobDiagnostics=1, OV3B=1, HD PERF: Waived 2043 / Placed 2043 of
  3109 frames (R8 only; RUNNING_KIND 1), no reset, 9 receipt snapshots, 40.5 fps (receipt-class runs without the waiver:
  39.5-39.8; no same-package control). Overlap on GM: 601 of 11261 C->R pairs ended their TA before the compute end,
  median 0.54 ms, max 2.58 ms -- the waived R8s really overlap C7.
- EXP2214's hang does not reproduce in HD on this base (OV6 ranges, backend successor fix 56772879, kick fallback S1-S4a).
  FHD and the old base were not run: EXP2214 stays "not reproduced", not "explained".
- Decision: (1) ABBA HD on this package, waiver on/off (boot-time switches: cross-boot A/B/B/A with identical arm order,
  perf-gate protocol warm-up + 2 repeats, receipts OFF so the fps is production-class) -> the R8 gain in ms/frame decides
  the default (class C/O: correct and measured); (2) then FHD with the waiver, bounded like EXP2282 (the EXP2214 shape and
  the user's 7814 target); (3) OV6c after.

## UPDATE 2026-11-08 20:51Z -- OVERLAP PROVEN ON HARDWARE (EXP2281 OVPAIR-H): LONG TA RUNS DURING COMPUTE, 3D WAITS ON THE STAMP
- fork 6: 40/40 pairs, 37 waived, measure-gpu-stages: compute end - TA end median +29.6 ms (positive in 37/40 = the waived
  ones), render end 276 us after compute end. The G13 firmware runs a render's TA (~100k triangles, 1080p, D32) while an
  unfinished compute job runs on the other queue and releases the 3D queue on the compute stamp Barrier: the OV5 contract
  is sound including long TA. Not exercised: TVB growth/partial renders, prelude neighbours in the depth-2 window, DWM.
- Decision: go to GravityMark directly in EXP2214's shape but bounded (EXP2282 spec in the handoff): HD PERF only, the R8
  waiver (verdict 0 without OV3B since OV6), mid-run KMD receipt snapshots every 10 s, full.log saved before AT_PROXY.
  No hang -> EXP2214 was tied to FHD/OV3B/the old base; measure R8's gain (<= 0.65 ms HD) vs a same-switch control.
  Hang -> the last snapshot names the pair and the window state; then add the ingredient to OVPAIR-H.
- OV6c (VB/IB sized by view, VS UAV write only with stores) stays queued behind EXP2282.

## UPDATE 2026-11-08 20:22Z -- OV6b READ: R2/R4/R6 "VERTEX CONFLICTS" ARE RESOURCE-GRANULARITY, NOT PROVEN DEPENDENCIES (EXP2280)
- First conflicting pairs (fork 6 run, 3243 executions): R2/R4 = vertex-buffer read vs the compute's root UAV write in the
  same heap, overlap ~44.8 MB (62/63, 61/64 entries): the VB range is unsized (to the placed resource's end) and a root UAV
  covers its whole resource; R6 = a vertex-stage root UAV *binding* (counted as a write) vs the compute's root UAV write,
  overlap ~221 MB. No IND_ARGS/IND_CNT/TARGET/CLEAR pair comes first. Verdicts unchanged: 0x9999 (prelude), no-prelude 0x0aaa.
- Reading: the 10s are what the tracker can see, not what the GPU does -- Tellusim suballocates from big placed buffers, so
  resource-level ranges alias; Metal tracks per MTLBuffer and still overlaps c15||R16, so on Apple's side these are separate
  buffers or byte-disjoint. Provable byte ranges exist for VB/IB (view SizeInBytes) but not for a root UAV's writes.
- Decision (architect): park the R2/R4/R6 independence question -- their CDM prelude (9) blocks the TA regardless, so the
  prelude placement is the real gate for ~1 ms of the overlap ceiling. OV6c (VB/IB sized by their views, VS UAV counted as a
  write only when the shader stores) is cheap and correct (class C) but not on the critical path; queue it after OVPAIR-H.
- Critical path now: OVPAIR-H (long TA + TVB while compute runs, with overlap proof) -> if it holds, a bounded GM run with
  the R8 waiver (EXP2214 shape, recovery staged); if it hangs, the ingredient is found.

## UPDATE 2026-11-08 19:53Z -- OVERLAP LINE: THE BASE OV5 CONTRACT DOES NOT HANG (EXP2279); EXP2214 IS GRAVITYMARK-SHAPE SPECIFIC
- Architect/implementer split since this evening: this session analyses and specifies, fork 6 implements and runs.
- EXP2275 (OV6 finite ranges): every GM render-after-compute is placeable; R2/R4/R6 = prelude (9) and a resource-level
  vertex overlap with the preceding compute (10); R8 clean. EXP2276-2278: the OVPAIR reproducer needed one
  ExecuteCommandLists and a compute-first submit hook (Mesa flushes open batches in slot order, not creation order).
- EXP2279 (OV7 hook + OVPAIR, fork 6): 38/40 renders waived (TA root without the compute-stamp wait, 3D queue led by the
  Barrier on the unfinished compute stamp), 40/40 pairs complete, pixels right, no GPU reset. VERDICT: the base OV5 firmware
  contract is sound on a synthetic pair; the EXP2214 hang needs a GravityMark-specific ingredient: a long TA with TVB
  growth/partial renders while the compute runs, prelude-bearing neighbours in the depth-2 window, DWM jobs between, or
  the 3D queue blocked for milliseconds. Not measured yet: whether the TA actually overlapped the compute (timestamps).
- Next (specs for fork 6): OV6b conflict provenance (queued); OVPAIR-H = the same pair with a GravityMark-like render
  (1920x1080 RT, ~100k triangles, TVB in use, TA of several ms) and a 20-30 ms compute, 40 pairs, plus TA/compute
  timestamps from the KMD receipts to prove the overlap; then add the GM ingredients one at a time (DWM, depth-2 prelude
  neighbour) until EXP2214 reproduces or GM's R8 waiver is tried again with a bounded recovery.

## UPDATE 2026-11-08 16:39Z -- IRQ-COMPLETION STEP 2 ON HARDWARE (EXP2273): CORE FREED, FPS NEUTRAL, DETECT UNCHANGED
- Wom1IrqCompletion=1 (+ Mu MUIRQ001): poll busy 47.6 % -> 2.5 % of job time (waits instead of spins), 57 % of waits
  ended by the doorbell wake, WorkerSyncPopHits 0.8 %. GM HD PERF 43.82 vs 43.99 (step 1) vs 44.4-44.6 (no-887 controls).
- Per-job detect (firmware end -> KMD saw it) unchanged: 0.98 vs 1.02 ms/frame summed; the interrupt arrives when the
  predictive poll would have looked anyway. The "detect" item of the structural budget is firmware/HV notification
  latency, not the poll's wake timing -> not recoverable from the worker side. Only C0's launch improved (0.118 -> 0.057).
- Both 887 runs are 1.3-1.7 % below the no-887 controls. CORRECTION (fork 6 HV EXITS): the line adds ~4k ICC traps/s
  and ~0.5k IRQ exits/s (~390 mailbox interrupts/s, not 40k), too few for -1.5 %; the difference is near the run spread
  (EXP2266 44.08). HV tax = unsupported hypothesis. Decision: keep Wom1IrqCompletion opt-in (class C: correct event-driven completion, a
  core saved for the UMD/app) and do not flip defaults until a same-boot ABBA gate (IRQ on/off) is run.
- Structural gap re-ranked: R||C overlap ~2.0 (firmware contract, OV line) > DWM 0.85 (platform) > launch/broker 0.45
  (fork 6: GPUVA in KMD) > idle 0.22; detect ~1.0 summed = firmware/HV path (HV inventory items 2/3: ICC trap count per
  interrupt, route latency) -- measure the trap cost of an interrupt before touching it.

## UPDATE 2026-11-08 15:27Z -- IRQ-COMPLETION STEP 1 CONFIRMED ON HARDWARE (EXP2272)
- Mu MUIRQ001 (95263b97: J313_AGX_MAILBOX_IRQ -> Interrupt 887 Level beside the 889 edge) + KMD 9b64ec9a (validators accept
  edge==1 && level<=1; ISR census; armed mailbox line -> ISR drains the outbox FIFO into a ring, worker pops hardware only
  under DxgkCbSynchronizeExecution): boot normal, suite 119/0, GM HD PERF 43.99 (control 44.4-44.6, -1 %).
- Answered: (1) dxgkrnl connects both line-based Interrupt resources and calls the ISR with MessageNumber 0 for both
  (IsrByMessage1 = 0, mask {0}): the ISR must identify the source itself. (2) The firmware raises an RX doorbell for
  every job completion: 38905 messages for 33796 jobs in the suite+GM window. (3) The ring is enough (high water 3, 0 drops).
- Observed: the spinning worker still won 14.5 % of the messages (WorkerSyncPopHits) -> the interrupt path (m1n1 route +
  vGIC + ISR) is slower than a spinning poll for those; irrelevant once the worker sleeps on the wake instead of spinning.
- Next (step 2, one variable): Wom1IrqCompletion=1 -> DPC from the mailbox ISR sets JobThreadWake; the worker waits on the
  event with the predicted timeout instead of the predictive sleep+spin. Expected: Wom1JobPoll late_mean -> ~0, detect per
  job 0.04-0.17 -> <0.05 ms, worker CPU busy << 47 %, GM +1-2 %. Bench returned to fork 6 after rec2272h.

## UPDATE 2026-11-08 14:08Z -- THE 4.0 ms STRUCTURAL GAP DECOMPOSED (offline, EXP2268 log + EXP2267/2193 receipts)
- CORRECTION of the two sections below and of the EXP2225 text: `late` and `foreign` in agx_gpu_frame_breakdown --jobs are
  per-position FRACTIONS of frames (mean of 0/1 flags), not ms. "late submit 1.20 / 1.40, foreign (DWM) 0.99" were summed
  fractions (= ~1.4 late jobs and ~1 foreign DWM job per frame). Real non-work per frame (wall - GPU work) is ~2.0 ms, not 4.7.
- HD frame, EXP2268 frames t>15 s (scratch struct-decomp over the tool's job model): wall 21.60, GPU work 19.41 (C exact + R
  ta/3d/prelude), overhead 1.91 median / 2.05 mean = launch 1.39 + firmware idle gaps 0.22 + critical-path detect ~0.3-0.5.
  Per position: launch C0 0.12, R1 0.06, C2 0.87, R3 0.06, C4 0.05, R5 0.06, C6 0.06, R7 0; detect C0 0.13, R1 0.04, C2 0.10,
  R3 0.07, C4 0.17, R5 0.06, C6 0.14, R7 0.10 (sum 1.02, partly hidden by depth-2 pre-kicks).
- Item 1, DWM composition inside GravityMark's GPU timeline: ~0.85 ms/frame. C2's launch is 0.90 ms when a foreign (DWM) job
  completed ahead of it (57 % of frames) and 0.05 ms otherwise; R3 0.98 vs 0.06 (35 %); C4 0.88 vs 0.05 (4 %): one DWM job
  per frame, ~0.85 ms including its own kick. Owner: Windows windowed presentation (EXP2212: no MPO -> no independent flip).
  Apple's mtltime cb time EXCLUDES WindowServer, so the like-for-like GravityMark-only gap is ~3.2 ms, not 4.0.
- Item 2, our own launch latency ~0.45 ms/frame (0.05-0.12 per job, 8 jobs): KMD worker_to_kick ~55 us + begin/build 5-50 us
  (JobTiming971Hot) + broker JOB_BEGIN/LEASE (160-272 us per process switch, EXP2163) + firmware kick->start. Owner: KMD + HV
  broker (migration item #11 of HV-RESPONSIBILITY-INVENTORY.md).
- Item 3, completion detection ~0.3-0.5 ms/frame on the critical path (1.02 summed): the KMD detects completion by a PREDICTIVE
  SLEEP + SPIN poll, not by a firmware interrupt. Wom1JobPoll over EXP2267's suite+GM window: 35454 jobs, 40141 sleeps,
  sleep requested 24.8 s but slept 37.7 s -> the timer wakes 320 us late per sleep on average (late_max 3.5 ms), slack 492 us,
  CPU busy 49 % of job time (1.6 M spins), 3.7 % of completions first seen after a sleep. Owner: KMD design + Windows timer
  resolution. Lever: interrupt-driven completion (firmware notify -> ISR/DPC -> worker), as Asahi/macOS do.
- Item 4, firmware-view idle between our own jobs 0.22 ms/frame (depth-2 works; residual boundary cost).
- Item 5, no render||compute overlap: ~2 ms/frame net. Apple's unsplit frame is 3.2-4.5 ms below its split encoder sum
  (minus ~0.8 ms split overhead -> ~2.5-3.5 ms of overlap), while our render work is ~1.2 ms below Apple's split render sum.
  Owner: firmware work-item contract (OV2/OV3/OV5 line; OV5 TA waiver hang EXP2214 is the open blocker).
- Ranking by recoverable size: overlap (~2.0) > DWM (0.85, platform-owned, also hidden on macOS) > detect (0.3-0.5) >
  launch (0.45) > idle (0.22). Sum ~4.0 = measured wall - Apple unsplit in every aligned bin. Compute kernels: parity.
- Next: (a) OV line: byte-for-byte work-item comparison with Apple's kext for one C->R pair (prerequisite for overlap);
  (b) KMD interrupt-driven completion design note (WINDOWS/AGX/TRANSLATION/UNKNOWN) replacing the predictive poll on the
  critical path; (c) the broker/LEASE cost belongs to fork 6's GPUVA-in-KMD migration. No hardware run needed yet.

## UPDATE 2026-11-08 13:55Z -- C2 vs APPLE c11: EQUAL WORK, EQUAL TIME; THE COMPUTE GAP WAS A SCENE-SAMPLING ARTEFACT
- Work: our C2 records exactly Apple c11's 34 dispatches in the same order with the same direct grids and thread sizes
  (fc968f7a 2x1x1 + 4 indirect = Apple d0-d4 @128 threads; 1x1x1 singles; 256-thread indirect block d7-d28; 1024-thread
  d31; 64-thread d33). Indirect grids are app data (identical scene) -> equal work; the "amount of work" hypothesis is excluded.
- Time: GravityMark's camera path advances per FRAME, so our 44-fps run reaches each scene segment later than Apple's
  60-fps run, and Apple's c11 varies 0.8-4.6 ms with the scene. Aligning the per-frame series by C2 cross-correlation
  (tools/agx_gm_scene_align.py; lag 13 s, corr 0.85) gives, per 10-s bin: C0 block 0.94-1.01x Apple, C2 0.90-0.97x (ours
  FASTER), C4 lighting 1.13x (+0.25-0.3 ms; the DXIL 32-vs-12 sin/cos kernel), C6 0.97x; render sum <= Apple's split
  encoders; our GPU-work sum == Apple's split sum (20.6-23.9 ms) in every bin.
- Frame: our wall = Apple's UNSPLIT GPU frame (mtltime hd.csv) + 4.0 ms in every bin (20.94/16.83, 21.38/17.31,
  23.87/20.20, 23.30/19.63, 18.93/14.95, 21.75/17.63, 23.29/19.28). Apple's unsplit frame is ~3 ms below its own split
  sum: encoder overlap. So the whole HD gap is structure: our 4.7 ms non-work (launch/late/DWM/detect) and zero R||C
  overlap vs Apple's ~0 overhead and ~3 ms overlap. Kernel runtime is at parity (except C4 +0.3 ms, workload-owned).
- SUPERSEDED: "compute 12.73 vs 9.2 (+3.5)" above and the 2.5 OPEN "+4.5 ms compute runtime" item: run medians of our
  100-s PERF window (heavy first half of the camera path) were compared with Apple's full-run medians. The EXP2226
  per-dispatch ratios (2x) are the same artefact. Rule: compare GM jobs only after scene alignment.
- Next causal target (only one left on the GPU timeline): the 4.0 ms structural gap = non-work overhead
  (HV GPUVA broker/LEASE, late submit behind DWM, DWM composition, completion detect) and missing R||C overlap
  (OV2/OV3/OV5 line). Compiler/kernel work on GravityMark is closed unless C4 lighting (+0.3 ms) is wanted.
- Evidence: .local/experiments/EXP2268-nopmu/evidence/EXP2268/scene-align/ (ours-frames-2268.csv, apple-frames.csv,
  bins), Apple sources EXP2226 run2 splitd-hd.csv and MACOS-20261105-metal-timing/hd.csv.

## UPDATE 2026-11-08 13:45Z -- HD FRAME BUDGET RE-MEASURED OFFLINE ON THE SLC+CPPC PROFILE (EXP2266/2268 logs)
- Source: tools/agx_gpu_frame_breakdown.py --jobs on gm2266d/gm2268d-umd.log (profile SLC+DRAMCFG+CPPC, native SSD,
  GM HD PERF 44.4-44.6) vs gm2225d (pre-SLC, 40.8). Wom1GpuPerf post-membw->after-d (EXP2266/2267): P6 86-87 %, P1 12-13 %
  (the P1 share is suite/probe idle; GM itself ~98 % P6 per EXP2217), so the budget is at the macOS clock. No hardware run.
- Median frame (ms), EXP2225 -> EXP2268: wall 24.77 -> 22.75; compute exact 13.70 -> 12.73; 3D 4.15 -> 3.72; TA 3.95 -> 3.31;
  launch 1.40 -> 1.28; late 1.20 -> 1.40; foreign (DWM) 0.99 -> 0.99; detect 1.09 -> 0.81; idle 0.19 -> 0.18.
  Positions CRCRCRCR (span; C exact / R ta+3d): C0 7.87 -> 6.93 (x 7.91 -> 6.90), R1 2.72 -> 2.38 (ta 1.98 -> 1.58),
  C2 4.12 -> 4.09 (x 3.13 -> 3.22, launch 1.01 -> 0.88), R3 3.22 -> 2.70 (ta 1.74 -> 1.51, 3d 1.34 -> 1.07), C4 1.91 -> 1.84,
  R5 1.88 -> 1.90, C6 0.49 -> 0.71, R7 0.59 -> 0.21. SLC took ~1.0 ms from the big compute block (C0) and ~1.0 ms from TA/3D.
- Against Apple HD (EXP2192 encoders, span 17.4): compute 12.73 vs 9.2 (+3.5, was +4.5): big block C0 6.90 vs c0-c9 5.8 (+1.1),
  C2 3.22 vs c11+c13 1.57 (+1.65, largest single item: traversal fc968f7a launch), C4 1.90 vs c15 1.19 (+0.7), C6 0.72 vs
  c18+c19 0.63. Render 3D+TA 7.03 vs Apple render sum 8.3: ours is now FASTER than Apple (Apple's render encoders are stretched
  by overlap). Non-work (launch+late+foreign+detect+idle) 4.66 ms = 20 % of the frame, unchanged by SLC; Apple has ~0 plus
  2.3 ms of R||C overlap. The remaining HD gap (22.75 vs 17.2 = 5.5 ms) = compute +3.5 and structure (overhead 4.7, no
  overlap) minus our render advantage (-1.3).
- Consequence: the 13.7 vs 9.2 compute figure is superseded by 12.73 vs 9.2; the DXIL-heavier workload (C0 32 vs 12 sin/cos
  etc.) sits inside the +1.1/+0.7 items and is not yet quantified in ms. Next causal targets in order: (1) C2 traversal
  launch vs Apple c11 d2 (grid/work comparison, offline); (2) the 4.7 ms non-work budget (HV GPUVA broker/LEASE, late submit
  behind DWM, DWM composition) per HV-RESPONSIBILITY-INVENTORY.md; (3) quantify the DXIL-vs-Metal work ceiling.
- No EXP2272 hardware run: the existing logs answer the question; the bench stays with EXP2271 (fork 6).

## UPDATE 2026-11-08 08:30Z -- MEMBW LATENCY GAP = GPU DVFS ARTEFACT (EXP2265)
- During the MEMBW probe the GPU ran P1 (396 MHz) 64.7 % / P6 11.5 % (Wom1GpuPerf delta); GravityMark runs P6 85 %.
  The 2-3.5x "device-load latency" gaps vs the Metal twin (EXP2226-2264) are mostly clock: do not tune the memory path
  from them. EXP2260-2263 hypotheses (VMID, map TLBI, addressing form) are moot as explanations.
- EXP2266 (65536-step chase, P6 dominant): latency ~= macOS (same-line 64 vs 65-92 ns, 256K 130 vs 120, 64M 417 vs 452;
  residual 16M 351 vs 181). Memory hierarchy is NOT the GM gap. Reliable gap: HD compute 13.7 vs Apple 9.2 ms/frame.
- Next: verify pstate of our per-pass timing runs; compare indirect grids (amount of work) of traversal launches vs Apple.
- Open: our firmware DVFS ramp for bursty work vs macOS (initdata perf/ramp parameters) -- may matter for frame-paced
  workloads with idle gaps.

## UPDATE 2026-11-08 05:00Z -- NATIVE SSD EVERYWHERE; HV TRAPS CUT; GPU GAP NARROWED TO TRANSLATION
- Experiments AND recovery boot the native SSD (AppleAns ANS-DIRECT): run_uefi --storage-mode ANS_NATIVE --ans-direct
  --ans-production-fd-sha256 (ef1707777); prepare.sh refuses bridge launchers; recovery default RECANS001 (broker ON).
- Working GPU profile: m1n1 exp/m1n1-ans-direct-slc (SLC, DRAMCFG, CPPC at 0x300040000, guest-owned debug default,
  scanout from HV tick default), Mu MUANSCPPC002 (G2 ANS gpu-direct + SMBIOS + _CPC), WOM1_CPPC=1. GM HD 44.3-44.7.
- HV exits: MSR class 0 (EXP2256), scanout steps 16x fewer (EXP2257); PMCCNTR (0.8-2.3 M/10 s) already unlocked,
  removing it needs hiding the PMU (daytime experiment).
- GPU device-load gap (EXP2259-2261, macOS 118-181 ns, pages 65-92): ours 80 ns in one page, ~226 ns for 4 pages,
  436 ns for 1024 pages. Rejected: GPU clock (P6 1278 MHz = macOS), SLC per-DSID policy (EXP2258, GPU allocates),
  guest VMID 1 (EXP2260), map-without-TLBI (EXP2261), leaf MEMATTR (EXP2239).
- EXP2262 REINTERPRETATION: one self-pointing line already costs ~228 ns -> the "page-spread" gap is NOT translation;
  it is "same address in every lane" (probe and Metal twin both start all 32 threads on one line): ours ~228 ns vs
  macOS 65-92; distinct-lane 16 KiB chase is at parity. NEXT: compare our compiled MEMBWUAV load vs Apple's twin
  (codegen/coalescing) -> then codegen fix or GPU L1 config experiment; relevant to GM traversal kernels.

## UPDATE 2026-11-08 00:30Z -- WORKING PROFILE: SLC + DRAMCFG + SMBIOS + CPPC (EXP2243-2251)
- GPU experiment profile now: m1n1 brokerlock-cppc3 0467d900 (sha e871092f; SLC on, PMU filter fix, hv_cppc),
  Mu MUCPPC003 3870dfa1 (FD 9571633c; SMBIOS "Apple M1" + _CPC/_PSD/_OSC), launcher env WOM1_CPPC=1 (AGX_LAUNCH_ENV),
  DRAMCFG follows the P-cluster (launcher default + HV CPPC). Last-good m1n1 stays brokerlock-split 550c7f23.
- CPPC: Windows FxPPM runs real DVFS 600-2988 MHz; counters memory-backed (8-284 HV reads/10 s). GM HD 44.63 (EXP2251).
- HV cost map + migration plan: investigation/analysis/HV-RESPONSIBILITY-INVENTORY.md. CPU plan: CPU-DVFS-SMBIOS-PLAN.md.
- GPU device-load gap at 4M+ still open (GPU-side: UAT/TLB, GPU cache config, firmware perf state).

## UPDATE 2026-11-07 21:50Z -- SLC WAS OFF; ENABLED IN m1n1 (EXP2242/2243) -- NEW WORKING m1n1
- EXP2242: T8103 system-level cache (8 MiB) read 0/16 ways on all 4 MCC planes at the proxy: upstream m1n1 enables it
  only in kboot_boot(); our HV path never did. DRAMCFG on all 8 DCS channels = m1n1 "DEFAULT" (not FAST).
- EXP2243: m1n1 exp/m1n1-brokerlock-slc a0790a21 (default-on, byte-identical sha 83e5f207) -> GM HD 44.43 fps vs
  40.73-41.07 (+8.5 %), suite 119/0. GPU device-load latency only 15-25 % lower at 256K-1M; 4M+ unchanged -> SLC was part
  of the gap, not all of it. WORKING GPU m1n1 = brokerlock-slc; last-good stays brokerlock-split 550c7f23.
- Next causal targets: DRAMCFG FAST (macOS _mccSelectDynamicDRAMCFGMode; separate variable), GPU-side memory config.
  CPU plan (DVFS via CPPC + SMBIOS identity): investigation/analysis/CPU-DVFS-SMBIOS-PLAN.md.

## UPDATE 2026-11-07 20:15Z -- DEVICE-LOAD GAP IS INSIDE THE GPU MEMORY PATH (EXP2238-2241)
- CORRECTION: MEMBW (EXP2226-2237) read its buffer via SRV ld_raw -> nir txf (texture path); Metal twin used device
  loads. MEMBWUAV (34e9c89f3, UAV -> load_ssbo) is the like-for-like probe: ours 16K 132-134 / 256K 330-350 / 1M 365 /
  4M 424-466 / 16M 479-514 ns vs macOS 118 / 120 / ~150 / 176 / 181; page-spread 214-611 vs 65-92; bandwidth equal 58 GB/s.
  Exclusions done on the txf path only (PTE AP/PXN, VMID TLBI isolation) must be re-checked on device loads if revived.
- EXP2240/2241 CPU chase (tools/memlat/cpu_memlat.c): Windows recovery P-core L1 1.54 ns vs macOS 0.94 (clock ~2.0 vs
  3.2 GHz), DRAM 23 vs 15.6 ns (scales LESS than clock). SoC fabric/DRAM DVFS hypothesis NOT supported.
- Remaining candidates (rank): (1) GPU L2/uncore configuration or clock (256K-1M = GPU L2 range already 2.7x, one
  page 16K only 1.1x); (2) UAT walk/TLB (page-spread 3-8x). Next: kext RE + Asahi/m1n1 for GPU L2/SLC/uncore
  configuration and per-pstate fabric/uncore requests the firmware receives (initdata perf states), offline first.
- Side optimisation found: DXBC SRV raw loads via txf are slower than device loads -> lower SRV raw/structured loads
  of buffers to device loads (production change, measured with MEMBW vs MEMBWUAV).

## UPDATE 2026-11-07 14:00Z -- COMPUTE GAP = MEMORY LATENCY (EXP2226, macOS on the J313)
- mtlsplitd per-dispatch Apple times (run 2, valid): every large C3/C4 kernel is ~2x faster on Apple
  (C3 pieces 1.29/0.66/0.33/0.33/0.25 ms vs 0.605/0.235/0.184/0.150/0.139; 7aa43c6c 1.51 vs 1.13) with static
  code at parity -> not one kernel, a uniform runtime factor.
- Same pointer-chase probe (tools/macos/membw_metal.m = D3D12 MEMBW step) on the same machine: 32-thread dependent
  load ours 364/537/1111/1078 ns at 64K/256K/4M/16M vs macOS 130/120/176/181 ns; bandwidth equal (58-63 vs 56 GB/s).
  EXP2144/2145 "TLB-miss queueing = hardware" is SUPERSEDED.
- WHY CONTINUE COMPARISON: none -- next target is the layer that sets GPU cache/TLB behaviour: (a) device_load cache
  hints in our compiled code vs Apple's encoding, (b) UAT PTE attributes/shareability/page size of our GPU-only
  allocations vs Asahi uat.rs, (c) firmware initdata / m1n1 GPU cache config vs macOS. Offline first; then one
  Windows MEMBW probe run per candidate (32-thread 256 KiB chase ~120 ns = fixed).
- 14:30Z offline: (a) EXCLUDED -- 178/184 loads of fc968f7a are access=none -> device_load u4=4, the same encoding as
  Apple's m183 (0x00f2c8000d040105: bits 44-46 = 4). (b) STRONGEST: every user leaf is MEMATTR 2 (Asahi
  PROT_GPU_SHARED, "uncached"), incl. D3D12 DEFAULT-heap (GPU-only) memory; the MEMBW buffer is one. EXP2137 tried
  MEMATTR 0 for GPU-only leaves (KMD Wom1GpuPrivateCached + m1n1 AGX_GPUVA_PRIVATE_CACHED build) but measured only GM
  fps, not latency, at doubtful coverage; the current m1n1 (brokerlock-split 550c7f23) lacks that build flag.
  Next: confirm macOS's attribute for private GPU memory (kext RE, allowed), rebuild brokerlock m1n1 + GC, then one
  MEMBW probe run with/without Wom1GpuPrivateCached (measurement only: no cache invalidation on unmap).

## UPDATE 2026-11-07 15:30Z -- ACTIVE HYPOTHESIS: WINDOWS TLBIs DROP GPU TRANSLATIONS (VMID 0)
- EXP2230 macOS: page-spread chase 65-92 ns for 4..1024 pages (both layouts); ours (EXP2229) 606-962 ns -> ~400 ns
  per 16 KiB page change even among 4 pages: the GPU keeps almost no translations.
- Mechanism (source): m1n1 invalidates GPU translations with TLBI ASIDE1OS at EL2 with TGE=0 = EL1&0 regime of the
  current VMID 0 (hv_agx_retained_platform.c), and the GPU obeys it; the guest runs as VMID 0 (hv_vm.c VTTBR = table
  only), so Windows' own broadcast TLBIs can hit GPU ASIDs. macOS/Asahi run the host at EL2 (VHE): no such overlap.
- Test: m1n1 exp/m1n1-brokerlock-vmid1 c827d94c (AGX_GUEST_VMID1: guest VMID 1, broker GPU TLBI on VMID 0;
  artifact .local/experiments/m1n1-brokerlock-vmid1, sha 3c4de85f). EXP2231 = control (fixed-offset probe e2230,
  brokerlock-split), EXP2232 = same + vmid1 m1n1. Queued after ANS012-R4/ANS013.
- If EXP2232 confirms: production path = keep the guest off VMID 0 (m1n1 change), then re-measure GM.

## UPDATE 2026-11-07 14:40Z -- LATENCY: ATTRIBUTE MINOR, NEXT = TLB DISCRIMINATOR
- EXP2227 (control, GC m1n1, no switch) reproduces the gap; EXP2228 (Wom1GpuPrivateCached=1, MEMATTR 0 on GPU-only
  leaves) lowers the 32-thread chase 15-35 % from 1 MiB but 64-256 KiB barely move and 4 MiB stays 551-598 vs macOS
  176 ns; GM +1 % (noise). Leaf attribute = minor factor (Asahi also uses MEMATTR 2).
- Offline (subagent, file:line in chat 14:30Z): TTBR format, table descriptors, UatLevelInfo, initdata/hwdata and
  firmware IO maps equal Asahi. Differences: we flush the whole ASID on every leaf publish/unmap (Asahi: none on map,
  range on unmap) -- between jobs only. Guest runs VMID 0, GPU ASIDs 1-63 may coincide with Windows ASIDs: whether
  the GPU honours Windows' broadcast TLBIs is unknown (hypothesis).
- One page (16 KiB) is already 1.4-1.8x macOS; the gap grows with page count while macOS stays flat to 16 MiB.
- NEXT: EXP2229 page-spread chase (probe a6b44daa, 4/64/256/1024 pages, one 64 B line each): rising with N on ours
  but flat on macOS -> TLB entries lost (reach or external invalidation); flat -> cache allocation/clock.

## UPDATE 2026-11-07 07:27Z -- HD FRAME RE-RANKED (offline, existing evidence only)
- Bench lent to the ANS session (ANS010-R1) at ~07:05Z from normal recovery, no AGX package staged.
- EXP2225 default package, HD PERF (gm2225d, 2084 frames, `agx_gpu_frame_breakdown.py --jobs`), per frame 24.77 ms:
  compute exact 13.7, render 3D 4.15 + TA 3.95, launch 1.40, late submit 1.20, foreign (DWM) 0.99, detect 1.10, idle 0.19.
  DWM composition + late hand-off together are <= 2.2 ms/frame (9 %); EXP2212 ETW: VidSch keeps a packet at the
  KMD 96.7 % of the 12.9 s window (GM 4137, DWM 452 packets, 16 preemptions). Independent flip is impossible for the
  windowed 1280x720 benchmark on the 2560x1600 panel without MPO -> not the next lever.
- Compute vs Apple (EXP2192 HD encoders): ours 13.7 vs Apple 9.2 ms (+4.5). C4 7.9 vs 5.8 (+2.1, levers closed
  2026-11-07); the rest 5.8 vs 3.4 (+2.4).
- New decode of EXP2211 arm b (SPLIT_TAGS, previously undecoded): C3 (3.17 ms; Apple c11+c13 ~1.6) is dominated by one
  indirect launch of the camera-hierarchy traversal fc968f7a (~1.29 ms), then pieces of 0.66 / 0.33 / 0.33 / 0.25 /
  0.21 ms. Offline cost of fc968f7a: 128x1x1, 91 GPRs, 1024 threads (max occupancy), 0 spills, 6 atomics, 165 shared
  ops, 2 barriers -- static code is not the cause. The two traversal launches (fc968f7a in C3, 7aa43c6c in C4,
  1.51 ms) are the largest single kernels of the frame.
- WHAT IS STILL UNKNOWN: Apple's GPU time for the same traversal dispatch. Next discriminator (needs macOS on the bench,
  after ANS returns it): extend mtlsplit to commit every compute dispatch of c7/c11 in its own encoder and compare
  fc968f7a / 7aa43c6c dispatch for dispatch. If Apple's launch is also >1 ms the gap is elsewhere in C3/C4; if it is
  ~0.3-0.5 ms the traversal's runtime environment (indirect grid size, buffer/descriptor path, atomics) is the target.

## CURRENT 2026-11-07 03:30Z -- DEPTH-2 KICK FALLBACK S1-S4a ON HARDWARE; DEFAULT PACKAGE GATED
- Accepted default: firmware RX drain on (125bddd1, EXP2219/2220); depth-2 kick fallback S1-S3
  (be8c6f7f+55ea8d68, EXP2222) and S4a (20e34f05, EXP2224: 1380/1380 build_undone, 2 in a boundary
  preemption, 0 resets); window receipt v3 (675486d7). EXP2223 perf gate PASS (GM 41.016, CS 49.451);
  EXP2225 perf gate PASS on the S4a package (GM 40.776, CS 49.812).
- Perf baseline investigation/perf-baseline.json (EXP2220): GM HD PERF 40.770, CS 49.943; gate = warm-up +
  two same-boot repeats, worst gated, 2 %.
- Still reset by design: S4b/S4c (QueueFailed inside RunSuccessor) and S5 (adoption) -- possibly visible;
  S4b proof deferred (no natural occurrence observed).
- Priorities (investigation/GPU_DRIVER_ARCHITECTURE.md): OV5 TA waiver hang (EXP2214), compute runtime gap
  (+4.5 ms/frame HD vs Apple), independent flip.
- C4 offline levers closed (do not reopen): code parity with m187 (1159 vs 1177 instrs), dependent-load latency
  (L4 EXP2183 neutral), wait placement (W1 EXP2124 neutral), occupancy (ours 79 halfregs / 1024 threads = max; Apple
  m187 uses up to r72), memory bandwidth (MEMBW). The 2026-11-04 "Next: C4 wait placement" line below is superseded.
  Remaining C4 question needs hardware counters (per-kernel GPU time vs Apple), not compiler guesses.
- Gate: tests/run_against_baseline.py (1060 tests incl. render/depth2 suites) via scratchpad gated-commit.sh.

## CURRENT 2026-11-05 18:00Z -- OV2 IMPLEMENTED (default off), BLOCKED ON GRAVITYMARK BY QUERY RESOLVES; NEXT OV3
- EXP2193: GravityMark frame = 8 jobs CRCRCRCR; C->R carry 16-18 D3D12 transitions; R->C carry no app barrier, but
  each R is submitted by ResolveQueryData (pipeline-statistics heap, 1024 queries) and Asahi copies the results on the
  GPU inside the next compute batch (agx_query.c flush_query_writers + libagx_copy_query_gl): C depends on R.
- OV2 on integration (820cab01 KMD, e4090c71 UMD, 35ff4944 guard; Wom1Ov2NoHazardLead, default off, needs Q2c):
  satisfied lead Barrier for a UMD-proven independent compute successor; the UMD refuses independence after foreign
  owner work (resolves) or early dispatches, so it does not fire on GravityMark yet. Not run on hardware.
- Next (OV3, UMD): defer the resolve GPU copy to the next barrier / ECL end and emit it in the following render's
  prelude CDM (ordered behind C on the compute FIFO and behind R on the render FIFO), so C becomes independent of R.
  Potential: overlap R1||C2, R3||C4, R5||C6 (macOS overlaps 4.3 ms FHD, EXP2192). exp/ov2-rc is superseded, unmerged.
- Bench note: EXP2194 is the c4split run of another session; the next free EXP number follows its recovery.

## CURRENT 2026-11-05 17:30Z -- GOAL FHD >= 7814 (user protocol 2026-11-05); Q2c + F1 WORKS; FHD best 5396
- Goal (user): GravityMark D3D12 1920x1080, 100k asteroids, -temporal 0, windowed, same GravityMark build as macOS, >= 7814 in
  three consecutive pre-agreed FULL FHD runs after a fresh boot; correct image, no fault/TDR/reset/removal, full suite.
  macOS reference: 7814 / 46.8 FPS on this J313 (macOS 15.7.8, Metal, 2026-11-02, powermetrics 1278 MHz 100 % active).
- Measured (GravityMark's own FPS, Time 167 s): FHD 5268 / 31.5 FPS (EXP2183, L4 opt-in arm), 5396 / 32.3 FPS (EXP2190, Q2c on,
  first arm) -> ~31.0 ms vs 21.4 ms: -9.6 ms/frame (-31 %) still needed. FULL score and PERF window are separate metrics.
- Accepted defaults: M1b (EXP2182). Rejected: L4 (EXP2183), O1 (EXP2185), G1 (EXP2188, reverted). Opt-in validated, flip pending:
  Q2c Wom1ComputeOwnQueue + F1 (EXP2189: compute kick delay 1.69 -> 1.03 ms/frame HD; EXP2190 FHD 5396; EXP2191 = off control).
- EXP2184/2186/2187 correction: refusal = provider phase (line 3547), cause = backend ended render-with-compute on TA+3D (F1 fix).
- Protocol: one variable per run, ABBA or cross-boot pairs with identical arm order (FHD first: position/heat drift ~1.5-2 %),
  accept only by repeatable frame time; owner of each cost named (app/DXIL, UMD/compiler, KMD, m1n1, firmware, DWM).
- Next: finish Q2c control (EXP2191) -> FHD frame budget on the accepted package (receipt run) -> largest measured item.
  Metal per-pass timing needs a macOS boot (Instruments Metal System Trace): asked from the user once.

## CURRENT 2026-11-05 07:00Z -- DEFAULTS: retained VM slots + unpinned job thread (10fd111e); WORKING m1n1 = broker-lock split
- Working/lastgood m1n1: .local/experiments/m1n1-brokerlock-split/m1n1-g2-abi6-iomfb-owner-brokerlock-split.macho
  (exp/m1n1-brokerlock 550c7f23, AGX_BROKER_LOCK_SPLIT=1, sha 1c35a20b; user-approved). Emergency: m1n1-075e3980-guestram.
  Launch with AGX_M1N1=<split> AGX_M1N1_LASTGOOD=<split or 075e3980>.
- ABBA EXP2176-2179 + checks EXP2180/2181: B-behaviour FHD 5200 / HD 6456 vs A 5150 / 6385 (+1.0 % / +1.1 %).
  Bests: HD 6581 / FHD 5266 (EXP2175, single run). Run-to-run spread across boots ~1.5-2 %.
- Defaults verified by receipts (EXP2181): retained slots active, job thread unpinned. REG_DWORD 0 restores either.

## CURRENT 2026-11-05 03:00Z -- BEST HD 6581 / FHD 5266 (EXP2175, combined stack)
- Stack: experimental m1n1 exp/m1n1-brokerlock 550c7f23 AGX_BROKER_LOCK_SPLIT=1 (macho 1c35a20b, user-approved, not
  lastgood) + Wom1G3RetainedSlots=1 + Wom1JobThreadAnyCore=1. Frame idle R7->C0 ~1.0 ms (was ~1.25).
- EXP2173/2174: with the job thread pinned to P cores, 4/32 post-preemption pickups took 1.6-17 ms (late wake of an idle
  pinned vCPU, recovered at ~vsync); unpinned: 0/96. Wom1PreemptTrace v2 (KeSetEvent/wake stamps) is in the package.
- Open: one 21 ms outlier (EXP2174 mid12); make the stack default (needs a decision on m1n1 lastgood and the pinning
  default); root cause of the late pinned wake (m1n1 vGIC/WFI or Windows scheduling).

## CURRENT 2026-11-05 01:30Z -- BEST HD 6524 (EXP2159) / FHD 5200 (EXP2168, V1 on)
- Per-frame idle between GravityMark's R7 and next C0 ~1.25 ms (deployed m1n1): DWM submit 70 us + DWM begin/build
  ~250 us (lease ~200) + DWM GPU 0.64 ms + C0 begin ~300 us (lease ~200).
- V1 retained VM slots (Wom1G3RetainedSlots, default off): works on hardware (EXP2165-2168, no faults), leases ~10x
  fewer; the remaining post-DWM V5 call cost was broker_lock held by m1n1's scanout service step (EXP2169).
- m1n1 split build (exp/m1n1-brokerlock 550c7f23, AGX_BROKER_LOCK_SPLIT=1; user-approved experiment, NOT lastgood):
  removes that wait (EXP2170/2171) but 2 of 5 sampled frames showed 3-4 ms DWM stalls -> no FPS gain yet.
- Rejected: preemption-path cost (EXP2172), steady-state VidMm migration (M3), L2 lock split (EXP2160 bugcheck).
- Probe rule: upload arm-task.ps1 from the Mac; check ARM_TASK and gm*-done.txt (EXP2169/2170 skipped the game).

## CURRENT 2026-11-04 19:30Z -- BEST HD 6524 / FHD 5145 (EXP2159); NEXT: V1 RETAINED VM SLOTS
- EXP2163 (L1b split): the remaining process-switch cost is the V5 LEASE (slot write + TLBI ASIDE1OS + DSB OSH), 160-272 us
  per switch, twice per frame (GravityMark <-> DWM share slot 1). Design: analysis/V1-retained-vm-slots.md (per-process
  retained slots, release-before-mutate, switch Wom1G3RetainedSlots).
- Rejected today: L2 lock split (EXP2160 bugcheck 0x113_10: non-LIFO guarded/fast mutex release returned at APC_LEVEL;
  reverted), L3-P1b as the cause of the post-DWM BeginJob (kept, neutral), steady-state VidMm migration (M3 analysis:
  census op 9 = VIRTUAL_FILL of new BOs during the first ~2 s; zero paging from 25.7 s to 71.3 s).
- Probe habit: snapshot receipts mid-arm (kmd-receipts mid-a at iteration 12); after-arm dumps are overwritten by the desktop.

## CURRENT 2026-11-04 16:00Z -- BEST HD 6524 / FHD 5145 (EXP2159, defaults incl. L3-P1)
- EXP2157 (L1 lock profile): DWM's completion waited 0.14-4.4 ms for the adapter-wide G3 lock held by GravityMark's
  preflight parse; every job is parsed 3x under that lock. L3-P1 (2bf8248d, 32-entry proof cache + cached output checks)
  cut preflight/submit holds 92/149 -> 15/12 us (EXP2158, HD PERF +2.2 %). Remaining waiter: BeginJobAs (~38 us/job).
- Next: L2 per-process lock split (investigation/analysis/L2-g3-lock-split.md, step 1 in review), L3-P2 BeginJob
  re-parse elision (analysis/L3-g4-parse-cost.md). Always watch recovery with a notifying waiter.

## CURRENT 2026-11-04 12:00Z -- R7 -> C0 SPAN EXPLAINED (EXP2155/EXP2156)
- No KMD present blit runs (flip model, EXP2155). The span is one DWM render job per GravityMark frame (pid 11156,
  kick -> complete 0.65 ms) plus two unpaired boundaries (~0.4 ms each): the depth-two window refuses cross-process
  pairs (refusal 55) and render/render pairs with a different manager key (58).
- Driver-owned levers: (1) cross-process pairs (needs a cross-VM pair class and a resident second manager slot);
  (2) DWM composition cost (0.65 ms; check render-target size and tile load/store scope).

## CURRENT 2026-11-04 10:30Z -- BEST HD 6433 / FHD 5091 (EXP2150 control, defaults); NOTIFIER LINE CLOSED
- Best defaults: EXP2150 (package 827f7342, switch-free): HD 100k T0 FULL 6433, HD PERF 38.34 FPS, FHD FULL 5091, suite 119/0.
- Closed today (hardware): S1 stamp completion (EXP2147, +3.5-6 % then firmware crash: early release of slot/sidecar
  objects), N1 deferred threshold (EXP2148: firmware notifies only on count == threshold), N2 second notifier
  (EXP2149/EXP2151: the firmware never raises a second notifier on the shared work queues/JobList, generation fix did
  not help). The boundary gap (~1.6 ms/frame) needs a second Asahi Queue (own notifier_list + work queues): architectural.
- New tool: Metal oracle .local/metal-oracle (Apple's AGX binaries of all 274 GravityMark functions, applegpu listings).
  C0 bdb1944a = Apple m164 (1913 vs our 3543 instrs, same register use; the gap is already in the DXIL: 32 vs 12 sin/cos);
  C4 traversal 97ae4b02 ~ Apple m187 (1214 vs 1177 instrs): C4's cost is execution, not code size.
- Accepted (defaults): M1 f5dcae3a identity index masks dropped (EXP2152: HD PERF ABBA +0.7 % both pairs); M3 7c8d834f
  non-speculative LICM after texture crawls (EXP2153: HD neutral, FHD PERF +0.9 %; HD FULL 6439 best). FHD FULL falls
  with arm position (5091 3rd, 4865 6th, 4736 8th): run FHD FULL first.
- Workload finding (oracle): GravityMark's D3D12 DXIL is heavier than its Metal code before our compiler sees it (C0:
  32 vs 12 sin/cos; planet PS: 64 vs 24 sin/cos, 228 vs 137 loads, outer loops unrolled by dxc). macOS 9739 is not an
  equal-work target; driver-owned remainder: boundaries (~1.6 ms/frame, needs a second Asahi Queue or depth-K window)
  and latency hiding (C4 traversal: 34 waits / loop vs Apple 17, mean load->wait 6.4 vs 15 instructions).
- Next: C4 traversal wait placement (scheduler) against oracle m187.

## CURRENT 2026-11-03 18:00Z -- GRAVITYMARK HD 6257 / FHD 4862 (EXP2134, defaults); TARGET macOS HD 9739
- Baseline (switch-free package, dbd498a8 defaults): EXP2134 HD 100k -temporal 0 = 6257 / 37.4 FPS, FHD 4862 / 29.1 FPS,
  suite 119/0. Compare only at -temporal 0 (macOS HD 9739 = ~17.2 ms/frame; ours ~26.7 ms).
- Accepted today: shared-load vectorization (EXP2110), prelude pairs + compute-on-compute depth-two class on by default
  (EXP2129-2134: compute -> prelude start 0.59 -> 0.063 ms). Receipts: Wom1TaStartTimestamps (TA starts valid, EXP2126).
- Closed (hardware): W1 waits (EXP2124), D3D robustness (EXP2057), K1 spacing (EXP2080), per-dispatch overhead (EXP2077),
  trace I/O (EXP2119), update_ts alone (EXP2125), prebuild of unpaired successors (build ~5 us, EXP2132 ring).
- Closed 2026-11-03 evening: 16-bit min precision (EXP2135: Tellusim emits no min16 on D3D12 under 16-bit caps;
  switches APPLE_AGX_D3D12_MIN16_CAPS / APPLE_AGX_DXIL_MIN16 stay off); GPU-private cached UAT leaves (EXP2137 + correction:
  MEMATTR 0 for GPU-only memory gives HD 6240 / FHD 4857 = no change; experiment m1n1 ed6fd218 + Wom1GpuPrivateCached
  stay experimental, no FwCtl invalidation, one engine reset seen). Within-boot run order moves scores ~3 % (EXP2135):
  use ABBA for A/B.
- Planet PS measured (EXP2140, APPLE_AGX_D3D12_NULL_PS ABBA): at HD 100k the full-screen PS is 428487dae22a5191
  (the 71405b87 capture is HD 50k: GravityMark shader bytes depend on the run config; EXP2138 was inert, EXP2139 capture
  .local/experiments/EXP2138-nullps/evidence/EXP2139-capture). Nulling it saves 1.57 ms/frame (+6.4 % FPS) = about half
  of R1 3D and the ceiling for any work on it (7335 instrs, 255 GPRs, 384 threads, 152 spills offline).
- RC124 (664d6d10, opt-in APPLE_AGX_REMAT_INPUTS=2): re-issues far-used pure values (interpolated inputs, read-only
  global-load trees) after the last CSE. EXP2142: planet PS 6677/255/111 spills -> 6029/231/0 on hardware, +0.9 % HD PERF
  ABBA, FULL 6333, suite 119/0, image correct. Pending: default on after an HD+FHD FULL check. Level 3 (ALU roots) gave
  nothing offline (long-lived ALU values depend on textures). Remaining PS lever: occupancy (231 > 208 halfregs).
- Memory path measured (MEMBW probe step, EXP2143-2145): stream 58-63 GB/s (M1 peak 68.25); dependent miss over 64 MiB
  ~1.3 us single, ~5 us with 1024 concurrent misses (TLB-miss queueing, hardware). Memory is not the C4 gap. RC124
  level 2 is the default since 69464750 (EXP2143: HD FULL 6380 best, FHD +1.9 %). GravityMark uses one DIRECT queue.
- Next by size: render -> compute boundary idle ~0.4 ms x 4 per frame (EXP2142 a: gap->start 0.37-0.40 ms before every
  compute) -> depth-three job window (analysis/G2-pre-3d-span.md "Depth-three feasibility"); C4 compute 7.6 ms.
- Next compiler targets: planet PS 428487da register pressure (ceiling 1.57 ms); C4 kernels are memory/latency-bound (trio ~42 M instrs in 1.51 ms) for reasons other than UAT attributes.
- GPU budget per frame, current defaults (EXP2131 HD medians, scratchpad budget.py): C4 compute 7.70 ms; R1 3D 2.99
  (FHD 4.91); R5 TA 1.78 + 3D 0.80; C0 1.72 (FHD 3.34, = bdb1944a full-screen compute: 921600 threads x 3543 instrs,
  207 GPRs -> 512 threads, fp32 only); C6 1.55; R7 TA 0.74 + 3D 0.82; C2 0.68; idle before each compute 4 x 0.40 ms.
- Remaining idle (1.6 ms/frame) needs completion that does not wait for the deferred notifier event (U4: the event is
  deferred until every kicked job ends, Asahi queue/mod.rs:306); deeper pipelining (depth three or early retirement)
  therefore needs stamp-based completion: architectural, see analysis/G2-pre-3d-span.md. rc95 ring receipts: TA done
  pointers visible ~300 us before Complete, stamps only 20-40 us before (inconclusive whether stamps are written late or
  not polled).
- Execution is the main gap: ~21.6 ms of GPU work vs Metal's whole 17.2 ms frame. Next targets by ms: C4 kernels
  (traversal 7aa43c6c/97ae4b02, trio, indirect atomics), R1 fragment shaders, C0 bdb1944a.
- Workflow (analysis/ITERATION_WORKFLOW.md): agx_gravitymark_run.ps1 -Mode FAST|PERF|FULL, tools/agx_gm_perf.py
  (PERF = 10 + 60 s window), tools/agx_iterate.py plan. Launch: m1n1 075e3980 expanded RAM, Wom1GpuFaultInfoRead=1.

## CURRENT 2026-10-26 14:00Z -- GRAVITYMARK IS CPU<->GPU SYNC-BOUND (EXP1968); TARGET >= 3000 POINTS
- User target: GravityMark D3D12 score >= 3000 (native M1 Metal is about 9,950 / 60 FPS at 100k asteroids, 720p). We are at 750-800 / 4.5-4.8 FPS at 2k-10k asteroids. 2k and 10k run at the same speed.
- Stable package: EXP1967/EXP1968 (integration ff298b99). Counted ExecuteIndirect uses the count-bounded native path (CPU count read, path=2 reason 7). The GPU-generated VDM stream is opt-in (APPLE_AGX_D3D12_GPU_INDIRECT_STREAM=1): EXP1966 hit 0x119, and rc38 fixed the fence accounting and the links, but this is not hardware-validated. rc32 is removed.
- EXP1968 timing (rc35 receipts plus tools/agx_submission_analyze.py), median 2k frame of 180.6 ms:
  - GPU busy is about 9.3 ms;
  - query replay is 59.4 ms for 9 commands;
  - draws are 114.7 ms for 13 (7 counted, with CPU count reads);
  - dispatch is 1.1 ms for 146.
- The frame is CPU waiting on the GPU. Next: rc40 (GPU-timeline ResolveQueryData and no-wait queries), then the GPU stream arm.
- Open correctness bug: the asteroids render flat white. rc39 shader capture uses APPLE_AGX_D3D12_SHADER_DUMP_ALL=1 with APPLE_AGX_D3D12_SHADER_DUMP_DIR.
- Faults: 0x116 in EXP1964/1965 (unmapped CDM read 0x11ac9a0000 on the removed rc32 path; TDR quiesce cannot succeed while the firmware never retires). rc37 added ChannelError recovery and an opt-in fault drill (Wom1GpuFaultDrillAllowed); timeout-only recovery is still open.
- Launch: m1n1 075e3980 with AGX_GUEST_RAM_PROFILE=expanded; lastgood 0d3a75b3; AGX_DEVICE_SWITCHES=Wom1GpuFaultInfoRead (full pool).

## CURRENT 2026-10-26 10:30Z -- GRAVITYMARK D3D12 COMPLETES (2k 352 / 1.8 FPS, 10k 256 / 1.4 FPS, EXP1961)

- Host: the Mac (J313 over USB). The PC is the builder plus Codex. Hosting the J313 from Windows failed: see the EXP1940 entries.
- Operating point:
  - m1n1 075e3980 (branch codex/m1n1-guestram, macho .local/experiments/m1n1-075e3980-guestram, sha 452ab29f);
  - run_uefi `--guest-ram-profile expanded` plus `M1N1_PROXYCLIENT=.worktrees/m1n1-guestram/proxyclient` (prepare.sh sets both when AGX_GUEST_RAM_PROFILE=expanded);
  - guest RAM 6.88 GB (was 6.28);
  - device switch Wom1GpuFaultInfoRead=1 and the FULL 2688 MiB native pool (no Wom1PoolFraction; EXP1962: 2k 744 / 4.4 FPS, 10k 755 / 4.5 FPS, paging stalls gone).
- Fixed on the way (EXP1941-1961):
  - query-source BO residency (rc6);
  - bounded heap walks, CantTrimFurther, spill-helper references (rc7);
  - error-cause preservation (rc11);
  - user-mode device removal diagnosed (dxgkrnl RE plus ETW: VidMm "Unrecoverable page in failure" on a full single segment).
- Proven limits:
  - the pool backing promise must stay at min(live, cap), as for dGPU VRAM (rc28);
  - GravityMark genuinely needs about 2 GB GPU plus 2.6 GB process memory (rc29);
  - guest RAM was the binding limit.
- Open:
  - stalls remain: median frame 0.22 s, but 15-21 s paging stalls and about 2k trims. EXP1962 tests the full 2688 MiB pool on the larger RAM;
  - rendering throughput (Night Raid-class costs);
  - the regression check of D3D11/GL/Night Raid on the new m1n1.

## CURRENT 2026-10-25 02:30Z -- MOVED TO THE WINDOWS PC (WSL); EXP1940/EXP1941 BUILT, WAITING FOR THE J313

- Work environment: WSL Ubuntu on the builder PC (repo at /Volumes/pwdev/public_windows, symlinked). Offline
  baseline BASELINE_OK on Linux (host Mesa tree rebuilt natively; 351d5b4d).
- Builder: everything under C:\Users\pauls\APPLE AGX\active, one root APPLE_AGX_BUILDER_ROOT =
  C:\Users\pauls\APB12E~1\active (8.3 alias, no space) (7bdafb7c); README.txt there. Obsolete -> APPLE AGX\old.
  WSL pipeline names the root in .local/scratchpad-tools-backup/agx-env.sh; build-chain.sh replaces chain<N>.sh.
- Built, not yet run (J313 not in m1n1: USB 3-6 "Device Descriptor Request Failed"; needs a power cycle):
  - EXP1940 30.0.1940.0 (02c1393e): val1 validator + 4 candidate fixes + C-linkage fix; GravityMark D3D12 10k
    with gm-run.ps1 -ValidateRefs 1, switch Wom1GpuFaultInfoRead only. before1940.txt ready.
  - EXP1941 30.0.1941.0 (2e6d8976): EXP1940 + Codex t14 (firmware-killed retired jobs complete as aborted;
    fault VA in DMA_PAGE_FAULTED). before1941.txt ready. Run after EXP1940.
- On J313 return: one recovery boot (rec1939h) to validate the WSL m1n1 chain (udev /dev/cu.usbmodem*,
  scheduled task J313-usbipd-attach, proxyenv USE_CLANG), then launch-exp.sh for EXP1940.

## CURRENT 2026-10-23 21:30Z -- GRAVITYMARK D3D12 ONLY (user focus); ACTIVE HYPOTHESIS T7h

- Installed: EXP1918 package 30.0.1918.0 (b57cd075) with the UMDQ-1918q5 UMD hot-swapped (d8e1612f...). Restore with `umd-hotswap.sh restore`, then run recovery rec1918h/recover.sh directly.
- Fast loop: `umd-quick.sh` -> `umd-hotswap.sh` -> `gm-quick.sh` (~6 min, UMD only). KMD changes use the short profile with a reboot.
- PROVEN (EXP1918U5, 68a97a70; `tools/agx_gm_alloc_join.py`): the G4 Parse/Unmapped refusal (Va 0x1192ad0000, 129 MiB) hits a committed resource/owner BO, not a heap. The BO was never CPU-mapped; only three 128 MiB WRITE_COMBINE upload heaps are mapped. T7f unknown #1 is closed.
- ROOT CAUSE (source): `agx_win32_asahi_bo.c:356-360` makes every General BO CpuRead|CpuWrite (Win32Flags 0xf), including GPU-only resources, so under pool pressure VidMm may place them on scattered 4 KiB aperture pages.
- Tried and rejected: CpuVisible=1 without the aperture (EXP1027, T7c/EXP1917 -> E_INVALIDARG). The segment pool cannot grow: 2688 MiB is the Windows 50%-of-RAM limit, and the peak reserve was 2953 MiB.
- ACTIVE HYPOTHESIS T7h (Codex, `.local/handoff/codex/t7h-gpu-only-local`): GPU-only BOs (committed DEFAULT textures, NOT_AVAILABLE heaps) are created with CpuVisible=0 and a native-leaf-segment-only placement, so VidMm evicts them instead of using the aperture. The next hardware run is its KMD package with the short profile. The T7f shadow stays the fallback.

## (was CURRENT) 2026-10-23 18:30Z -- CODEX OFFLINE + CHAINED BENCH; GRAVITYMARK D3D11 COMPLETES; PERF = COPY ESCAPES + CPU RETIRE

Workflow (see CLAUDE_WORKING_METHOD "Offline work through Codex"):
- Codex gpt-6-astra runs offline tasks from `.local/handoff/codex/<task>/TASK.md` (scratchpad `codex-run.sh`). Each branch is merged only after `codex-review.sh` prints REVIEW_OK.
- The bench runs unattended: `chain-next.sh` and `launch-exp.sh`.
- The fork session (kmd-native-output-bin-4) owns the D3D12 owner, tiled and volume work.

**Validated on hardware (EXP1907-EXP1912)**
- C70 staged uploads; C71 autogen RT; C72 BC sub-block copies; C73 LowMemory floor (superseded by T8, see below); C74 index-buffer binds; fork C21H + 51e4b51b; C80 presentation readback.
- Codex T2 reinterpret copies: GravityMark D3D11 runs the full 240 s benchmark (EXP1912), but at ~0 fps.
- Rejected: C67b and C76 (redirected-blt caps, no ETW change). The redirected-blt track has no evidence-backed candidate (Codex T5 note).

**Performance**
- GravityMark moves 10-71 GiB through 64 KiB copy escapes on >16 MiB buffers. Fix: C75c, Codex T6b (direct buffers up to the class limit, evictable when unmapped). Hardware result pending: EXP1913.
- Night Raid frame ~146 ms (fork C82 analysis):
  - 68 ms is GBuffer draw_vbo at ~140 us/draw; C82 receipts give the verdict, Night Raid run on EXP1914 approved;
  - 53 ms is the CPU retire after each Execute; fix C83 (Codex T9, opt-in APPLE_AGX_D3D12_GPU_WAIT=1), not yet on hardware.

**Open defects**
- GravityMark D3D12:
  - G4 Parse/Unmapped submit refusal; T1 race fix not sufficient; Codex T7 receipt v3 provenance in EXP1914;
  - OOM at 20-22 s;
  - map-failure crashes, fixed by T6/T6b NULL-safety (in EXP1913).
- DWM c00001ad under GravityMark memory pressure (EXP1911). Fix: Codex T8 M2-f7c headroom policy (High && !Low + immediate reclaim), in EXP1914.

**Queue**
1. EXP1913: C75c + C77 shared open + T2b, running.
2. EXP1914: T8 + T7 + C82; build restarted after an X86B FRESH_OUTPUT failure.
3. Night Raid on EXP1914 (nr-c82.sh), then rec1914h.

## (was CURRENT) 2026-10-23 12:00Z -- NIGHT RAID COMPLETE (1379); D3D12 FL 12_1; m1n1 0d3a75b3

The experiment m1n1 is 0d3a75b3 (vGIC running priority + IPRIORITYR fix; boot- and load-stable EXP1905/1906). Lastgood is 21c3648d; prepare.sh writes the lastgood pair.

**Validated**
- Night Raid GT1+GT2 complete: graphics score 1379 (6.48/6.77 fps at 1080p windowed; EXP1906N). This needed:
  - 188d73e2: a refused scene no longer wedges the GPU;
  - 37123151: transient/1x1 ZLS.
- D3D12 FL 12_1 by default (C68, EXP1904); ROV=0 or CR=0 gives 12_0, FL12=0 gives 11_1.
- Tiled Tier 2 by default, min/max, general Texture3D (the fork, EXP1902-1904).
- Portal 2 renders through D3D9On12 at ~60 presents/s (C66, EXP1901G), but the window is invisible: the legacy redirected-blt gap. C67 cap rejected; next is the DxgKrnl ETW discriminator (dxg-etw.ps1).
- C69 D3D11 typed UAV caps: GravityMark D3D11 now creates its kernels (EXP1906).

**In the next package (EXP1907)**
- C70: compressed-texture uploads through one cached staging. Fixes GravityMark D3D12's E_OUTOFMEMORY at ~20 s.
- C71: D3D11 GENERATE_MIPS render targets with UAV. Fixes GravityMark D3D11's device removal at 41 s.
- The fork's C21H BC volume fix.

## (was CURRENT) 2026-10-23 08:10Z -- NIGHT RAID GT1 SCORES; GT2 BLOCKED BY KMD PREEMPTION

Last installed package 30.0.1899.0 from a479e796 (EXP1899, m1n1 21c3648d).
Regression on that package: 118/0.

**Validated**
- Night Raid Custom GT1 completes and is scored: 6.90 fps at 1920x1080
  windowed with the UMD trace on (EXP1899N).

**Blocked**
- GT2 fails with 0x887A0005 (EXP1899N). The UMD is clean. The KMD's
  Wom1GpuReset shows a VidSch engine reset after DxgkDdiPreemptCommand
  could not begin a boundary preemption (scheduler_windows.c:399): a queued
  render packet with fences 0x65e2/0x65e3 was never dispatched, and no job
  was on the firmware.
- After that, DWM crash-looped (0x8898008D) and the desktop stayed black.
- NEXT CAUSAL TARGET: which BeginBoundaryPreemption condition fails. The
  receipt-only Wom1PreemptRefusal (9d102cd5) records it, in EXP1901.
  Rerun GT2 with async compute off.

**Fixed, awaiting hardware**
- Portal 2 (D3D9On12) presents a 4x back buffer. The presentable rule
  excluded MSAA, so present failed with reason 2 and the device was removed
  (EXP1899G). Fix: C66 f7e6947b, a resolve into the 1x presentation
  allocation, as in C40. Probe: AGX_D3D9_*_MSAA_PRESENT.
- C53 reserved buffers without a heap handle (c848b78f, the fork). Tier 2
  checkpoint: rerun in EXP1901.

## (was CURRENT) 2026-10-22 21:00Z -- REAL WORKLOADS: NIGHT RAID GT1, D3D9On12, FULLSCREEN RGBA

Last validated package 30.0.1885.0 from 4e141c27 (EXP1885, m1n1 21c3648d RGBA).

**Validated**
- Night Raid GT1 renders (EXP1876G).
- GT2's refused pixel shader is fixed: a dynamic SRV range overran the next
  range (40a70224, offline-proven from the EXP1879G dump). GT2 still needs a
  hardware rerun.
- D3D9 renders through D3D9On12 on the WOW64 Umd32, which is the Portal 2
  path (EXP1883). The fault was a scissored pipe->clear for a whole-surface
  rect; integer clear colours are now converted (ff88ebed).
- Logic ops: C57, OutputMergerLogicOp (EXP1883).
- Fullscreen R8G8B8A8 (D3D-FS1):
  - A8B8G8R8 source modes need distinct mode Ids (4e141c27).
  - DXGI keeps the A8R8G8B8 desktop mode and presents through it
    (EXP1885F).

**Open**
- GravityMark refuses feature level 11_0 on D3D12 and on D3D11 (EXP1881G).
  Plan: investigation/analysis/D3D12-FL-program.md (11_1, then 12_0 with
  tiled resources on AGX's native sparse page tables). See also
  D3D12-C53-tiled-resources-study.md.
- 0x101 / all-CPU hang under app load (Night Raid GT2 EXP1876G, GravityMark
  D3D11 EXP1889 and EXP1890, 2/2 with GravityMark). The cause is a stale
  ACTIVE list register in m1n1's software vGIC (EXP1890 HV WATCHDOG
  snapshots): the running priority pins at 0x80, so the timer and IPIs never
  signal on that CPU. The fix is in its own session ("Fix m1n1 vGIC stale
  active LR"). No GravityMark run until that fix.
- D3D11 FL 11_1 is the default (EXP1889).
- D3D12 by default (EXP1897, probes 118/118 on three ABIs): FL 11_1, ROVs,
  CR Tier 1 (VS, tessellation, application GS, MSAA); =0 variables opt out.
  12_0 waits on tiled Tier 2 + min/max (C53/C65 session).
- EXP1894: the first vGIC running-priority m1n1 (db990f95) hung Windows boot
  (SGI 0 ACTIVE prio 0x10 on 5 CPUs). Snapshots went to the vGIC session;
  m1n1 stays 21c3648d.
- D3D9: the "<>" D3D9-slot marker routes Direct3DCreate9 through D3D9On12
  (EXP1891: DEFAULT and fixed-function draws PASS).

**Process**
- Steam launches on the guest kick the user's CS2 session: ask first.
- The WOW64 Umd32 must be rebuilt per package (X86B recipe).
- Run syncheck through $S/syncheck-d3d12-run.sh only.

## (was CURRENT) 2026-10-22 12:50Z -- D3D12 DEFAULT IS INTERFACE 0076 WITH SM 6.5

Last validated package 30.0.1873.0 from 3f84931e (EXP1873, rec1873h PASS).
- Default (SM6 processes): R7 build 6 (0076), SM 5.1-6.5. Probes 107/107 on
  ARM64 and x64 (C50A SV_ViewID, C51A denorm, C52A dot4add/WaveMatch/
  WaveMultiPrefix); 19 samples (2 need tiled resources / two adapters) and
  MiniEngine ModelViewer pixel-identical (0x74d932e6). Opt-outs:
  APPLE_AGX_D3D12_DDI=0012|0033|0042, APPLE_AGX_D3D12_SM6=0 (0003).
- Interface ladder (D3D12-C50/C51 notes, test_d3d12_interface_0033 derives
  every retyped member from the WDK header): R2+ runtimes need the
  extended-features table, the 0022 core-layer allocate/deallocate (owner
  bridge), caps 1062/1059/1057/1069/1071/1072 "none" and the 0022 texture
  layout caps. The runtime accepts 0033, 0042, 0054, 0065, 0076, 0099 but
  never 0071 (APPLE_AGX_D3D12_DDI=scan|sNNN measure it).
- SM 6.6 is blocked: AGX has no 64-bit atomics (Asahi/Honeykrisp).
- FL 12_0 needs tiled resources tier 2 and min/max filtering
  (D3D12-C53-tiled-resources-study.md; min/max is not in AGX hardware).
  Capabilities may not be advertised before their DDIs exist, so tiled
  work starts offline (no measurement profile).
Next: tiled resources tier 1 (buffers, then 2D), real D3D12 applications
(no FL 11_0 game/benchmark is installed on the guest: 3DMark has only Solar
Bay and the sampler-feedback test; Portal 2 is D3D9).

## Prior state superseded by EXP1869-EXP1873

### (was CURRENT) 2026-10-22 10:40Z -- TYPED UAV LOADS VALIDATED; INTERFACES 0033/0042/0071 IN HARDWARE TEST

Last validated package 30.0.1868.0 from 634d1535 (EXP1868, rec1868h PASS).
- Default unchanged: R1 build 2 (0012), SM 6.0, wave ops; probes 104/105
  (C50A needs the 0033 opt-in). TypedUAVLoadAdditionalFormats validated in
  both frontends (C49A; EXP1867 DXBC, EXP1868 DXIL after 17cbad44:
  dxil-spirv typed_uav_read_without_format, else every read typed UAV was
  R32). ModelViewer stage-1 pixel 0x74d932e6 is the baseline
  (CORRECTION-EXP1867: 0x5cd732e6 there was the typed-load defect).
- Interfaces, all opt-in via APPLE_AGX_D3D12_DDI=0033|0042|0071 (SM 6.1 /
  6.2 / 6.3-6.5; D3D12-C50-interface-0033.md, D3D12-C51-interface-plan.md):
  tables project onto the R1/0003 entries (test_d3d12_interface_0033 derives
  the retyped members from the WDK header). EXP1868 (0033): R3 negotiated,
  then the runtime required the extended-features table (type 8, dc21562d)
  and the owner found a NULL kernel callback (6aed4bc1: receipt
  d3d12-kernel-callbacks + bridge to the 0022 core-layer allocate). EXP1869
  runs 0033/0042/0071 profiles.
- Gate: syncheck-d3d12-opt.ps1 (/O2) besides /Zs (C4701 broke one build).
Next: make the highest validated interface the default; then tiled
resources tier 2 for FL 12_0 (design study running), real games.

## Prior state superseded by EXP1867-EXP1868

### (was CURRENT) 2026-10-22 08:30Z -- SM 6.0 + WAVE OPS ARE THE DEFAULT D3D12 PROFILE

Last validated package 30.0.1866.0 from f1b256e7 (EXP1866, rec1866h PASS).
- Default: R1 build 2 (0012), SM 6.0, wave ops; APPLE_AGX_D3D12_SM6=0 keeps
  0003/SM 5.1. Probes 103/103 on ARM64 and x64; 17/19 samples (the other
  two: tiled resources not advertised, two adapters needed); MiniEngine
  pixel-identical to the DXBC path.
- EXP1862-1865: R1 CachedPSO heap corruption was ours -- the zero-cache
  library answered size 0 for nonzero initial data and Create wrote past
  it (2e74d659); found with cdb on the guest (dbgrun.ps1, run-dbg.sh).
Next: SM 6.1+ (SV_ViewID, barycentrics, 16-bit), tiled resources, real
games; the Asahi path reading the HS winding is still unlocated.

## Prior state superseded by EXP1862-EXP1866

### (was CURRENT) 2026-10-22T05:59Z -- SM 6.0 + WAVE OPS VALIDATED ON ARM64 (SM6 PROFILE); TESS WINDING FIXED

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

## 2026-10-27 GravityMark D3D12 performance state (EXP1985-EXP1995)
- Accepted package line: integration d3dc7f65 / 90f5a5fd. It includes:
  - native staging (rc56-rc66);
  - the copy-escape proof (rc68);
  - batched IA statistics (rc67);
  - the 50 ms completion busy-poll (rc72);
  - the GPU indirect stream as the default (rc69 params, rc71 VA release and scratch, rc63 faulted-scheduler TDR);
  - the written-BO cap at 1024.
- Best results:
  - D3D12 HD 50k **3873** (EXP1995);
  - D3D12 FHD 100k **2372** (EXP1993);
  - D3D11 HD 10k 2515 (no regression).
- Bound: GPU timeline. FHD has 0.4 ms of idle in a 63.7 ms frame (`tools/agx_gpu_frame_breakdown.py`).
- Largest GPU costs: standalone compute (24 ms FHD), main pass (15 ms), render-compute pre-3D (13 ms).
- Next causal target: shader cost (rc74). The rc73 barrier elision is rejected: GravityMark barriers after almost every dispatch.

## 2026-10-28 GravityMark D3D12 state (EXP2023)
- Accepted line: integration 6ab1417a / e232def4. It includes:
  - stream default plus rc89 output capacity;
  - soft faults by default (rc82, fault_control 0xb);
  - rc74/75/78 shader cost;
  - rc76 clear absorb;
  - rc80 TVB growth;
  - rc81/83 KMD job path;
  - rc85-88 startup and memory;
  - rc87 texture-only heap deferral.
- Best results:

  | Setting | Score | macOS reference |
  |---|---|---|
  | HD 50k | **6783** (EXP2021) | — |
  | HD 100k | **4984** (EXP2020) | ~9950 |
  | FHD 100k | **3919** (EXP2019) | ~7700 |
  | FHD 200k | **2951** (EXP2023) | user's own macOS on this J313: 2191..4400; leaderboard ~5500 |

  This J313 has a 7-core GPU.
- The score counts frames in a fixed ~167 s window, so the first-frame stall and memory thrash count against it.
- Remaining known issues:
  - driver-built CDM control-stream faults still bugcheck 0x116 (EXP2005/2007); app data faults are silent under soft faults;
  - the FHD working set is close to the about 2.9 GiB LOCAL budget (50% of 7.2 GB guest RAM).
- Next targets:
  - per-frame GPU time: lighting kernel bdb1944a, main pass;
  - per-job overhead (depth-2 job window);
  - the remaining 9-10 s stall (app work, page allocation).

## 2026-11-06T00:53:08Z -- CS 1.6 / OpenGL regression (active), bench lent to the SSD work
- Proven: the 5 fps was software GL; the packaged WOW64 ICD was the pinned 2026-10-16 out61 binary (EXP2204). Every package now builds the ICD from its own source (EXP2205, CHG ...-ICDBUILD); CS renders through the AGX ICD again: 18.634 fps vs EXP1378 44.784.
- Frame budget (EXP2205-B receipts, offline): present ~25.6 ms (DXGI blit wait ~5.5), game-thread CPU ~14 ms, KMT waits ~4 ms, scene ACQUIRE stalls ~5.6 ms (629 escapes >= 10 ms, median 36 ms).
- Hypotheses, ranked (from source): (1) Q2c default (e28304ae): the ICD's present blit and GL texture uploads take asahi_compute_blit, so GL frames are R/C-interleaved and every C now crosses to the compute queue with a cross-queue wait plus depth-2 kicks; at efa17240 one queue. (2) G3 private scene ACQUIRE: rc80 TVB growth waits up to 50 ms for idle on a cached hit (5d6cdd44), or foreign PRESSURE eviction by DWM (0c458548, bc72e1f5).
- Next hardware (when the bench returns), one variable each: CS on the default package with Wom1JobDiagnostics=1 + Wom1PresentTimestamps=1, receipts captured right after hl.exe exits (job spans, PrivateStats HIT/MISS/TRIM/PRESSURE, per-process TVB Grows/Failures/LastReason); then Wom1ComputeOwnQueue=0 alone; then Wom1JobDepth2=0 alone only if needed.
- Also open: KMD preemption-refusal engine reset (scheduler_windows.c:537, EXP2183/EXP2203; proposed as its own task); OV5 KMD drill (096c0cac) not yet on hardware.

## 2026-11-06T02:29:01Z -- CS 1.6 regression: hypothesis revised after the EXP2206 decode (OV2 session, offline)
- Q2c is ruled out: JobTiming971Hot holds only renders from hl.exe (0 compute jobs, ~25 serial renders per frame). The rc80 TVB growth wait and foreign PRESSURE eviction are ruled out: RELEASE escapes are as slow as ACQUIRE; PRESSURE 0.
- Active hypothesis: the adapter-wide G3 state lock is held across UPDATE_PAGE_TABLE broker work (paging.c:2055: 162k builds, 113 s held, max hold 68.7 ms, ~700 us average, broker UPDATE_LEAF/REVOKE/REGISTER millions of calls). That work is driven by DWM's present-resource churn (2600 creates of 2560x1600 BGRA8 surfaces, 16 MB UPT each), and it stalls hl.exe's private escapes (median 26 ms) and CompleteJobTimestamp (job completion). Counts are cumulative since the first export, so the CS-only share is still unproven.
- Next hardware (single variable none, receipt-only): EXP2207 = EXP2206 setup with KMD receipts at ~t=60 s and ~t=160 s during CS plus after exit; the difference between the two in-run captures gives the CS-only lock/paging cost per frame, and PrivateProcs is captured with hl.exe alive. If it confirms the picture, the fix belongs in the KMD G3 paging layer (do not hold the adapter-wide lock across per-PTE broker calls; the earlier per-process lock attempt 32d363ef4 was reverted in 6bcae8318 -- its reason must be read first).

## 2026-11-06T03:34:24Z -- CS 1.6 / OpenGL regression CLOSED (EXP2209)
- Root causes, both fixed: (1) stale packaged WOW64 ICD (software GL, ~5 fps) -> ICD built per package (EXP2205); (2) DWM refused batches on a read-only GDI redirection surface (NativeQuery READ_ONLY) -> device re-creation loop -> G3 page-table churn under the adapter lock (18.6 fps) -> 025ab1d6 (EXP2209: 45.457 fps, EXP1378 44.784).
- Parked: G3 lock split design (not on the critical path now). Open: KMD preemption-refusal engine reset (proposed task), OV5 KMD drill on hardware, GravityMark FHD gap (5396 vs 7814).

UPDATE 2026-11-08T13:52:10Z EXP2271 SUBMIT BRACKET VERDICT: the doorbell-only pending_submissions + KICKFW bracket without atomics is stable (suite 119/0) but gives no gain (GM HD 44.396 vs 44.435; short-burst P1 69.6 % vs 64.7 %). Wom1SubmitBracket stays default off. Never use atomics on the write-combined firmware mappings (EXP2270 reset). Next causal target for GM: compute cost at equal clock against .local/metal-oracle.

UPDATE 2026-11-08T13:59:54Z GPUVA-IN-KMD SCOPE CHECK (no hardware, EXP2271 full.log BROKERLOCK): in the GM steady windows (identified by scanout ~5300 calls/10 s, 8 windows) v5 = 2071-3797 calls/10 s, lock hold 4.0-7.3 ms per 10 s, i.e. ~0.015 ms per frame of the ~4 ms HD structural gap. The inventory's LEASE 160-272 us x2/frame (EXP2163) predates retained slots. v5 bursts (up to 381k calls/10 s, hold 0.6-1.08 s/10 s) occur only outside those windows (load/suite). Decision: GPUVA v5 -> KMD is NOT a GM lever; it stays a production-architecture item (load-time cost, no EL2 in the data path), deferred. Blocking unknown if it resumes: whether TLBI ASIDE1OS from guest EL1 reaches GPU translations (no experiment has issued it). Next GM target: the non-work items late 1.40, launch 1.28, foreign 0.99, detect 0.81 ms and R||C overlap.

UPDATE 2026-11-08T14:58:18Z OVERLAP LINE, WHERE IT STANDS (ledger read, no new run; investigation/analysis/OV1-render-compute-overlap.md "Next" is obsolete): R->C (OV2/OV3b) is closed as the wrong direction: OV3b is correct but the waiver fires on 2 of 54634 pairs (EXP2198), ranges free only C9 (EXP2200), and EXP2201 shows Apple's overlap is a render starting during an independent compute (C5->R6, C7->R8). OV5 is the right direction: only R8 was eligible (EXP2210; R2/R4/R6 refused reason 9, CDM prelude), and waiving its TA wait hangs the GPU (EXP2214, 0x116). So the open items are (1) a bounded reproducer of the EXP2214 hang, worth <= ~0.65 ms HD by itself, and (2) the CDM prelude refusal on R2/R4/R6, without which the ~2 ms overlap estimate is not reachable.

UPDATE 2026-11-08T15:31:48Z CDM PRELUDE CAUSE (offline, measure-perf-reason decoded from EXP2271 gm2271d-umd.log, 3437 presents): "Input assembly counters" (Asahi agx_ia_update) has 0 rows, so pipeline statistics are not the prelude source; that hypothesis is rejected. Rows that fit: GPU stream scratch reuse 15.07/present, GPU stream capacity below MaxCount 7.54/present, Flush writer due to Indirect stream arguments 2.16/present (agx_win32_indirect_stream_emit.h:186). The prelude is most likely the ExecuteIndirect GPU-stream expansion (agx_d3d12_windows.cpp:5262 generates streams before rendering), whose arguments come from the preceding compute: a real data dependency, not removable by a waiver. Not yet proven per render (R2/R4/R6). Consequence: the ~2 ms overlap estimate should not be treated as reachable through OV5; only prelude-free renders (R8) qualify, and those hang (EXP2214).

UPDATE 2026-11-08T16:19:16Z CORRECTION TO "CDM PRELUDE CAUSE" ABOVE: "a real data dependency, not removable" is not supported for R6. EXP2201: Metal tracks all 162 resources automatically and still starts R16 (our R6 by position) while c15 (our C5) runs, so R16 has no tracked hazard on c15 beyond what Apple's overlap allows; the args of our R6 then need not come from C5. Our prelude is CDM work queued behind C5 on the compute channel, which would serialize R6 behind C5 even without a data dependency. Measured Apple overlap ceiling is HD 0.95 + 0.72 = 1.67 ms, FHD 1.95 + 1.45 = 3.40 ms (c15->R16, c19->R20). Open: per render, does the ExecuteIndirect args writer equal the immediately preceding compute. If not, the lever is expanding R6's stream ahead of C5 (compare EXP2195 EXPAND_AHEAD, rejected as a default for other reasons).

UPDATE 2026-11-08T17:16:03Z EXP2274 OV5 NO-PRELUDE RECEIPT: R2/R4 reason 11, R6 reason 10, R8 0. Inconclusive for R2/R4/R6 (unplaced accesses; open-ended ranges make 10 an upper bound). Overlap line parked: measured ceiling 1.67 ms HD, needs exact ranges plus the EXP2214 hang explained. Next HV item: count ICC/LR trap cost per interrupt (peer EXP2272/2273: ~1.5 % GM loss with ~40k line-887 interrupts/s).

UPDATE 2026-11-08T17:20:29Z LINE 887 TRAP COUNT (offline, medians of HV: EXITS per 10 s in GM windows, no new run): EXP2271 (no 887) icc 190838 irq 830 fiq 117574 impdef 1049009; EXP2272 icc 226087 irq 6969; EXP2273 icc 233475 irq 5473. The line adds about 35-43k ICC traps and 5-6k IRQ exits per 10 s (about 4k ICC/s, 0.6k IRQ/s), not ~40k interrupts/s. Per-exit cost is not measured, but at this count ICC traps are an unlikely explanation for the ~1.5 % GM loss; that loss (44.0/43.8 vs 44.4-44.6) is also close to run noise (EXP2266 44.08). Largest exit class remains impdef (PMCCNTR), about 100k/s on the unlocked path.

UPDATE 2026-11-08T21:43:33Z NO CRASH DUMPS ON NATIVE SSD: AppleAns refuses crash-dump and hibernation instances by design (1e7a690c4), so a bugcheck on the ANS-DIRECT profile leaves no MEMORY.DMP, minidump or event 1001 (seen in EXP2270, EXP2283). Evidence for such resets is full.log (PSCI RESET lines), receipts from the crashed boot (rec*/kmd-receipts-pre-disable.txt), and periodic receipt snapshots. This supersedes the "hypothesis" wording in EXP2283 ACTUAL.

## UPDATE 2026-11-08T22:41:28Z -- the waiver-profile reset is at GravityMark process start, waiver not involved
EXP2283 run b and EXP2285 run a both reset within ~1 s of GM's owner creation (UMD log 5 lines, stdout ends at "Using Fetch
Mode"), with Waived 0 / Placed 0 and only CLASS refusals in the receipt (Considered 677). The reproducible fact is "diagnostics
off + {Wom1Ov2NoHazardLead, Wom1Ov5TaWaiver} resets 2 of 3 GM starts on the current package"; every run EXP2265-EXP2284 had
diagnostics on, the last diagnostics-off GM boots (EXP2250-2264, 15, no switches, older package) never reset. Active
hypothesis replaced: not a race in the waived path but a process-start fault under the faster worker, switch or base
unknown. Discriminators queued after EXP2286 (stop code via m1n1 dfe621ea): EXP2287 diag off + Ov2 lead only, 3 GM starts;
EXP2288 diag off, no switches, 3 starts. ABBA/default waiver still blocked. Details: EXPERIMENTS.md "EXP2285 OFFLINE READING".

## UPDATE 2026-11-08T22:55:30Z -- EXP2286 stop code 0x116: firmware did not retire a job at GM start; TDR quiesce failed (0xC0000483)
Not a KMD fault. Waived 0, Ov2 0 at the stall; receipts absent because the worker publishes them after completions.
Three diag-off GM starts reset, one survived; diag on 0 of 3. EXP2214's verdict (waiver fault, Waived 3 + fault receipt)
stands but shares the diag-off/GM-start profile. Next: EXP2287 (diag off, OV2 lead only, 3 starts), EXP2288 (no switches),
D4 = m1n1 prints ASC RX pending at the PSCI trap (worker stuck vs firmware stall). Ledger: "EXP2286 ARCHITECT READING".

## UPDATE 2026-11-08T23:54:14Z -- EXP2288: the waiver SWITCH is necessary for the 0x116 (3/3 on, 0/3 off), but no waiver fires before it
Death at 10-15 s after launch = GravityMark scene creation, before "Ready" (13-17 s). In the healthy diag-on run no render
is flagged TA-independent before Ready (first at 21.7 s), so Waived 0 at the reset is real. Switch effects left: KMD-internal
(per-kick refusal, receipt notes -> worker registry exports) vs the UMD feature bit (hook call only). Next EXP2289: waiver on,
diag off, feature bit withheld (Wom1Ov5TaWaiverNoFeature=1), 3 starts, with D4 in m1n1. ABBA/default waiver still blocked.

## UPDATE 2026-11-09T00:39:56Z -- EXP2289: the UMD feature bit is necessary; the GM-start pair is "process's first compute -> loading-screen UI render"
Bit published 3/3 resets, withheld 0/3, switch off 0/3 (diag off). In the healthy log the only waivable pair before the
benchmark is the UI render at 18.30 s behind the process's first compute batch (18.24 s), verdict 0. Diag off lands the
kick inside that short compute; diag on misses it. Next: EXP2290 = EXP2286 config + waiver-only beacon in the broker page
(fence, compute WaitValue/Event, StampSelf, stamp PA) printed by m1n1 at the PSCI trap with the live stamp and ASC RX
pending. Then fix (a): no waiver behind a process's first compute, if the beacon names that pair.

## UPDATE 2026-11-09T01:40:46Z -- EXP2291: beacon (tens of us before the waived doorbell) removes the stall: a kick-timing race
6773 waived pairs, 0 resets, vs 3/3 without the beacon. H3: the waived successor's doorbell reaches the firmware before
the running compute has started (start-up pair: render already pending at the compute's kick). EXP2292 = defer the waived
kick until the compute's firmware start timestamp is nonzero (one variable, beacon off), 3 GM starts; then the ABBA.

## UPDATE 2026-11-09T02:17:10Z -- EXP2292: deferral premise confirmed (Deferred 1 per start, compute not started); placement after the pending claim broke the start (3 ENGINE resets)
Fix placement: decide before AdmissionG4PendingKickLocked, under the locks, return FALSE with nothing mutated. EXP2293 reruns.

## UPDATE 2026-11-09T02:52:12Z -- EXP2293: compute-start gate holds (3/3 clean, Waived 7436); waiver profile stable with diagnostics off
Next EXP2294 ABBA (one variable: waiver on top of Ov2 lead; diag off; warm-up + 2 measured runs per boot).

## UPDATE 2026-11-09T03:50:48Z -- NEW DEFECT: every restart after a GPU session bugchecks (0xEF / 0x1A 0x6001 C000000E / 0x154), seen via the D2 print; clean after staging
Independent of OV switches. Suspects: KMD shutdown teardown with live user mappings (native staging rings, pool MDLs) vs ANS stopping before paging ends. Separate task proposed; ABBA (EXP2294/2295 first pair +0.02 %, inside noise) continues.

UPDATE 2026-11-09T04:44:43Z OV5 WAIVER A/B/B/A VERDICT (EXP2294-2297): no frame-rate gain in HD (+0.02 %, -0.28 %; 43.25 on vs 43.30 off). Wom1Ov5TaWaiver and COMPUTE_FIRST stay default off. Separate open defect: bugcheck at ordered reboot after a GPU session (0x154/0xEF/0x1A), reported by m1n1's KiBugCheckData print.

## UPDATE 2026-11-09T04:45:44Z -- OV5 WAIVER LINE CLOSED: stable (compute-start gate), no gain (ABBA EXP2294-2297 +0.02 % / -0.28 %), KEEP-OFF
The R||C overlap via R8 is real but tiny (5 % of pairs, 0.54 ms median). The ~2.0 ms "missing overlap" of the structural
decomposition cannot come from R8; R2/R4/R6 carry true VB-read vs UAV-write dependencies (OV6b). Next causal question
(architect, offline): where in Apple's timeline the overlap sits -- cross-frame (C(N+1) || R(N)) or intra-frame -- using the
scene-aligned job timelines (agx_gm_scene_align.py, agx_gpu_frame_breakdown.py --jobs) before any further OV work.
Open defect in its own task: shutdown bugcheck after GPU sessions (0xEF/0x1A/0x154; EXP2288-2297), see the proposal card.

## UPDATE 2026-11-09T04:50:04Z -- APPLE'S 2 ms OVERLAP LOCATED: C4||R1 (1.46 ms) and C6||R3 (0.75 ms), intra-frame, render starts first
From splitd-hd.csv directly (sum 21.41 vs union 18.52 ms/frame; compute||render 2.03, cross-frame 0.31). Not the R8 waiver.
Metal order C0 R5 C2 R7 C4 R1 C6 R3 vs our D3D12 order C0 R1 C2 R3 C4 R5 C6 R7. Next (offline): do our OV2 receipts judge
C2-behind-R1 and C4-behind-R3 independent? If the tracker refuses them for a reason Apple's overlap disproves, that is the
next lever (compute kicked behind a running independent render: COMPUTE_ON_3D + OV2 lead). Ledger: "APPLE OVERLAP STRUCTURE".

## UPDATE 2026-11-09T04:54:07Z -- NEXT LEVER: OV8 full independence (render ahead of the compute on both stages), spec written
Apple's pairs in our names: C4||R5 1.46 ms, C6||R7 0.75 ms (correction of the previous update's names). Our blockers are
ours: R5's prelude rule + unsized-VB conflict, R7's 3D wait on C6. investigation/analysis/OV8-full-independence.md:
step 1 = receipt-only GM run (full-range / barrier / prelude verdicts per render), step 2 = opt-in both-leads waiver + ABBA.

## UPDATE 2026-11-09T05:46:18Z -- OVERLAP LINE CLOSED FOR GRAVITYMARK: the D3D12 port orders C4->R5 (UAV barrier) and C6->R7 (UAV->PS_SRV transition) itself
EXP2298/2299 receipts. Apple's 2.2 ms is the Metal port's structure, not available under the D3D12 contract. Remaining
recoverable HD gap ~1.5-2.0 ms of 21.6: DWM windowed composition 0.85 (independent flip/MPO, DISPLAY work), launch 0.45
(HV LEASE / worker), detect 0.3-0.5 (Wom1IrqCompletion proven: FPS neutral, frees a core -> default-on candidate via the
perf gate), idle 0.22, C4 kernel +0.3 (workload). Next causal targets in that order; no OV hardware runs.

## UPDATE 2026-11-09T06:19:02Z -- shutdown bugcheck: probable cause in the AppleAns storport miniport (fork 6, offline)
The first SRB_FUNCTION_SHUTDOWN set ShuttingDown and stopped the controller from a DPC; later paging I/O got
SRB_STATUS_NO_DEVICE -> STATUS_NO_SUCH_DEVICE (= p2 of the 0x1A), hence 0x1A/0xEF/0x154 only after sessions with paged-out
memory. Fix under test: SHUTDOWN = ordered cache flush only, the controller stops in ScsiStopAdapter / the power-down path.
Verification: GM runs + shutdown /r -> m1n1 prints BUGCHECK code=0x0. Not a GPU defect.

UPDATE 2026-11-09T06:48:53Z RESTART BUGCHECK FIXED (AppleAns 0.4.2, 1f7129fd; EXP2300 code=0x0 after three GM runs). Cause: the first SRB_FUNCTION_SHUTDOWN stopped the controller, later paging I/O got STATUS_NO_SUCH_DEVICE. Installed on the bench C:; ANSBUILD011-I stays staged as rollback.

## UPDATE 2026-11-09T19:32:40Z -- NEXT TARGETS: IRQ COMPLETION DEFAULT-ON (SPEC) AND DWM ROW DISCRIMINATOR F0 (architect, offline)
1. Wom1IrqCompletion default-on: spec in investigation/analysis/KMD-IRQ-COMPLETION.md ("Default-on plan"). Blocking source
finding H1 (hypothesis, never observed): the level mailbox line is not dismissed when the ISR returns early (not ready, not
armed, unmapped) or the 64-slot ring is full -> storm risk once the line is in a default Mu. Order: harden ISR + test,
IRQD1 (inert on MUANSCPPC002), IRQD2 (candidate Mu with the line, same-boot ABBA), then promote the Mu. Recovery Mu unchanged.
2. DWM row (0.85 ms): EXP2273 hot ring, 7/7 frames: DWM job submitted +0.06 ms after GravityMark's render completes, kicked
+0.32, complete +0.85, GravityMark compute (submitted +0.21) kicked +0.96; refusal 55 on every DWM job. DWM cannot be
pre-bound behind the render it depends on; only its neighbours can overlap it. Note: investigation/analysis/DWM-COMPOSITION-LEVER.md.
WHY CONTINUE COMPARISON: not applicable (no archaeology). ACTIVE HYPOTHESIS: exclusive fullscreen removes the DWM job and
returns 2.5-4 % at HD. NEXT CAUSAL TARGET: F0 = windowed vs fullscreen same-boot ABBA + ETW attribution, default package,
no code. F0 outcome selects: cross-process depth-2 pairs (C1 then C2a), display direct-flip fix, or dropping the row.

## UPDATE 2026-11-09T20:00:21Z -- TDR RECOVERY DESIGNED (NEXT TARGET H1), MU CANDIDATE FOR IRQD2 = MUIRQ001 (architect, offline)

- TDR timeout-only recovery: design in investigation/analysis/TDR-FIRMWARE-RESTART-RECOVERY.md (7bedfb1fa).
  Active hypothesis U1: the iBoot-loaded GFX image boots a second time after an orderly stop (AP 0x10, IOP
  0x10, CPU RUN off/on) with power and the retained root untouched. Never tested (EXP589 did not run). Next
  causal target: stage 2 restart mechanism (off by default), then hardware H1 = idle warm restart, three
  cycles in one boot. No TDR DDI wiring before H1.
- IRQ completion default-on: Mu candidate is MUIRQ001-95263b97 (FD 0fa3cea5...), gate clean on the J313ABI
  SSDT (the GPU device is not in the DSDT). KMD change on refs/heads/claude/irq-default-on-24672b (in
  progress, merge needs the user's word); fork 6's parked patch is the cross-check. Then IRQD1, IRQD2.
- Bench: EXP2301 F0 (DWM row discriminator) running since 19:37:58Z.

## UPDATE 2026-11-09T20:28:57Z -- F0 VERDICT (EXP2301): LEVER UNMEASURED, DISPLAY-SIDE CAUSES FOUND OFFLINE (architect)

- EXP2301 observation: DWM composes GravityMark in both arms (RedirectedFlip, one dwm Flip per present). The
  "fullscreen" arm was a failed mode set (ChangeDisplaySettingsExW failed; 2560x1600 swap chain, 1280x720 render),
  so windowed 43.77 vs 42.89 fps is not the specified comparison. Correction appended to the ledger (EXP2301
  ARCHITECT VERDICT AND CORRECTION).
- Proven boundary (sources + receipt): bench m1n1 83a9cf74 has no CAP_SCALED_SOURCE (BrokerCaps 0xff, ScaledModes 0,
  1 of 11 modes) -> class C regression, the scaled source exists only on codex/m1n1-scaled (66bf65c1, 8209e527,
  74a4d8f4). Direct flip: UMD CheckDirectFlipSupport = FALSE by design (agx_wddm2_ddi.cpp:677), back buffers not
  scanout-capable (EXP1038).
- Rejected premise: "exclusive fullscreen flips the application's primary" (note E6 / mechanism B) is unproven.
- ACTIVE HYPOTHESIS: none on hardware. NEXT CAUSAL TARGET: (1) port the scaled source to the bench m1n1 line and
  verify 11 modes + a real 1280x720 GravityMark mode (one variable = m1n1, recovery 83a9cf74); (2) offline
  direct-flip contract (Microsoft docs + pinned WDK) -> spec; (3) F0 again with equal present sizes. C1/C2a parked.

## UPDATE 2026-11-09T20:53:07Z -- ISR default-on and TDR stage 2a merged (no hardware)

- `gpu/memory-latency` now contains 212f09e0e (KMD mailbox ISR, `Wom1IrqCompletion` default on, inert without the
  ACPI line), fe119f211 (shared-layer warm firmware restart entry points, called by nothing) and 84b91a7cb
  (RuntimeStart arms the runtime before publishing it to the ISR).
- Verified offline only: touched suites pass; `run_against_baseline.py` 1091 tests, 0 NEW when
  `Q22_M1N1_SOURCE=/Volumes/pwdev/public_windows/.worktrees/m1n1-q22` (without it three `test_g4_live_*` tests
  fail against the `m1n1_windows` checkout 0d3a75b3, which predates 7cce5a0e). Not MSVC-compiled, not on hardware.
- Accepted package: unchanged (the EXP2300 package). Production Mu MUANSCPPC002; candidate MUIRQ001-95263b97.
- NEXT CAUSAL TARGETS (bench serial, fork 6): (1) package build of the merged tree = /W4 compile gate;
  (2) IRQD1 on MUANSCPPC002: behaviour identical, perf gate PASS, suite 119/0, `Wom1IrqRx` absent;
  (3) IRQD2 on MUIRQ001: same-boot ABBA on `Wom1IrqCompletion`, Discarded/RingDrops/Overflow 0, 3 disable/enable
  cycles, ordered restart `BUGCHECK ok code=0x0`; (4) scaled-source m1n1 checkpoint, then F0 again.
- Architect, offline: TDR stage 2b spec/implementation and receipt v5 (design note section 9.4); direct-flip
  contract note (`investigation/analysis/DIRECT-FLIP-CONTRACT.md`).

## UPDATE 2026-11-09T21:00:41Z -- direct-flip contract written (no hardware)

- investigation/analysis/DIRECT-FLIP-CONTRACT.md (FULL GRAPHICS). D3/D4 of DWM-COMPOSITION-LEVER.md section 7
  corrected: the KMD already scans an RGBA8 or BGRA8 linear application buffer with the committed geometry
  (rc106/rc110). The only gate for a full-screen application buffer is the Mesa D3D11 CheckDirectFlipSupport
  answer (FALSE), plus D1 for sizes other than 2560x1600.
- KMD SupportDirectFlip/FlipIndependent are truthful; no segment carries DXGK_SEGMENTFLAGS.DirectFlip (atomic
  companion of a truthful UMD answer, not changed alone).
- Next causal target unchanged in order: D1 checkpoint -> F0 repeat at a real 1280x720 mode (no package). The F0
  repeat decides: full screen already bypasses DWM (measure only) / still RedirectedFlip (build the atomic P1
  package: segment flag + UMD rule) / mode set fails (D1 not fixed).
- Windowed GravityMark (the 0.85 ms DWM row) needs a second DCP layer + CheckMultiPlaneOverlaySupport3 (P2),
  parked until P1 is measured.

## UPDATE 2026-11-09T22:03:55Z -- IRQD1 confirmed (EXP2303); TDR stage 2b implemented offline on a side branch

- **IRQD1 verdict: CONFIRMED (EXP2303, package from 6a53a8646, MSVC-built).** On the unchanged platform
  (Mu MUANSCPPC002, m1n1 83a9cf74) the ISR-default-on package with the TDR stage 2a shared layer is inert:
  CM_PROB_NONE, suite 119/0, perf gate PASS (CS 1.6 warm 53.168/52.738 vs baseline 49.943; GravityMark HD
  PERF worst 44.154 vs 40.770), recovery rec2303h done. This is also the first MSVC build and hardware pass
  of the merged ISR + stage 2a tree.
- **IRQD2 is in flight as EXP2306** (fork 6, Mu MUIRQ001 with the mailbox line, same-boot A/B/B/A on
  Wom1IrqCompletion). Promotion rule unchanged: Mu becomes MUIRQ001 only on PASS; on any stop the package
  default returns to off and Mu stays MUANSCPPC002.
- **TDR stage 2b + receipt v5** exist on `refs/heads/claude/tdr-stage-2b` (b0587f169, fa4d7cf17, 08b519773 on
  dcdce59d4; not merged, not pushed, no MSVC build, no hardware). Architect check: the baseline gate on its
  tip in a checkout with the full environment (`.local`, `Q22_M1N1_SOURCE`) runs 1105 tests with 0 genuine
  NEW failures (the one reported NEW is the absent `mu` checkout of the temporary worktree; the session's
  "17 failures" were the same kind of environment gap). Sequence reviewed against sections 8-10 of
  TDR-FIRMWARE-RESTART-RECOVERY.md: no ACTIVATE/PREPARE on the warm publish, ISR closed only after the proven
  stop and reopened before the CPU start, only caller is the drill escape behind `Wom1FwRestartDrillAllowed`.
- **Not purely additive:** the branch also splits `AdmissionRetainedActivate` on the cold path, widens the
  backend-stop condition of `AdmissionPlatformDestroy`, and grows `Wom1GpuReset` from 792 to 1064 bytes. So
  after a merge the order is: MSVC build -> inert checkpoint (drill value absent: suite + perf gate, as IRQD1)
  -> only then H1 (idle warm restart, three cycles).
- **H1 BEFORE must name:** (1) a stopped ASC may keep outbox messages that the reopened ISR hands to the new
  boot (control words recorded, nothing drained); (2) submitters wait out the restart, so `TotalMs` against
  the 2 s TDR budget is an observation; (3) the render shared-memory owner's lifetime words rely on the first
  job's InitializeQueues plan; (4) retained VM-slot leases stay leased (T5 not implemented).
- **Next causal target:** unchanged for performance (IRQD2 -> scaled-source checkpoint -> F0 repeat at real
  1280x720); for recovery: merge decision on stage 2b (the user's), then the two-step hardware order above.

## UPDATE 2026-11-09T23:02:11Z -- TDR stage 2b merged (e6fb8736e); IRQD2 interrupted by an m1n1 defect (EXP2306), fix under test

- **Stage 2b is on gpu/memory-latency** (merge e6fb8736e of claude/tdr-stage-2b on 06b4fdcf4; the user's decision).
  Host gate on the merged tree: 1105 tests, 0 NEW (BASELINE_OK). Still no MSVC build and no hardware. The warm
  restart has one caller, the drill escape behind `Wom1FwRestartDrillAllowed`; nothing is wired into the TDR DDIs.
- **Recovery order (fork 6, behind the IRQ runs):** MSVC package + drill tool build -> inert checkpoint (drill
  value absent; must equal EXP2303, receipt decodes as v5 with no restart) -> H1 = idle warm restart, three
  cycles, on Mu MUANSCPPC002 first. Without the mailbox line H1 does not exercise the ISR close/reopen; that is
  a separate later run on MUIRQ001. H1 answers U1 (does the iBoot-loaded GFX image boot a second time).
  Staged in .local/handoff/OV-ARCHITECT-HANDOFF.md (pass criteria from the decoded `firmware_restart` record).
- **EXP2306 (IRQD2): INCONCLUSIVE for the Mu promotion, arm A1 valid.** With the line listed and the default on:
  107645 ISR messages, RingDrops/Discarded/Overflow 0, job-poll busy fraction 0.0206, perf gate PASS. The run
  ended at the first live `pnputil /disable-device`: EL2 SYNC abort in m1n1 (`backing_find` on the broker that
  AGX_RR_CLOSE had just zeroed). Owner layer m1n1, independent of the Mu image by source. MUIRQ001 is not
  promoted: stop/start with the line connected is still unmeasured.
- **EXP2307 part 1:** m1n1 e6ac18bb (06616be7, closed-broker lookup returns NULL) passed one live disable with no
  EL2 exception. The following enable failed closed by design: StartDevice consumes the one-shot `G3Armed`
  value and the toggle script did not re-arm (probe defect). EXP2307B (same boot, re-arm between disable and
  enable) is in flight; it is the first second RR_OPEN + firmware start in one boot on this bench.
- **Production gap named by EXP2307:** a live disable/enable (Device Manager, driver update without reboot)
  ends in Code 43 because every StartDevice needs a freshly written one-shot arm. Acceptable for qualification,
  not for a production driver; the arm gate needs a replacement contract (architect, offline; no bench need).
- **Next causal target:** performance unchanged (EXP2307B -> IRQD2 rerun on MUIRQ001 with m1n1 e6ac18bb if
  confirmed -> scaled-source checkpoint, which needs 06616be7 on its m1n1 line before any adapter disable ->
  F0 repeat at real 1280x720). Recovery: the three-step order above.

## UPDATE 2026-11-09T23:40:00Z -- night plan: the GravityMark gap vs macOS becomes the only performance target

- **User order:** approach macOS as closely as possible this night (FULL scores: FHD 69 %, HD 68 %; HD PERF ~74 %);
  the architect decides and starts work without asking. Bench order: GM-gap experiments first; H1/TDR drills, IRQD2
  and ANS-S1 only when the bench would otherwise idle.
- **Why the plan changes:** the FHD frame was never decomposed although its gap is larger (ours ~31.0 ms vs Apple
  21.4 ms; HD ~22.5 vs 17.4). The closed same-frame overlap line (EXP2298/2299) and the known HD overhead rows
  (~2.0 ms) together reach only ~86-89 % of macOS at HD, so a cause outside those rows must exist or the job
  structure itself must change.
- **Open hypotheses, ranked (all labelled HYPOTHESIS until a report cites evidence):**
  H-A FHD-only cost on the render/present side (load/store, compression, partial renders, DWM copy size);
  H-B dxgkrnl queue contract / late submission owns launch + detect + idle + the wait behind DWM;
  H-C a legal overlap we do not use (cross-process DWM||compute; cross-frame or next-compute-behind-render pairs);
  H-D GPU runs below its maximum (P6 residency 86-87 %, firmware power inputs), or mapping granularity;
  H-E macOS queues the frame ahead in firmware and has no compositor job on the critical path.
- **Running offline (read-only subagents, reports in /Users/pavel/public_windows/.local/night-20261110/):**
  r1-fhd-budget, r2-dxgk-queue, r3-dag-bound, r4-compute-levers, r5-macos-submission, r6-render-scaling.
- **Decision (architect):** mechanism C of DWM-COMPOSITION-LEVER.md is unparked. C1 (class I, opt-in
  `Wom1CrossProcessPairs`) and C2a (class O) are being implemented offline in a separate worktree session; F0 is
  not a precondition any more because the cross-process boundary is also the first step towards per-process
  firmware queues (the Asahi structure), whatever the size of the DWM row.
- **Bench item N1 (fork 6):** GM-NIGHT-BASE, measurement only: FHD and HD with the job-timing ring, PERF runs,
  mid-run pstate residency. Staged in .local/handoff/OV-ARCHITECT-HANDOFF.md (2026-11-09T23:35Z).
- **Next causal target:** the largest FHD row by (ours - Apple) from r1 + N1; one lever per experiment after it.

## UPDATE 2026-11-09T23:49:25Z -- full-screen at other resolutions: owner order and staged checkpoint FS-D1

User order: full-screen mode at different resolutions must work as on an ordinary Windows PC. Priority stays below
the GravityMark gap (the user repeated that the windowed macOS score is already ~30 % above ours, so the windowed
comparison is the target; full-screen is not the explanation of the gap).

Proven boundary (EXP2301): the bench m1n1 line has no CAP_SCALED_SOURCE (BrokerCaps 0xff, ScaledModes 0), the KMD
exposes 1 of 11 modes, ChangeDisplaySettingsExW fails. Nothing above that boundary was ever exercised on the
ANS-direct line.

Decision (owner-layer order, DISPLAY_ONLY first):
1. FS-D1: scaled source on the current bench m1n1 source (closedv5, with 06616be7). The old offline port
   (b87eda82..., base 83a9cf74) is superseded before any run because the bench moved to e6ac18bb; the m1n1 port
   session rebuilds it in `.local/experiments/m1n1-ansdirect-closedv5-scaled1/`. Checkpoint staged in the
   architect handoff (one variable = m1n1; mode list, every mode set and back, GravityMark full-screen at two
   sizes, adapter disable/enable in a scaled mode, perf gate). Not run yet; hash pending.
2. Windows-side audit (read-only agent, `.local/night-20261110/fs-windows-audit/REPORT.md`): VidPn scaling types,
   primaries at non-native sizes, DXGI blt-model exclusive and flip-model full-screen in the D3D11/D3D12 UMDs,
   GL ICD, pointer in scaled modes, mode restore; plus the specification of a full-screen conformance probe
   (`AGX_FS_<step>: PASS|FAIL`). Defects are fixed in the owning layer, one per experiment.
3. Direct flip P1 (segment DirectFlip flag + UMD CheckDirectFlipSupport rule, atomic companion) only after a real
   mode set exists; unchanged from DIRECT-FLIP-CONTRACT.md.

Next causal target for this line: FS-D1 receipt (BrokerCaps 0x1ff, ScaledModes 1, 11 modes) on hardware.

## UPDATE 2026-11-10T00:04:40Z -- GravityMark gap: offline budget from five reports (no hardware), hypotheses re-ranked

Sources (read-only agents, /Users/pavel/public_windows/.local/night-20261110/): r1-fhd-budget, r2-dxgk-queue,
r4-compute-levers, r5-macos-submission, r6-render-scaling. Reports are model output; rows below marked (checked)
were spot-checked by the architect against the cited tables, the rest are as reported.

- **Corrected size of the gap (r1, HD, PERF scene window, current profile EXP2307/2308):** ours 20.7 ms per
  GravityMark frame vs Apple 16.6 ms = 80 % of macOS, gap 4.1 ms = GPU work +1.8 (like-for-like ~+2.3) and
  non-busy +2.3. The "~74 %" figure came from counting present rows: at HD two 8-job frames can lie between two
  present rows (gate 44.39 fps, frames ~48.1 fps). Tool correction in progress (new frame-based field; gate metric
  unchanged until a baseline exists).
- **H-A (FHD-only cost) REJECTED as stated:** same boot/package (EXP2196a/b) ours grows HD->FHD +5.70 ms, Apple
  +4.75; FHD-only extra +0.95 ms. r6: render configuration at Asahi parity, 8 jobs CRCRCRCR at both sizes, 0
  partial renders, no extra copy. The earlier "+5 ms only at FHD" mixed the 11-05 profile (FHD) with the current
  one (HD). Current-profile FHD is unmeasured (N1).
- **H-B (dxgkrnl queue contract) REJECTED at HD (r2, checked):** dxgkrnl refills the cap-2 queue in 24 us median;
  per-frame pre-start gaps K 0.459 (firmware floor, Apple parity), F 1.16 (DWM job between GM jobs), D 0.111
  (late render completion behind a pre-kicked compute), U 0.002. HwQueuePacketCap 3 alone ~0: not to be run alone.
- **H-D (clock) mostly REJECTED (r4, r5):** same 1278 MHz P6; one reproducible dip per run (58-72 s, P6 59-80 %)
  worth 0.1-0.3 ms, explained as a consequence of GPU starvation in that scene (HYPOTHESIS).
- **H-E (frame-ahead queuing) TRUE on Apple's side, small for us:** Apple queues 1-2 frames ahead and starts the
  next frame's c0 before R20 ends: 0.27-0.33 ms HD, 0.6 ms FHD.
- **Apple "overlap" re-read (r5 + r6):** c15 lies inside r16 and c19 inside r20 because the render span includes
  its wait for the compute; our R5 TA is 0.2 ms, so running it under C4 gains <= 0.2 ms. The lighting stage
  C4+R5 vs r16 is still the largest job-level row (+0.66 HD current, +1.4..2.4 on the 11-05 profile HD..FHD);
  whether that is dependency structure or work is open (r3 pending).
- **Ranked open rows at HD (ms per frame, ours - Apple):** (1) GPU work across all jobs +1.8..2.3: C0 +0.51,
  R5 +0.48, R1/C2/R3 +0.22 each, C4 +0.18 -- three jobs of different size with the same +0.22 suggest a fixed
  per-job cost (HYPOTHESIS; r8-work-deficit running); (2) DWM job in front of a GM job 0.86 (current) .. 1.16
  (r7-dwm-row running: cheaper DWM job vs windowed overlay plane; mechanism C covers only the launch part);
  (3) own launch + late submit + unattributed ~1.2 of which ~0.45 is firmware floor; (4) cross-frame overlap 0.3.
- **Also found:** hitch frames > 45 ms (3..25 per run, 205..1770 ms per run, not explained by switches; r4);
  after a second DxgkDdiStartDevice in one boot dxgkrnl fails AddAdapter with 0xC0000017 (EXP2308 toggles,
  event 549, Problem 43): same-boot adapter toggles are still invalid, so diagnostics values must be set before
  first start (recorded as a separate class C item, not chased tonight).
- **Next causal target:** N1/EXP2309 on a fresh boot (FHD+HD, per-job rows, pstate mid-run) to confirm the budget
  on the current profile; r8 decides fixed-vs-proportional for row (1); r3 and r7 decide rows (1)/(2) designs.

## UPDATE 2026-11-10T00:11:39Z -- H-C REJECTED: Apple's compute||render overlap is waiting, not work; top row = uniform GPU work deficit
- Source: .local/night-20261110/r3-dag-bound/REPORT.md (offline; spot-checked: apple_split_enc_hd.txt, apple_wait_vs_work.txt,
  abba_xframe_check.txt). Ledger correction appended to EXPERIMENTS.md at the same stamp.
- Apple's R16/R20 start with the paired compute (TA only); their 3D stage waits for it. The Metal port has the same
  fragment-stage edges as D3D12 (same ping-pong RGBA16F surfaces). Bound for matching Apple on those pairs: <= 0.22 ms HD
  (= OV5, already built, ABBA neutral). The row "~2.0 ms D3D12 serialization, not recoverable" is WITHDRAWN.
- That ~2 ms is GPU work: against Apple's per-encoder split every job of ours is 9-14 % slower at the same scene (+2.25 ms HD
  EXP2268; +1.79 ms EXP2307 profile; compute delta fits ~9 % proportional with ~0 fixed part: C0 +0.51 of 5.84, C6 +0.03 of 0.68).
  "Compute at Apple parity" came from the per-dispatch split (inflates Apple's frame 17.86 -> 21.76 ms) and is superseded.
  Cause = OPEN (r8-work-deficit in progress). Already measured and not the cause by themselves: SLC ways (EXP2242/2243),
  DRAMCFG (EXP2245), kernel code (metal oracle, lighting 1.13x only).
- Dependency-driven scheduling ceiling at HD (simulator dag_sim.py): serial today 82.0 % of macOS; Apple's structure 84.1 %;
  full legal DAG 85.1 %; plus DWM beside a GM compute 88.6 % (87.9 % with contention). Parity needs the work deficit closed.
- Only real Apple concurrency we lack: 3D(R7,N) beside C0(N+1), 0.35 ms HD / ~0.6 ms FHD; never fires on our stack even with
  OV2+OV3B on (0 of 10204 boundaries, EXP2294-2297); refusing rule unknown -> receipt-only run (pair conflict resource,
  KMD arrival vs completion QPC, pair-refusal code), to be bundled with the R6 receipts (store masks, DiscardResource count).
- Ranked open rows at HD now: (1) uniform work deficit 1.8-2.25 ms; (2) DWM job between GM jobs 0.85 (C1/C2a in work);
  (3) per-job launch/detect/idle 0.5; (4) cross-frame C0||3D(R7) 0.35; (5) TA5 beside C4 <= 0.22 (OV5).
- NEXT CAUSAL TARGET: cause of the uniform work deficit (r8); EXP2309 (N1) for the current-profile HD/FHD budget with pstate.

## UPDATE 2026-11-10T00:34:02Z -- GravityMark HD rate was undercounted: count frames, not present rows

- **Source:** tool change 7b7151e6b (CHG-20261110-7b7151e6-GM-FRAME-RATE), re-run by the architect on
  `EXP2307/gm2307a1r3-umd.log` (10+60 s window): present rows 2662 = 44.392 fps = mean 22.53 ms; GravityMark frames
  (execute rows of the benchmark process) 2947 = **49.145 fps = mean 20.35 ms**; presents_per_frame 0.903,
  jobs_per_frame 8.0. Full run gm2196b: score 6713 = 6712 execute rows, 6510 present rows.
- **Cause (observed):** no Present DDI call for a frame that ends <= 16.53 ms after the previous present (1021 of 1021
  skipped frames in three runs). The limiter sits above the UMD; GravityMark vs DXGI is unproven. Not a driver defect.
- **Corrections:** the night plan line "HD ~22.5 vs 17.4 ms" (2026-11-09T23:40Z) is present-based; frame-based HD is
  20.35 ms. FHD is affected by < 1 %. The perf gate and perf-baseline.json stay present-based (history-compatible);
  judge HD levers by `fps_frames` (PERF_INFO line). A lever that shortens frames below 16.7 ms does not show in the
  present-based number.
- **Open tool item:** `agx_gm_scene_align export_ours` and `agx_gpu_frame_breakdown` still drop double-frame present
  intervals, i.e. the fastest frames.
- **HYPOTHESIS (one coarse pass, 10 s bins, lag by eye ~10 s, EXP2196 HD full run vs macOS hd.csv):** our share of the
  macOS rate is lowest in the fast scenes (macOS 76-85 fps, ours 42-54: 55-67 %) and highest in the heavy scenes
  (77-83 %). To be checked on the EXP2309 FULL logs with fps_frames and a fitted lag before it is used.

## UPDATE 2026-11-10T00:46:28Z -- R8: the HD work deficit is job-specific; the frame gap is mostly a fixed offset

Source: offline analysis .local/night-20261110/r8-work-deficit/REPORT.md (scripts and out-*.txt beside it)
on EXP2307 a1r3 / EXP2308 a1r3 against the macOS per-encoder split (MACOS-20261105-metal-timing) and
the per-dispatch split EXP2226. No hardware run. Frames = execute rows.

- Frame level: ours wall = 3.16 + 1.057 [1.023,1.102] x Apple wall (EXP2308 a1r3); 4.18 + 1.002
  [0.970,1.042] (EXP2307 a1r3). wall - work = 2.1-2.3 ms in every scene class. The gap to macOS is
  mostly a fixed 3.2-4.4 ms per frame, not a rate ratio.
- CORRECTION to the HYPOTHESIS in the previous update ("our share is lowest in fast scenes, 55-67 %"):
  the fitted shares are 78 % (fast third of the bins), 80 % (middle), 82-83 % (heavy), 71 % in the two
  fastest bins (scene 48-52 s). Lower in fast scenes because of the fixed offset, by 4-11 points.
- Work deficit +1.87 ms (vs Apple completion increments; +2.2 ms vs Apple GPU spans). "Uniform ~9 %"
  holds only as an aggregate (job sum = 0.22 + 1.107 x Apple) and is REFUTED as a mechanism:
  C0 = 2.70 + 1.258 [1.230,1.281] x Apple(e7+e9), constant part (67 dispatches) at parity 0.99-1.00;
  C2 slope 1.066; R3 slope 0.98 plus fixed 0.22-0.31 ms; C4 1.167; R5 3D 1.16-1.19; R1 1.13-1.17.
  Split: C0 scene part 0.51 (0.75 vs spans, ~1.0 in heavy scenes), C4 0.19, C2 0.19, R3 0.18,
  R5 3D 0.17, R1 0.16, three render compute preludes 0.195 fixed (R1 0.063, R3 0.040, R5 0.092; ours
  only, UMD batch design), R5 TA ordering 0.21 (Apple runs e16's vertex phase under c15).
- REJECTED (architect hypothesis of 2026-11-09 night plan): firmware idle exit / clock ramp /
  context-switch cost per job. Job-time residual vs pre-job idle and vs a foreign (DWM) job right
  before is flat, <= 0.01-0.02 ms (out-idle.txt). Also excluded for C0: clock (parity constant part in
  the same job), static code size, per-dispatch overhead, submission.
- R4's "compute at parity" came from the per-dispatch Apple split, which adds a fixed +0.68 ms to
  encoder 7 and +0.39 ms to encoder 11. Use the per-encoder split for totals; per-dispatch only for
  large single launches.
- C0 kernels behind the 1.26 slope: hierarchy traversal 97ae4b027aa43c6c (= Apple e7 d24/d25, about half
  of the scene slope) and a8260e4297efffb3 (= e7 d48, about a quarter). The same chain in C2 (variant
  fc968f7a) runs at 1.05-1.075. OPEN between (a) generated code of the C0 variants and (b)
  data-dependent memory cost (1.9x indirect grid, atomics). HYPOTHESIS (cross-profile hint only): (b).
- NOT VERIFIED by R8: pstate residency for EXP2307/2308 (no receipt); piece-to-dispatch mapping is by
  order and pipeline identity (+-0.05 per row).

Next causal target (one run, no package change): N2 SPLIT-TAGS, spec in
.local/handoff/OV-ARCHITECT-HANDOFF.md (arms: control / EXP2078 C0 list / EXP2211 C2 list / control on
one boot, P6 residency >= 95 % required). Decision rule: C0 ratio within +-0.05 of the C2 ratio on
shared pipelines -> (a), Metal-oracle op-class diff; higher by >= 0.10 -> (b), atomic/chase probe at P6.

Lever ranking by size at HD (ms per frame, upper bounds; none validated on hardware yet):
1. wall - work 2.1-2.3: DWM job between GM jobs 0.85 (cross-process pairs C1/C2a in work), launch +
   detect about 0.5 (KMD prebind miss -> on-time completion -> depth 3), cross-frame C0(N+1) beside
   3D(R7,N) 0.35 (never fires; needs a refusal-reason receipt).
2. C0 scene part 0.5-1.0 (cause open, N2).
3. Render compute preludes 0.195 (UMD batch design) and R5 TA under C4 <= 0.21 (OV5, neutral so far).
4. C4 / R5 3D / C2 shader work 0.17-0.19 each (DXIL heavier than Metal for C4 and the planet PS: not
   equal work; no driver lever identified).

## UPDATE 2026-11-10T00:58:50Z -- night baseline EXP2309, queue N2 (EXP2310) and N3 (cross-process pairs)

- EXP2309 (package unchanged, Wom1JobDiagnostics=1, one boot): HD PERF fps_frames 48.97 / 48.99 / 49.15
  (present-based 44.2-44.5), FHD PERF fps_frames 39.60 / 39.71 / 39.86; FULL untraced HD 7784 = 78.3 % of macOS
  9946, FHD 6066 = 77.2 % of 7853. P6 residency 99.4 % mid->post (the pre->mid window holds ~5.3 s of P1 at
  launch). R1's offline FHD model CONFIRMED (wall 26.2-26.6 ms against 26.6-27.3 predicted, score inside
  6000-6200); HD FULL 7784 is slightly above its 7500-7700. This is the base for every lever below.
- N2 = EXP2310 (running): SPLIT_TAGS arms a / b (C0 list of EXP2078) / c (C2 list of EXP2211) / a2; decides whether
  the C0 scene-dependent deficit (0.5-1.0 ms) is generated code or data-dependent memory cost.
- N3 staged: cross-process pairs merged default off (4a83e3a36; C1 Wom1CrossProcessPairs, C2a
  Wom1CrossProcessPrivateLead). Target: the DWM composition render between two GM jobs (0.85 ms/frame HD).
  Experiments N3a (package equivalence) -> N3b (C1) -> N3c (C2a), one variable each; package = EXP2309 source +
  the four commits, without FS-F1.
- FS-F1 (3952f9ad5, size-generic D3D10/11 DXGI Blt) is on the branch, software-only; it is a DWM-visible UMD path
  and gets its own experiment before it rides in any GM package.
- In work offline: source cause of the "in time, never prebound" boundaries (chain refusal 38) and of the
  cross-frame pair C0(N+1) beside 3D(R7,N) never firing (r9-window-misses).

## UPDATE 2026-11-10T01:12:00Z -- N2 verdict (EXP2310): C0 excess sits in two variant kernels

Run: EXP2310, four arms on one boot, all valid (a/a2 controls fps_frames 48.843 / 48.839; P6 mid->post 98.5-100 %;
wer 0, STOPS 0; BUGCHECK ok code=0x0 at the ordered restart). Split cost: arm b 23 jobs/frame, fps_frames
43.933, +2.3 ms for 15 extra jobs = 0.155 ms wall per extra job. Analysis: 3541 (arm b) / 3534 (arm c) frames,
37 two-second bins on Apple's split2 scene clock (lag 10.6 / 11.0 s, corr 0.9996 / 0.9998); piece -> kernel
mapping verified from the measure-d3d12-dispatch tag sequence of a frame. "slope" = Theil slope ours/Apple.
Scripts and tables: `.local/night-20261110/r8-work-deficit/n2_map.py`, `out-n2-map.txt`.

| kernel (ours) | Apple dispatch | ours ms | Apple ms | slope |
|---|---|---|---|---|
| C0 traversal 7aa43c6c x5 | e7 d22-26 | 1.737 | 1.427 | 1.250 [1.239,1.258] |
| C2 traversal fc968f7a x5 | e11 d0-4 | 1.509 | 1.370 | 1.101 [1.094,1.108] |
| C0 97efffb3 | e7 d48 | 0.870 | 0.656 | 1.357 [1.345,1.367] |
| C2 f582ac2c | e11 d26 | 0.518 | 0.476 | 1.070 [1.053,1.088] |
| C0 d27-47 (shared pipelines + d45 variant) | same | | | 1.069 [1.008,1.173] |
| C2 d5-25 | same | | | 0.977 [0.952,1.005] |
| C0 d49-56 + e9 | same | 0.629 | 0.746 | 0.997 [0.849,1.141] |
| C2 d27-33 + e13 | same | 0.461 | 0.614 | 0.997 [0.936,1.060] |
| C0 const piece | e0+e1+e5 | 2.630 | 2.719 | ratio 0.968 |
| C4 | e15 | 2.087 | 1.858 | 1.168 |

- Predictions of the BEFORE entry confirmed (trav 1.2-1.3, d48 1.2-1.4, C2 trav 1.09-1.14, C2 d26 1.07); control
  slopes within 0.05 of R8 (C0 var 1.216, C2 1.071-1.085, C4 1.168-1.175).
- Verdict by the preregistered rule: cause (a) "generated code of the C0 variants" is favoured. Everything in the
  chain that is not a C0 variant runs at slope 0.98-1.07 in both encoders; the excess is carried by 7aa43c6c
  (1.25 against 1.10 for its C2 twin) and 97efffb3 (1.36 against 1.07 for its twin f582ac2c). Excess above the C2
  level: (1.25-1.10) x 1.427 + (1.357-1.07) x 0.656 = 0.21 + 0.19 = 0.40 ms/frame HD; to Apple parity for the
  four variant kernels about 0.68 ms.
- Not excluded: (b) for the heavy kernels' own data footprint (C0 walks a 1.9x larger indirect grid). The small
  shared kernels cannot test that. If the op-class diff finds the variants equal up to constants, the verdict
  flips to (b) and the next step is the atomic/chase probe twin of EXP2267 at verified P6.
- Correction to a working table of this night (never published): "C0 k49 vs e7 d49 = 1.82x" compared a 5-dispatch
  piece (d49-53) with one Apple dispatch. Only the combined d49-56 + e9 row above is valid.
- Unverified lead (HYPOTHESIS until the field is decoded): in measure-d3d12-shader-stats the third-from-last
  field is 0x16-0x52 for every C0 variant and 0x7b-0x8a for every C2 twin.
- Next causal target (offline, no hardware): op-class and hot-loop diff of 7aa43c6c vs fc968f7a and 97efffb3 vs
  f582ac2c, each against Apple's twin in `.local/metal-oracle` (r10-variant-opclass). Closed levers stay closed:
  instruction-count parity, dependent-load latency (EXP2183), wait placement (EXP2124), occupancy, MEMBW.
- Bench order unchanged: ANSQ-T2 slot -> N3a (EXP2311, Wom1JobDiagnostics=1 as EXP2309, both xproc switches
  absent) -> N3b -> N3c.

## UPDATE 2026-11-10T01:20:00Z -- R9: source cause of two depth-two window misses (offline, no hardware)

Report: `.local/night-20261110/r9-window-misses/REPORT.md`; cited lines re-checked at 42dbf3428.
- "In time, never prebound" (0.4-0.5 jobs/frame, chain refusal 38): the successor is not in the KMD queue.
  `AdmissionG4PendingAdmissible` is sampled once (gpuva_g3_windows.c:6282); on FALSE the dxgkrnl submit thread
  sleeps in `AdmissionPlatformRuntimeAwaitWork` until the window is empty and never re-checks. Receipt agreement:
  Wom1SubmitPath Waits[1] 0.53-0.66 per present, mean wait 1.7-2.3 ms. Which predicate fails is a HYPOTHESIS
  (context two-fence limit: dxgkrnl is notified before the context retires the fence, resubmits ~20 us later).
  Fix F-A2 (KMD, class I): the wait re-evaluates admissibility; upper bound 0.15-0.19 ms/frame HD.
- Cross-frame pair C0(N+1) beside the 3D stage of R7(N) never fires: a refusal, not arrival (C0 reaches the KMD
  before R7's end in 99.5-99.8 % of frames). `AgxD3d12JobBarrierExecution` (agx_d3d12_job_barriers.h:408-412)
  resets PreviousKind and the ranges at every ExecuteCommandLists, so the frame-first compute always gets refusal
  2. Fix F-B (UMD owner, class O): carry the last render's exact write ranges across the execution boundary,
  resource-exact rule only; upper bound 0.35 ms/frame HD. Shadow-verdict receipt first.
- Validity note: the "0 of 10204" cross-frame check of the OV2+OV3B ABBA arms compared an intra-execution pair
  (OV3B appends a trailing drain job); it holds only for the default arm.
- Next: implement both default-off with receipts (two sessions), bench as N4a (receipts) -> N4b -> N4c after N3.

## UPDATE 2026-11-10T01:30:51Z -- R10: C0 variant kernels differ by a dynamic constant-buffer index; scoreboard slot policy is the first code-level candidate (offline, no hardware)

Source: `/Users/pavel/public_windows/.local/night-20261110/r10-variant-opclass/REPORT.md` (208 lines). Checked by
the architect against files: stats column order (`drivers/apple-agx/mesa/winsys/agx_win32_shader_stats.h`), the slot
rule in the projection (`native-asahi-latency-schedule.py`, `agx_win32_wait_block`), Apple's listing
(`.local/metal-oracle/m208_13b1c0.s` 0x262-0x288: four device loads on slot 0, one `wait 0`), our listing
(`ours/97efffb3.skel`: `wait a` + `wait b` pairs), packaged stats rows (EXP2310 gm2310a-umd.log).

Established (static, high confidence):
- Source difference inside our own pair of encoders: the C0 variants read the constant buffer at a DYNAMIC register
  index (cb[view*57+k]) and use no texture; the C2 variants use constant indices and 4 texture ops.
  cbufferLoadLegacy total/dynamic/texture ops: 97efffb3 64/54/0, f582ac2c 60/0/4, 7aa43c6c 31/20/0, fc968f7a 55/1/4.
- The third-from-last shader-stats column is PREAMBLE (instruction count). C2 variants hoist the constant-buffer reads
  into the preamble (0x7b, 0x8a); C0 variants cannot (0x16, 0x1e). This closes the "unverified shader-stats lead" of
  the N2 entry: the column is not a defect, it is this source difference.
- Lowering difference against Apple: in 97efffb3 each of the 16 frustum plane tests issues 2 per-thread device loads.
  Ours puts them into scoreboard slots a and b and pays two waits; Apple puts both into slot 0 and pays one. Loads are
  at parity (56 vs 59 before the loop); waits are not (51 vs 31 before the loop, 70 vs 47 in main). In the C2 pair
  (f582ac2c vs m201) waits are at parity (19 vs 17).
- Cause in our source: the upstream Asahi slot choice "first slot with nr_pending == 0" (agx_insert_waits.c:97-103),
  kept in our W1 projection. W1 (EXP2124) left every a/b pair in place, so it never tested this pattern.
- Twin table (by skeleton): 7aa43c6c = m183, fc968f7a = m187 (high); 97efffb3 = m208 (medium-high, alternative m196);
  f582ac2c = m201 (medium, alternative m202).

CORRECTION to the "C4 offline levers closed" bullet of the 2026-11-05 section above (text left as written): that
bullet compares the traversal with Apple's m187. By R10's skeleton match m187 is the twin of the C2 traversal
fc968f7a, and the twin of 7aa43c6c is m183. What stays closed: static instruction parity of the traversal, L4
(EXP2183), W1 as implemented (EXP2124), MEMBW. What is reopened, narrowly: wait placement, only as the slot choice
for paired loads, because no experiment has covered it and EXP2310 split tags now resolve 0.004-0.008 ms per kernel
(EXP2124/EXP2183 judged whole-frame FPS with ~1 % noise against an expected effect <= 0.5 %). Occupancy with the
corrected pairing: C0 kernels sit on the same tier as their twins (1024/1024, 832/832); Apple's thread count is
inferred from the highest register, HYPOTHESIS.

Not established (HYPOTHESIS, confidence medium-low): that the extra waits cost the measured time. 27 removable waits
are 3.4 % of the static instructions of 97efffb3, the excess is 36 %; the hypothesis needs a `wait` to cost about
5 ns per group (a reschedule point, not an ALU op). For it: Apple pays the same per group in C0 and C2 (340 vs
344 ns/group), ours 461 vs 368; the fits are linear with zero intercept and overlapping group ranges, which argues
against a pure data-size cause (b); excess ratio k48:p10 = 5.0 vs removable-wait ratio 4.0 (four points, correlation).

Implemented (class O, opt-in, default byte-identical): SS1, `APPLE_AGX_WAIT_SINGLE_SLOT=1` queues every asynchronous
op on slot 0; hazard rules, barrier drains and the 8-pending overflow wait are unchanged. Two sites: the upstream
local pass and `agx_win32_wait_block`. Test `tests/test_d3d12_wait_single_slot.py` (four captured kernels; default
identical for absent/""/0/2/true with and without W1; with the option: only waits removed, no slot b, operands
otherwise equal). Wait counts in the test fixture, default / W1 / single / single+W1: 7aa43c6c 119/110/90/89,
fc968f7a 126/118/96/96, 97efffb3 144/141/109/106, f582ac2c 133/130/107/105. R10's fixture (closer to the package:
793 instructions for 97efffb3 against 801 in the packaged stats row): 54/45/40/34, 39/31/29/23, 70/65/43/41,
38/34/29/27; Apple 32, 25, 47, 32. The offline fixtures do not reproduce the packaged compile options, so the
packaged counts are read from the shader-stats rows of the run itself.

Next (N5, after N3/N4, one variable = slot policy): split-tags ABBA control / single / single / control, HD windowed
100k -temporal 0, traced PERF 10+60 s, tag set b of EXP2310. Expected if the hypothesis holds: 97efffb3 slope against
e7 d48 from 1.357 [1.345,1.367] to <= 1.20 (0.870 -> <= 0.77 ms), 7aa43c6c x5 from 1.250 to 1.17-1.22; bound of the
gain 0.2-0.4 ms/frame HD. Failure: 97efffb3 slope >= 1.33 in both single arms -> slot and wait-count policy rejected
for good; next is the (b) probe (C0 kernel on the C2 view data). Arm validity: INSTRS of 97efffb3 in the stats row
below the control's 801 and equal P6 residency across arms.

## UPDATE 2026-11-10T02:22:53Z -- N3a/N3b verdict (EXP2311, EXP2312): cross-process pairs C1 accepted by the firmware

- EXP2311 (N3a, package 25c1945b6, both switches off): CONFIRMED equal to the EXP2309 baseline (suite 119/0,
  no xproc receipt, HD fps_frames 48.521 / 48.901 / 48.708 at back-to-back cadence).
- EXP2312 (N3b, Wom1CrossProcessPairs=1): CONFIRMED. The hardware unknown of C1 is resolved: the firmware accepts
  a compute work item of one VM slot queued behind a running 3D work item of another slot. Receipt v2 at post-gm:
  considered 10662, kicked 5652, fell_back 0, failed 0; no GPU_RESET, no PREEMPT_REFUSAL, kick_failures 0;
  suite 119/0; perf gate PASS. Window effect: compute_on_3d kicks 35468 -> 41175, about 0.7 extra prebound
  kicks per GravityMark frame.
- C1 is class I: accepted on receipts and a clean run. HD fps_frames 49.051 / 49.417 / 49.333 against EXP2311 is
  +1.14 % mean, but the two arms are separate boots (the switch is read at adapter start; a same-boot adapter
  cycle is invalid since EXP2308), and EXP2309 with a different cadence sits between them. Bounded statement:
  gain between 0 and about 1.1 %, not claimed. No default flip from this boot.
- New observation, not yet a lever: refusal 55 (render_job_window.c:296-300, a cross-process pair that is not
  "running RENDER -> next COMPUTE") fired 4987 times in the same GravityMark window, about 0.62 per frame, the
  same order as the admitted class. The receipt does not split 55 by kind pair. HYPOTHESIS (medium): most of
  them are the DWM render queued behind a running GravityMark compute or render (the mirror pair). Deciding it
  needs a receipt-only split of code 55 by (running kind, next kind, present or not), class D.
- Next: EXP2313 (N3c, C1 + C2a). C2a is class O: kept only with a mean fps_frames gain >= 1.0 % over EXP2312,
  otherwise default off and the bench moves on. Then N5 (SS1, package 25c1945b6 + 0cd63ef87) unless F-A2 and
  F-B are merged first (N4).

## UPDATE 2026-11-10T02:41:47Z -- N3c (EXP2313): C2a private lead accepted by the firmware, no gain, KEEP-OFF; N4/N5 code merged (no hardware)

N3c, GM arms read by the architect from the evidence directory with tools/agx_gm_perf.py summary (run-health items
beyond those listed here are in fork 6's EXP2313 ACTUAL):
- fps_frames HD PERF, same cadence, separate boots: EXP2312 (C1) w 49.051 / r2 49.417 / r3 49.333, mean 49.267;
  EXP2313 (C1 + Wom1CrossProcessPrivateLead=1) w 49.425 / r2 49.173 / r3 49.147, mean 49.248. Difference -0.04 %.
- Receipt v2 post-gm: considered 10569, kicked 5597, fell_back 0, failed 0, refusals {55: 4956, 6: 16};
  lead considered = waived (post-x 7618 / 7618), refused none. No GPU_RESET / PREEMPT_REFUSAL in the receipts.
- Verdict by the rule fixed before the run (gain >= 1.0 % needed): C2a is class O without a gain -> default off,
  no closing boot, line closed for GravityMark. The firmware accepts the private lead (kept as a proven option).
- C1 stays as recorded (class I, accepted, default off until a same-boot comparison form exists for registry
  switches; EXP2308 forbids adapter cycles).

Merged to gpu/memory-latency after review, gate BASELINE_OK twice in the main checkout (0 NEW), software only:
- ebfb0fa80 / 236d61b52  OV2 cross-execution: shadow verdict receipt (class D) and apply switch (class O, process
  environment APPLE_AGX_D3D12_OV2_CROSS_EXECUTION=1). The shadow verdict exists only with the KMD OV2 feature
  (Wom1Ov2NoHazardLead=1) in a measured run.
- 3e19cb440  Wom1SubmitPath v2 (class D): first failing predicate of a non-admissible submission.
- 53956af4d  F-A2 Wom1SubmitRequeue (class I, default 0): the waiting submission re-tests admissibility.
- 53237bfd6  Wom1CrossProcessPairsReceipt v3 (class D): refusal 55 split by running/next kind.
Review notes: default-off identity checked in the diff for both switches (AgxD3d12JobCrossApply returns the old
verdict; AdmissionG4SubmitAwait calls the old wait); the F-A2 predicate has the same sample-then-queue window as the
existing deferred path. Not MSVC-built yet.

Next causal targets (bench, one package = package line 25c1945b6 + 0cd63ef87 + the five commits, all off by default):
1. N4a: package identity against EXP2312 + SubmitPath v2 (F-A2 premise) + refusal-55 split; N5 arms (SS1,
   per-process, split tags) in the same boot.
2. N4b: Wom1SubmitRequeue=1, only if the premise holds.
3. N4c: OV2 feature on; shadow verdict read (x0), then per-process arms of the apply switch in the same boot.
Still true: no proven score gain this night; HD 78.3 % / FHD 77.2 % of macOS (EXP2309).

## UPDATE 2026-11-10T03:23:10Z -- N4a (EXP2314) receipts: F-A2 premise CONFIRMED, refusal 55 is render -> prelude render

Read by the architect from evidence/EXP2314 (submit-path-post-gm.fixed.txt, submit-path-x.fixed.txt,
xproc-rx-*.txt); run health and identity are in fork 6's EXP2314 ACTUAL (fps_frames 49.205 / 49.100 / 49.572,
mean 49.292 vs EXP2312 49.267, +0.05 %: the package with all new code off is identical in performance).
- Wom1SubmitPath v2, GM block (pre-gm -> post-gm): not admissible by the context two-fence limit 5819, of these
  the context's oldest fence was already REPORTED by the firmware in 4739 (81 %); idle 12680; irql, not queueable,
  pending capacity, faulted all 0. Waits "worker finishing" 5654, mean 1.9 ms. Traced run x: context limit 2036,
  reported 1713 (84 %), 0.56 / 0.47 per present. Requeued 0 (switch off).
  Verdict: the R9 premise holds (rule: >= 0.05 per present; measured ~0.5). In about half of the frames one
  submission is refused only because the worker has not yet retired a fence the firmware already reported, and
  then waits for the whole pending queue to drain: the pipeline falls to depth one for that job.
  -> N4b (Wom1SubmitRequeue=1) GO. Class I: accepted on receipts (Requeued > 0, wait count and mean drop) and a
  clean run; a default flip needs more than a separate-boot comparison.
- Wom1CrossProcessPairsReceipt v3, GM block: refusal 55 = 4427, of which running render -> next render with
  prelude 4342 (98 %), render -> render 85; no compute on either side, capability_off 0, no_process 0.
  The earlier hypothesis (a compute or render of GravityMark with a DWM render behind it) is REJECTED in its
  compute half. The remaining cross-process volume is two renders of different processes. Admitting it needs two
  things the same-process window does not have either: the prelude pair class (EXP2100: unproven, firmware
  faults, gated) and a second resident buffer manager (refusal 58). Its gain is bounded by the TA/3D overlap with
  a DWM render (DWM 3D ~0.85 ms per frame) and EXP2313 showed that removing the lead wait of the admitted
  cross-process pairs moves nothing. Lever PARKED: not on the critical path for GravityMark; reopen only with the
  prelude class work.
Next: N5 arms (EXP2315, same boot) -> per-kernel fit; N4b; N4c.

## UPDATE 2026-11-10T03:50:17Z -- N5 verdict (EXP2315): slot policy REJECTED for 97efffb3, small confirmed effect in the traversal, SS1 stays off; submit phase race fixed offline

Source: architect's fit on the three valid arms of EXP2315 (c1 unset, s1/s2 APPLE_AGX_WAIT_SINGLE_SLOT=1; c2 invalid),
evidence .local/experiments/EXP2314-n4a-n5/evidence/EXP2314/gm2315{c1,s1,s2}-umd.log, method of R8 n2_map.py (tag set b,
pattern C{15}RCCRCRCR, scene clock by lag against the macOS split2 data, 2-s bins, 37 common bins, lag correlation
0.9984-0.9996). Variable acted: INSTRS 0x321 -> 0x306 (97efffb3), 0x45e -> 0x450 (7aa43c6c). P6 mid 90.1 / 88.2 / 90.4 %.

| piece (ms, scene-aligned mean) | c1 | s1 | s2 |
|---|---|---|---|
| C0 k48 = 97efffb3 | 0.871 | 0.866 (-0.5 %) | 0.865 (-0.6 %) |
| slope vs Apple e7 d48 | 1.358 [1.346,1.369] | 1.349 [1.332,1.363] | 1.354 [1.344,1.363] |
| C0 trav = 7aa43c6c x5 | 1.738 | 1.690 (-2.7 %) | 1.689 (-2.8 %) |
| slope vs Apple e7 d22-26 | 1.253 [1.242,1.261] | 1.210 [1.197,1.220] | 1.221 [1.209,1.229] |
| C0 (15 jobs) | 6.476 | 6.402 (-1.15 %) | 6.398 (-1.21 %) |
| all 23 jobs | 19.388 | 19.454 (+0.34 %) | 19.418 (+0.16 %) |
| R7 span | 0.298 | 0.464 | 0.505 |
| frame period, scene 10..76 s (33 bins) | 23.407 | 23.847 (+1.9 %) | 23.888 (+2.0 %) |

- VERDICT by the preregistered failure criterion (R10 section, "97efffb3 slope >= 1.33 in both single arms"): wait count
  and slot policy are REJECTED for good as the cause of the 97efffb3 excess (+0.21 ms). With the option that kernel has
  fewer waits than Apple's twin (43 vs 47 by the R10 fixture) and still runs at 1.35 x. Do not retry wait variants there.
- CONFIRMED, small: the traversal 7aa43c6c responds as predicted (slope 1.253 -> 1.21-1.22, inside the expected band
  1.17-1.22), -0.048 ms per frame; both single arms agree within 0.1 %.
- SS1 as an optimisation (class O): NOT ACCEPTED, APPLE_AGX_WAIT_SINGLE_SLOT stays unset. The C0 work gain is 0.075 ms
  (0.3 % of the frame) and the frame period was 1.9-2.0 % LONGER in both single arms (fps_frames 43.908 -> 43.021 /
  43.005). That period result has one valid control and a fixed order (c1 first), so it is order-confounded:
  INCONCLUSIVE as a slowdown, sufficient to refuse a default. No further bench slot is planned for it.
- OBSERVATION (HYPOTHESIS, not pursued): the last render job R7 is bimodal (about 0.16 ms and 0.65 ms). Share of the
  slow mode: EXP2310 arm b 11 %, c1 34 %, s1/s2 82 %. The period difference between the arms sits in gaps and in this
  mode, not in job work. Candidate owner: the parked cross-process pair "render -> prelude render" (refusal 55), i.e.
  a DWM job ahead of R7. Size if it were removed entirely: slow share x 0.49 ms.
- DEFECT (class C, pre-existing since 2026-10-07): arm c2 lost the device at 26.39 s, Wom1G4SubmitFailure branch 7
  subsite 0x10D kind 4, total 1. Cause (fork 6, checked by the architect in source): AdmissionPlatformRuntimeAwaitWorkEx
  read Backend.Phase a second time after AdmissionPlatformRuntimeReadyEx had reported predicate 4; a job completing
  between the reads turned a healthy runtime into a non-transient refusal. Fix 3f34e22a2 reviewed: one reread into a
  local, Ready on the reread retests, Failed/Stopping/Starting still refuse at once; predicate 4 implies runtime != NULL
  (short-circuit order in ReadyEx), the retest advances only on a state change and is bounded by the deadline. Two
  remarks for the package cherry-pick: the reread should be a volatile/no-fence read so the compiler cannot fold it
  back into two loads, and the package line 25c1945b6 has no FwRestartActive clause (adapt the hunk). Not in package
  f84f0d1a2b83; goes into the N4c package. No hardware confirmation possible beyond absence (first sighting).

Lever ranking after N3-N5 (HD, ms per frame; gap to macOS about 4.7 ms; none of these has a proven gain yet):
1. N4b F-A2 requeue: <= 0.15-0.19 (premise confirmed by receipt, EXP2314). Next boot, EXP2316.
2. N4c F-B cross-execution lead: <= 0.35. Package = N4 line + the phase fix.
3. Traversal slot policy: 0.05, not worth a default.
4. Parked: cross-process render -> prelude render (needs the prelude pair class and a second resident buffer manager).
5. OPEN, largest identified work item without a lever: 97efffb3 +0.21 ms and the rest of the C0 scene part. Remaining
   candidates per R10: exec-mask control-flow shape, integer address arithmetic, per-view data effect. Next step is an
   offline design of the (b) probe (C0 kernel on the C2 view data), no hardware until it is written.
Identified levers 1-3 sum to at most 0.6 ms of 4.7 ms: they cannot bring the gap under 5 % by themselves.

## UPDATE 2026-11-10T04:27:19Z -- N4b verdict (EXP2316): requeue ACCEPTED as class I, +1.05 % separate-boot indication; R11: C0 excess is a fixed cost per thread group; next target N6 section profiler

Sources: EXP2316 evidence .local/experiments/EXP2316-n4b-requeue/evidence/EXP2316 (gm2316{w,r2,r3,x}-umd.log,
submit-path-*.txt, perf-gate.txt), read by the architect; R11 offline report
.local/night-20261110/r11-c0-dynload/REPORT.md with out-r11-map-2310.txt (EXP2310 arm b against the macOS split data).
EXP2316 ACTUAL (rollback, BUGCHECK line) is recorded by the implementer; this section holds the architect's verdict.

N4b = Wom1SubmitRequeue=1 on package f84f0d1a2b83, registry as EXP2314 (C1 on), HD windowed 100k -temporal 0, PERF.

| fps_frames | warm | r2 | r3 | mean |
|---|---|---|---|---|
| EXP2314 (requeue 0) | 49.205 | 49.100 | 49.572 | 49.292 |
| EXP2316 (requeue 1) | 49.251 | 49.886 | 50.285 | 49.807 (+1.05 %) |

- Receipts (GM block): requeued 7493 = context limit 7493; "worker finishing" waits 6346 with mean 27 us (EXP2314:
  5654 waits, mean 1907 us); backend-submitted waits 0. GM error logs empty, no device removal, no GPU_RESET or
  PREEMPT_REFUSAL, kick failures 0, PERF_GATE_PASS.
- VERDICT: mechanism CONFIRMED by receipt, class I, ACCEPTED as an opt-in switch. The FPS step is a SEPARATE-BOOT
  comparison: three earlier boots of the same registry (EXP2312 49.267, EXP2313 49.248, EXP2314 49.292) agree within
  0.1 %, so +1.05 % is about ten times the boot spread, but it is an indication, not a same-boot proof. Default stays 0.
- Two preregistered expectations were NOT met and are recorded as such: the context-limit count rose (5819 -> 7493)
  and the wait count did not fall; only the wait duration fell (70 x). Reading (HYPOTHESIS): a requeued submission
  retests the limit instead of sleeping through the window, so the same limit is counted more often.
- N4c (EXP2317, package b46a092e8f6b = N4 line + phase fix with the volatile read) runs with Wom1SubmitRequeue=1 as
  the base; its variable is the env APPLE_AGX_D3D12_OV2_CROSS_EXECUTION inside the boot (arms x0..x3). x0/x3 are a
  second boot of the requeue configuration (compare with EXP2316 x 50.334).

R11 (offline) on the open C0 item:
- REJECTED: the architect's four-point model "excess = 25 + 1.8 x dynamic CB loads x threads/256 ns per group". The
  kernel 6cf11e8a (54 dynamic CB loads, 256 threads) measures +53.6 ns per group against 122 predicted, and is the
  worst ratio in the frame: 0.348 ms against Apple 0.191 ms (1.82 x). Its C2 sibling 2286332c is at 1.11 x.
- No static count explains all rows: dynamic CB loads fail on 6cf11e8a, integer-op difference has the wrong sign on
  both traversal variants, waits were rejected by EXP2315.
- NEW FACT: the excess is a fixed amount per thread group, not a rate. Traversal 7aa43c6c launches b5/b6/b7 have
  slopes 1.14 / 1.215 / 1.41 but excess 35.1 / 43.4 / 37.7 ns per group (Apple 249 / 202 / 92 ns per group); the C2
  traversal 24.8 / 15.5; 97efffb3 119.5 on Apple's 335.
- Code difference found: we form the row address as register index (iadd idx, k, view x 57; load with lsl 2), Apple
  folds view x 912 into the base once and uses immediate rows. Offline prototype reaches Apple's shape for 97efffb3
  (793 -> 739 instructions, local loads 10 -> 3; Apple 745 / 3) but is worse for 7aa43c6c until the view load is
  hoisted. Upper bound if both kernels reached 1.07 x: 0.378 ms per frame. Kept offline: it is one candidate location
  of the fixed cost, not a measured one.
- CLOSED without a run: scalar shared stores in thread-0 sections. APPLE_AGX_SHARED_STORE_VECTORIZE already exists
  and was measured (pairs c->d -0.24 %, f->e -0.09 %, stays off); the fixture shows it does remove them
  (7aa43c6c local stores 79 -> 37, fc968f7a 64 -> 23), so the removal was tested and gave nothing.

WHY CONTINUE COMPARISON: not applicable. Static comparison of our and Apple's code for the C0 kernels is STOPPED
(three candidate counts excluded, a fourth has only an upper bound). The next step is a measurement.

Next causal target: N6 = prefix-cut section profiler (class D, opt-in env APPLE_AGX_DIAG_CUT=<hash8>:<E|B1|B2|L>,
`stop` placed after register allocation so the prefix is byte-identical). Ten HD PERF arms in one boot on 97efffb3,
6cf11e8a, 7aa43c6c. It separates group launch cost / pre-loop path / loop with device stores / thread-0 sections.
Spec with the four sections: .local/handoff/OV-ARCHITECT-HANDOFF.md, section "N6 SPEC". Runs after N4c.

Lever ranking (HD, ms per frame; gap to macOS about 4.7 ms):
1. N4b requeue: accepted class I; indication about 0.2 ms (separate boots).
2. N4c cross-execution lead: <= 0.35, EXP2317 pending.
3. Traversal slot policy: 0.05, closed as a default.
4. Parked: cross-process render -> prelude render.
5. OPEN: C0 scene excess +0.833 ms (traversal 0.31, 97efffb3 0.21, 6cf11e8a 0.157, rest) -- location unknown, N6.
Levers 1-2 and item 5 together are at most 1.4 ms of 4.7 ms. The remaining about 3.3 ms has no lever yet.

## CORRECTION 2026-11-10T04:30:46Z -- to the UPDATE above (R11 part): "6cf11e8a = 1.82 x Apple" is a piece-mapping error

Refers to the R11 statements in the previous section and in .local/night-20261110/r11-c0-dynload/REPORT.md
(rows "b13 6cf11e8a vs d49", "+53.6 ns per group", "k49 +0.157 ms", "worst ratio in the frame"). The same error was
already noted in the UPDATE of 2026-11-10T01:12:00Z and was repeated by the R11 script; the architect did not catch it
when checking the report.

Evidence (dispatch tag sequence of frame 2000 in EXP2310 gm2310b-umd.log, split tags of arm b): the job that starts
at 6cf11e8a (tag b1db88bf, d49) runs until the next tag a4452d43 at d54, so it holds FIVE dispatches d49-d53
(b1db88bf, 520e6881, 546c0c9a, 05a37650, c4d44596). In arm c the sibling 2286332c (tag 1f20788f, d27) is a single
dispatch because 9726b186 at d28 is also a tag.

| piece | ours ms | Apple ms | ratio |
|---|---|---|---|
| C0 d49-d53 (6cf11e8a + four) | 0.348 | 0.303 (d49 0.191 + d50 0.014 + d51 0.003 + d52 0.003 + d53 0.092) | 1.15 |
| C2 d27 (2286332c alone) | 0.200 | 0.181 | 1.11 |

- 6cf11e8a is NOT an outlier: its piece is +0.045 ms (at most about 15 ns per group if all of it were 6cf11e8a),
  the same level as its C2 sibling. It is not measured alone in any run so far.
- Apple's per-dispatch rows carry a fixed cost (empty dispatches read 0.014-0.016 ms), so sums of several Apple
  dispatches are inflated against one job of ours; with that the piece excess is at most about 0.09 ms.
- The rejection of the four-point model STANDS and is stronger: 54 dynamic CB loads at 256 threads predict 122 ns
  per group, the piece allows at most 15-30.
- Consequence for the address-form candidate (register index against base + immediate): 6cf11e8a has the same form
  as 97efffb3 and no comparable excess, so the form alone is a WEAKER candidate than stated. It stays offline.
- "Fixed excess per thread group" now rests on the traversal launches only (b6 43 ns and b7 38 ns per group at
  Apple 202 and 92 ns per group; b5 is too small to weigh). HYPOTHESIS level, to be measured by N6.
- C0 scene excess +0.833 ms, corrected split: traversal 0.31, 97efffb3 0.21, piece d49-d53 0.05-0.09, the rest
  about 0.22-0.26 in pieces that cannot be compared one to one (Apple per-dispatch cost).
- N6 change: the two 6cf11e8a arms are dropped. Arms: p0, 97efffb3:E, 97efffb3:L, 7aa43c6c:E, :B1, :B2, :L, p1.

## UPDATE 2026-11-10T04:53Z -- R12: gap re-budgeted on EXP2316 x (C1 + N4b on): 3.71 ms = busy +2.48 + non-busy +1.23

Source: /Users/pavel/public_windows/.local/night-20261110/r12-nonbusy-rebudget/REPORT.md (offline, read-only;
scripts nb.py / split_foreign.py / corr.py / align_nb.py, outputs out-*.txt checked by the architect against the
report). HD windowed 100k -temporal 0, PERF window; every cross-run delta is SEPARATE BOOTS.

| ms per GravityMark frame | wall | GM busy union | non-busy |
|---|---|---|---|
| EXP2311 r2/r3 (no levers) | 20.450 / 20.531 | 18.266 / 18.278 | 2.183 / 2.253 |
| EXP2314 x (C1) | 20.173 | 18.239 | 1.934 |
| EXP2316 x (C1 + N4b) | 19.868 | 18.159 | 1.708 |
| scene-aligned 10..70 s: EXP2316 x | 20.313 | 18.618 | 1.695 |
| scene-aligned: Apple split2-hd | 16.605 | 16.139 | 0.466 |
| delta ours - Apple | +3.708 | +2.478 | +1.229 |

- The two levers moved only non-busy time (2.2 -> 1.7 ms); busy union is flat (18.16-18.33). Check: GPU-timeline wall
  19.868 ms = fps_frames 50.334. Pstate valid (P1 time is start-up only, second half 100 % P6).
- Non-busy by cause (EXP2316 x, 1.693 ms): DWM job between two GM jobs 0.910 (0.90 per frame: before C2 43 %, R3
  32 %, R5 9 %; 0.85 / 1.13 / 1.24 ms per occurrence); depth-one launches of R5 and R7 0.47 at those two positions
  (57-60 % depth-one when the render two jobs back is reported only with the compute in between, 1-5 % otherwise);
  firmware start floor 0.31 (about 0.05 ms per transition, Apple 0.035-0.045: parity); completion detect 0.10;
  application/UMD late 0.011. Apple non-busy 0.466 = 16 inter-encoder start gaps only: no foreign job, no depth-one
  launch in the game's timeline.
- Frame-level fit of the busy union over the seven scene bins (architect, from out-align.txt): ours = 1.66 + 1.051 x
  Apple. It does not replace the per-job rows of R8 (lines 2716-2838); it bounds a uniform proportional deficit to
  about 5 %.
- This supersedes the sentence "the remaining about 3.3 ms has no lever yet" of the 04:27Z UPDATE: every part of
  the gap has a row; what is missing is validated levers.

Gap ledger (HD scene-aligned, ms/frame, state = EXP2316 configuration):
| part | size | lever | status |
|---|---|---|---|
| non-busy: DWM job on the GM path | 0.91 (Apple side: 0.05-0.12 foreign per vsync, R5) | MPO second plane, windowed (R7 S2); cross-process render -> prelude render relaunch <= 0.13 is inside it | OPEN, designed by R7, never run; first checkpoint H1 = two-slot DCP swap in m1n1 (needs the operator's eyes) |
| non-busy: late render completion -> depth-one R5/R7 | <= 0.30 | second firmware queue (own notifier) | line closed 2026-11-04 as architectural (EXP2147-2151); reopened as a DESIGN task only: new evidence is the conditional rate above |
| busy: C0 scene part | 0.83 (R8: 0.51 against e7+e9, 0.75 against span) | unknown location -> N6 section profiler | in implementation (fork 6) |
| busy: Apple runs frame N+1 head under frame N tail | 0.27 (+ <= 0.085 non-busy) | N4c cross-execution lead | EXP2317 staged |
| busy: C4 lighting, R5 3D, C2, R3 fixed | 0.19 / 0.17 / 0.19 / 0.18 (R8) | none; DXIL heavier than Metal for C4/R5 | no lever |
| busy: Apple TA under C4 | <= 0.21 | closed for us by D3D12 dependencies | closed |
Reaching a gap below 5 % (0.83 ms) needs about 2.9 of the 3.71 ms: the DWM row, the C0 part and the cross-frame
part together are 2.0-2.1; the rest must come from the late-completion row and shader work.

HYPOTHESIS items inherited from R12 (not measured): dxgkrnl holds DWM's packet until a GM completion (H1); the
depth-one mechanism = late report + two-fence limit (H5; refusals 0.67 per frame vs about 1.0 depth-one launches);
WindowServer does not occupy the GPU between macOS encoders (H6; measurable only with an all-process Metal System
Trace on macOS = operator).
Next (offline, parallel to the bench): MPO preparation (Asahi DCP driver facts for slot z-order and blend, two-slot
swap builder as a patch with host test, H1 spec) and the second-queue design note. No hardware until specs exist.

## UPDATE 2026-11-10T11:17Z -- EXP2317 (N4c) architect reading: lever closed by receipt; the configuration REGRESSED (all renders depth-one)

Source: .local/experiments/EXP2317-n4c-nohazard/evidence/EXP2317 (pipeline finished about 05:02Z; ACTUAL not yet
written by the bench owner; the guest was still on the experiment boot at this stamp). Architect's decode:
- Receipt (ov2-refusal-x0.txt, all 2621 lines of the GM pid summed): seen 0, applied 0; every pair refused with KMD
  reason 6 ("3D stamp unreadable or zero"). By the preregistered rule x1/x2/x3 were not run: N4c LEVER_CLOSED_BY_RECEIPT.
- fps_frames: w 42.346, x0 (traced) 43.678 against EXP2316 w 49.251, x 50.334 (separate boots, -13 %). FULL f0 untraced
  score 7036 / 42.1 fps against EXP2309 7784. Perf gate passed only because its baseline is EXP2220 (40.77).
- R12's nb.py on gm2317x0-umd.log against gm2316x (same tool, ms/frame): busy union 18.448 vs 18.159 (flat); non-busy
  4.446 vs 1.708. Per-job work identical within 0.07. Every render lost depth two: fraction started before the KMD saw
  the previous completion R1/R3/R5/R7 = 0.03/0.02/0.04/0.01 (EXP2316: 0.98/0.66/0.55/0.46); the render is submitted to
  the KMD after the previous compute ENDS (median +0.03..0.05 ms; EXP2316 -0.1..-6.4 ms) and render completion detection
  became 0.7-6.1 ms. Compute jobs are still pre-kicked (0.96-0.98). Pstate pre->mid P6 86 % (valid).
- Package differences against EXP2316: (1) the phase fix 7100e2d79 + b46a092e8 (acts only inside the submit wait loop
  on reason 4); (2) Wom1Ov2NoHazardLead=1, which reports AGX_WIN32_DEVICE_FEATURE_INDEPENDENT_SUBQUEUE to the UMD; the
  UMD then logs and feeds job barriers on every replay (agx_d3d12_windows.cpp AgxD3d12OwnerJobBarriersEnabled) and
  composes compute envelopes through the OV2 hook (agx_win32_gpuva_batch.c ~421).
- HYPOTHESIS (ranked): the OV2 switch (UMD + KMD side) delays every render submission until the preceding compute
  has ended, because it changes every replay while the lever itself never applied; the phase fix is second because it
  can only act on a wait with reason 4. Discriminator: EXP2318 = the EXP2317 package with Wom1Ov2NoHazardLead unset,
  everything else identical (one variable). Expect fps_frames back at 49.5-50.5 and render depth-two fractions back.
  If it stays at 42-44, the phase fix is the cause and must be fixed before any further package (it is in all of them).
- Status of the comparison with macOS (HD windowed, 100k, -temporal 0): best configuration remains EXP2316 (C1 + N4b):
  GPU-timeline frame 20.31 ms vs Apple 16.61 ms scene-aligned (82 %); last FULL untraced score 7784 / 9946 (78.3 %,
  EXP2309, without C1/N4b). No FULL score of the EXP2316 configuration exists.

R14 (late render completion, .local/night-20261110/r14-late-completion/REPORT.md): own depth-one cost 0.272 ms/frame;
0.233 of it because the render prelude's notification is deferred when C4/C6 is kicked onto compute Queue B before the
prelude ended (99 % vs 0.5 % conditional). Proposed lever (class O, effort S): a retryable kick refusal holding a Queue-B
compute behind a running prelude render until its prelude completed; bound 0.19-0.23 ms/frame. Open: worker re-poll
latency after the refusal. Not yet reviewed against source by the architect.
R13 (MPO second plane, .local/night-20261110/r13-mpo-prep/REPORT.md): J313 DCP has two surface slots, fixed z-order
(slot 1 on top), premultiplied flag only (no opaque upper plane, no X8). Patch file (not applied) for a two-slot swap
builder + proxy diagnostic op 0x1106; H1 headless part (swap accepted and latched) needs no photo, visibility does.
Microsoft's MPO hardware requirements ask for a YUV and a stretching plane: whether DWM uses our two RGB planes is open.

## UPDATE 2026-11-10T13:03Z -- EXP2318 architect verdict: the OV2 switch was the EXP2317 regression; first FULL score of C1+N4b = 7923 (79.7 % of macOS)

Source: .local/experiments/EXP2318-n4c-switch-unset/evidence/EXP2318 (EXP2317 package b46a092e8f6b, Wom1Ov2NoHazardLead
unset, everything else as EXP2317; the bench owner's ACTUAL entry and perf gate were still in progress at this stamp).
- fps_frames (tools/agx_gm_perf.py, HD windowed 100k -temporal 0): w 49.048, r2 49.808, r3 50.526, x (traced) 49.796
  against EXP2316 49.251 / 49.886 / 50.285 / 50.334 and EXP2317 w 42.346, x0 43.678. Separate boots, +-1 %: equal to
  EXP2316. Pstate mid P6 89.2 % (valid).
- nb.py on gm2318x-umd.log: wall 20.082, busy union 18.281, non-busy 1.801 ms/frame (EXP2316 x: 1.708; EXP2317 x0:
  4.446). Fraction of renders started before the KMD saw the previous completion R1/R3/R5/R7 = 0.99/0.65/0.59/0.40
  (EXP2316 0.98/0.66/0.55/0.46; EXP2317 0.03/0.02/0.04/0.01). Render depth two is back.
- Verdict: CONFIRMED. Wom1Ov2NoHazardLead=1 (the INDEPENDENT_SUBQUEUE feature bit and what the UMD does when it sees
  it) removes render depth two and costs 13 %; the phase fix 7100e2d79 + b46a092e8 is harmless and stays. The switch
  stays unset and must not be set in any package until its side effect is removed (research line R15: root cause of
  KMD OV2 reason 6 and of the submission delay; the cross-frame row 0.27 + <= 0.085 ms stays open behind it).
- FULL f0 (untraced, HD windowed): Score 7923, 47.4 fps. macOS 9946: 79.7 %. Previous FULL 7784 (EXP2309, without
  C1/N4b): +1.8 %. This is the first FULL score of the C1 + N4b configuration and the current best.
- Process defect recorded: the package perf gate compares against EXP2220 (40.77) and passed EXP2317 at -13 %. From
  now on the close-out of every GM package also compares HD fps_frames of arm w with the EXP2316/EXP2318 level
  (floor 48.0 = -2.5 % of 49.15); below the floor = stop and discriminate before the next package.

R14 reviewed against source (architect): ACCEPTED as the next KMD change (class O, effort S). flight_on_a/flight_on_b
(apple_agx_render_shared_memory.c) put a prelude render on both firmware Queues; publish_own raises Queue B's threshold
when the next compute is published, so the prelude's notification is batched with that compute. Lever: retryable kick
refusal 66 in AdmissionJobWindowKickRefusal while Running is a prelude render whose prelude has not completed and Next
joins Queue B; decided before the pending head is claimed (the OV5 defer precedent at backend_platform_windows.c
~7200, EXP2292 lesson); while it holds the poll plan is capped (sleepCapUs, the existing 250 us "not bound yet" cap at
~8205), so the compute is still kicked >= 1.5 ms before the render ends. Switch Wom1PreludeNotifyGate, default 0.
Bound 0.19-0.23 ms/frame. Spec: .local/handoff/OV-ARCHITECT-HANDOFF.md section "R14 kick gate".

Research lines started 2026-11-10T13:03Z (read-only, reports under .local/night-20261110/): R15 N4c reason 6 + switch side
effect; R16 prelude-free renders (ledger rows 0.195 busy + 0.233 notify batching + <= 0.13 cross-process relaunch);
R17 shader work rows C4/R5/R3/R1 against the Metal oracle (0.9 ms/frame without a lever today).

## UPDATE 2026-11-10T13:25Z -- architect: R16/R18 verdicts, DWM row route changed, gate package gains PT1

- R16 (prelude-free renders): the three render preludes are our expansion of counted ExecuteIndirect; design (a')
  (kernels appended to the still-open application compute batch, UMD only) targets the same 0.233 ms row as the R14
  kick gate. PARKED: built only if the R14 gate is rejected on hardware (gate confirmed -> (a') is worth ~0.06 ms).
- R18 (DWM plane policy): DWM acceptance of our truthful MPO caps (RGB only, no stretch, software cursor) is a
  HYPOTHESIS judged 0.3; the D3D11 UMD PresentMultiplaneOverlay1 and CheckDirectFlipSupport would be mandatory.
  A reference observation is not available: the builder's RX 7900 XT reports "MPO MaxPlanes: 1" (console-session
  dxdiag, 2026-11-10T13:25Z). Windows-side MPO work is PARKED. The m1n1 DCP two-slot diagnostic (H1) stays queued.
- DWM row (0.91 ms non-busy): macOS shows no serial compositor gap in GravityMark's timeline, so the route to parity
  is concurrency of DWM's render with GravityMark's jobs (cross-process refusals 55/58), researched as R19.
- Gate package: two default-absent switches, one per boot: Wom1PreludeNotifyGate (refusal constant 7, event-based
  input, 1000 us fail-open) and Wom1PerfTgtUtilization=60 (PT1: HwDataA floats 0x788/0x7ec; r4). Not run yet.
- Storage base: AppleAns 0.4.8 (bus type NVMe) installed, ANSQ-T7a/T7b confirmed (ledger b9aa05837).
- Specs and decisions: .local/handoff/OV-ARCHITECT-HANDOFF.md (sections of this date).

## UPDATE 2026-11-10T13:38Z -- architect: R15 reopens N4c (EXP2317 misread corrected), R17 adds T5 texture-state promotion, R14 gate final form

- CORRECTION to the 11:17Z update: "every pair refused with KMD reason 6 (3D stamp unreadable)" was a misreading.
  The count is the UMD shadow verdict 6 detail 3 (CopyBufferRegion before C0's batch counted as a foreign command);
  the KMD stamp path never ran (Wom1Ov2Receipt Waived 0, Refused[6] 0). N4c is NOT closed. EXPERIMENTS.md carries the
  EXP2317 correction entry.
- EXP2317's -13 % = profile side effects: OV3 deferral without OV3B gave R7 a prelude (no prelude-free render left,
  pre-submitted renders 0.093 vs 0.735), plus RcExportPending stuck TRUE (registry export work after every job).
  Split not measured (HYPOTHESIS). Wom1Ov2NoHazardLead stays unset in every package.
- Accepted: C-a (class C, KMD: the export flag must always have an export that clears it), C-b (class C, UMD: OV3
  never creates a prelude on a prelude-free render), N4c-v2 (class O, effort M: REG_DWORD Wom1Ov2CrossLead -> own
  feature bit; tracking + cross-execution only; copies placed as ranges; receipt of the first conflicting pair).
  Bound 0.27-0.35 ms/frame HD, realistic 0.20-0.33 (+1.0..1.6 %). Open before the copy rule is implemented: how our
  CopyBufferRegion executes (job member / separate job / CPU copy with a map wait) -- R15 follow-up, offline.
- R17 (shader rows): Apple loads constant-handle textures into texture-state registers once in the preamble; our
  D3D12 UMD never sets key->promote_textures, so every shadow-loop sample of C4 (lighting) and R5 (planet PS) is
  bindless. T5 = promotion behind env APPLE_AGX_D3D_PROMOTE_TEX (default off), accepted as class O, effort M, bound
  0..0.36 ms/frame HD (time effect is a HYPOTHESIS; structure verified). Built on branch gpu/t5-texstate-promotion in
  its own worktree, merged into a package by the bench owner. T1/T2 (ALU count) not built (EXP2188). R3, R1, C2 keep
  no shader lever.
- R14 gate final form (implemented by 32597a428, default off): refusal constant 7, event-based input only, applies
  only to successors that join Queue B and not Queue A, fail-open after 1000 us.
- EXP2319 (N6): untagged w/r2/r3 run first, w carries the 48.0 floor; tagged arms are N6 controls only.
- Builder as MPO reference, completed: RX 7900 XT (monitor attached) reports MPO MaxPlanes 1; the Ryzen iGPU
  (0x164E, no monitor) reports 0. Plane counts are per adapter output and do not add up. No DWM OverlayTestMode
  value is set there. The Windows-side MPO package stays parked.
- Lever queue for the bench: EXP2319 (N6) -> gate package (R14 gate, PT1) -> package with N4c-v2 + T5 (one variable
  per boot) -> FS-D1 / MPO H1.

## UPDATE 2026-11-10T13:52Z -- architect: N4c-v2 copy rule settled offline; PT1 field type corrected

- N4c-v2 change 4 is no longer contingent. A D3D12 CopyBufferRegion (kCommandCopy) is replayed through
  resource_copy_region, which for buffer->buffer is two compute kernels inside the context's compute batch
  (reference mesa agx_blit.c:618-655), with no map, no CPU wait and no job of its own. Rule: under the
  Wom1Ov2CrossLead feature bit a kCommandCopy adds WRITE {dst heap, offset, bytes} and READ {src heap,
  offset, bytes} to the open compute batch's cross-execution shadow and is not a foreign command;
  intersection with KeptRender -> reason 8, unplaceable -> reason 11; other non-dispatch kinds stay foreign.
- Legality evidence (existing receipts, no hardware): measure-batch-deps at position C0, conflict with
  the previous batch (R7 of the previous frame) = 0 of 4004 / 3475 / 3940 pairs (EXP2316 x, EXP2317 x0,
  EXP2318 x); Bloom sets cannot miss a conflict. C0 does conflict with C6, R5, C4 of the previous frame.
  New failure criterion for the N4c-v2 run: any copy conflict in the shadow arm stops before apply.
- C0's 1.5 ms pre-submit wait (EXP2318 submit table) is the 16-deep submission window (retire_held on the
  job 16 submissions back), i.e. UMD pacing two frames ahead; it does not block the waive. C0's KMD submit
  is 0.024 ms after C6(N) completes and 0.541 ms (p10 0.244, p90 0.713) before R7(N) completes.
  N4c-v2 bound unchanged: 0.27-0.35 ms/frame HD. HYPOTHESIS: the share above R7's 3D stage.
- Side observation, not scheduled: window 16 = 35-41 ms between UMD submit and firmware kick (two frames
  of latency, FPS-neutral). Class C/latency candidate after the GM levers; Apple's frames-in-flight first.
- PT1 correction (found by the implementer, verified in m1n1 initdata.py:471/:500 and Asahi
  initdata.rs:345/:370): perf_tgt_utilization and perf_tgt_utilization_2 (HwDataA 0x788, 0x7ec) are u32,
  not floats. Wom1PerfTgtUtilization writes the percent as two little-endian u32 words, guarded by
  "current word == 85" at both offsets. Expected values and failure criteria of the PT1 boot unchanged.
- Sources: .local/night-20261110/r15-n4c-reason6/FOLLOWUP.md; handoff sections "N4c-v2 addendum" and
  "PT1 correction accepted" in .local/handoff/OV-ARCHITECT-HANDOFF.md.
- Order unchanged: EXP2319 (N6) close-out -> gate package boots A/B/C -> N4c-v2 + T5 package -> FS-D1.

## UPDATE 2026-11-10T14:05Z -- architect: R19 verdict, DWM row split; K2 + K1 accepted offline (no hardware)

- Source: `.local/night-20261110/r19-dwm-concurrent/REPORT.md` (main checkout), replays on EXP2316 x / EXP2318 x.
  No new hardware run. Best result unchanged: EXP2318 FULL 7923 = 79.7 % of macOS 9946.
- DWM row 0.85 ms/frame HD = 0.48 GPU held by the DWM job (kick -> complete 0.535 ms, 0.90 jobs/frame) + 0.41
  serial admission (arrival 0.044, depth-one launch 0.169, hand-over to GM compute 0.057, to GM render 0.139).
- Mechanism of the admission part (observed): dxgkrnl preempts node 0 for dwm.exe (DxgkDdiPreemptCommand 13629
  per run), withholds GM packets, DWM arrives at an empty KMD and launches depth-one. Correction to
  `investigation/analysis/DWM-COMPOSITION-LEVER.md` E3: the hold is the preemption handshake, not the present fence.
- Verdict by the pre-registered rule (concurrency replaces the plane route only with a bound >= 0.5 ms/frame):
  bound of the concurrency family is <= 0.38 ms/frame, so it does not replace the plane route. The 0.48 held part
  needs a second plane or direct flip (MPO H1 stays queued, Windows-side MPO stays parked). The routes add up.
- Accepted: K2 (class D, firmware stamps of foreign jobs in the timing ring); K1 (opt-in
  `Wom1CrossProcessRenderPairs`, default unset: GM prelude render pre-kicked behind a running DWM render, bound
  0.125-0.14 ms/frame, firmware acceptance of the cross-VM render pair is HYPOTHESIS until its run).
  Not standalone: K3 (0.066, below noise). Parked: K4 (second 3D node, 0.29-0.33 total, dxgkrnl delivery is
  HYPOTHESIS). Rejected: K5 (depth three). HAGS stays last.
- Implementation of K2 then K1: offline, branch `gpu/k1-xproc-render-pairs`; bench order
  EXP2319 -> gate package -> N4c-v2 + T5 package -> K1 package (two boots) -> FS-D1 / MPO H1.

## UPDATE 2026-11-10T14:35Z -- architect: EXP2319 (N6) reading; group launch excluded, cut table defect

- Run: EXP2319 (ACTUAL in EXPERIMENTS.md, commit 5d71558e7), rolled back, package floor and perf gate PASS.
  Best result unchanged: EXP2318 FULL 7923 = 79.7 % of macOS 9946.
- Preregistration defect: the offline cut table came from the cost fixture's default compile (12406 / 14460 bytes),
  not the production compile (guest 5272 / 7770). Ruling: E arms valid (offset 0 is the entry in any binary; this
  is an architect correction of the void rule, not a relaxation by result). B and L arms stay void by the letter
  until an independent offline compile reproduces the guest (R20: all four offsets reproduced with the production
  root-view masks; main code residual +48 / +98 bytes after the last cut offset, cause open).
- Reading (`.local/night-20261110/n6-reading/n6read.py`, scene window 10-84 s, controls p0/p1 within 0.8 %):
  97efffb3 stop at entry 0.0093 ms against 0.798 whole = about 5 ns per group. Group launch is not the item
  (spec decision row 1 rejected). PROVISIONAL: its first 164 bytes (four dependent device loads + two barriers)
  cost 0.114 ms = 61 ns per group; Apple's twin has the same prefix structure, so the 0.142 ms excess sits after it.
- Spec correction: the traversal 7aa43c6c's first launch is a direct 2-group dispatch and the later launches are
  sized by its output, so cut arms empty them. Only single-group latency is readable: launch <= 1.7 us, thread-0
  sections 3.5 + 1.9 us (PROVISIONAL), loop 18.5 us. Throughput per section of the traversal was not measured.
- Consequence: kernel time follows chains of dependent loads and barriers, not instruction count (consistent with
  the null results of SSV and W1). Next causal target, offline: chain depth per path, ours against Apple's twins
  (R21). No C0 lever chosen yet; no bench action from N6.
- Sources: handoff sections "EXP2319 (N6) interim ruling" and "EXP2319 (N6) reading" in
  `.local/handoff/OV-ARCHITECT-HANDOFF.md`.

## UPDATE 2026-11-10T14:41:44Z -- architect: EXP2319 (N6) final ruling; cost fixture not production-equal

- Verdict: all six EXP2319 cut arms (ae, ab2, be, bb1, bb2, bl) are VALID. Supersedes the 14:15Z interim ruling
  ("B/L void by the letter / pending"). The pre-registered offset table was a flag-less fixture compile and is rejected.
- Basis: independent offline production-shaped compile equals the guest `measure-d3d12-shader-stats` row in all ten
  values for both kernels and all eight guest `measure-d3d12-diag-cut` values
  (.local/night-20261110/r20-n6-cut-table/REPORT.md; rows and receipts re-read by the architect).
- Final offsets: 97efffb3 main 5272, E 0, B1 64, B2 164, no L. 7aa43c6c main 7770, E 0, B1 2844, B2 3922, L 3976.
- Final section times: 97efffb3 launch 0.0093 ms, E..B2 0.114 ms (61 ns/group, 4 device loads + 2 barriers in 164 bytes),
  B2..end 0.672 ms. 7aa43c6c single-group latency: E..B1 3.5 us, B1..B2 1.9 us, B2..L 0.2 us, L..end 18.5 us.
- New fact: production guards every SSBO atomic (97efffb3 +8 instrs / +48 bytes, 7aa43c6c +16 / +98). Apple-side
  equivalent unknown; in R21 scope.
- Measurement-tool defect (class D): tests/dxil_agx_cost_fixture.c is not production-equal (missing atomics guard in
  the robustness filter; stand-ins for Gallium binding / sysval / uniform passes). 24 of 82 GM compute kernels equal
  the guest with correct masks; 79 of 82 with both fixed. Fix assigned to branch gpu/fixture-production-shape; not merged.
- Fixture-derived numbers in r10, r11, r17, K1-asteroid-compute.md, K3-c4-kernels.md and the T5 branch test are not
  production values. T5 is admitted to the bench only after re-measurement on the real passes.
- Not verified: binary byte identity; timing neutrality of stop at a cut point; three other kernels differ by 2-12 bytes.
- Active causal target unchanged: dependent load-chain depth per path, ours vs Apple twins (R21, now on
  production-equal listings). No C0 lever chosen. Bench order unchanged.

## UPDATE 2026-11-10T16:20:04Z -- architect: R21 verdict, EXP2321 reading, K2 / T5 / production fixture landed

- R21 (serial load-chain depth, ours vs Apple twins, production-equal listings; `.local/night-20261110/r21-load-chain-depth/REPORT.md`):
  ACCEPTED as analysis. 97efffb3 (b12, 0.798 ms/job) equals Apple m208 in everything kernel-specific; its two surplus
  round trips (one before the plane tests, one store drain per loop iteration) also exist in its 1.07x sibling.
  Verdict: chain depth does NOT explain the 97efffb3 excess (+0.21 ms/frame). Traversal 7aa43c6c is deeper by 5-8
  round trips but only on thread 0 of a 0.027 ms job: rejected as a lever. 6cf11e8a (b13, 0.343 ms/job) carries
  surplus loads against m207: mask words loaded separately, 9 extra in the 16 plane tests, 2 loop-invariant constant
  buffer loads per iteration; `APPLE_AGX_D3D_LICM_CONST=1` does not remove them. Bound of its whole surplus
  <= 0.14 ms/frame (HYPOTHESIS, derived group count). SSBO atomic guards add 0 round trips.
- Anti-loop decision: no wait-placement / slot / serialisation variants (R21 candidates 5-7): EXP2315 already showed
  97efffb3 does not respond to fewer waits. Open are only loads that should not execute per iteration (candidates 3-4).
- HYPOTHESIS (architect, not measured): the 97efffb3 prefix [0,164) costs 0.114 ms for two all-thread load levels,
  about five times the average later load level; a cold first-touch cost per thread group would fit. Path
  frequencies inside [164,5272) are unknown, so the next discriminator is a nested cut ladder (offline staging:
  sub-agent n6b-cut-ladder -> `.local/handoff/N6B-CUT-LADDER.md`). Scope of in-loop invariant loads over all GM kernels:
  sub-agent t6-invariant-loads -> `.local/handoff/T6-INVARIANT-LOADS.md`. Neither has reported.
- EXP2321 (boot B, Wom1PreludeNotifyGate=1; ACTUAL in the ledger, f33a873dd): REJECTED as a default. HD PERF
  fps_frames r2 49.841 / r3 49.868, mean 49.85 against EXP2320 50.08 = -0.45 % (neutral band of the pre-registered
  rule); the gate acted (holds 11923, settled 11851, timeout 63, refused 44037) but the mean hold 831 us fails the
  <= 500 us criterion. No default flip, no repeat boot, no fourth (both-switches) boot. FULL f0 7991 (EXP2320 7888,
  EXP2318 7923) is one sample outside the decision metric: no claim. Best result with a claim unchanged: EXP2318
  FULL 7923 = 79.7 % of macOS 9946. Architect reading: holding a prelude 0.83 ms to save one notify costs what it
  saves; the prelude lever is the cross-execution lead (N4c-v2), not a gate.
- Landed on gpu/memory-latency (software only, status implemented): K2 timing ring v5 (f2ad4db4a, class D);
  T5 texture-state promotion, opt-in `APPLE_AGX_D3D_PROMOTE_TEX=1`, default off (60daee99e, class O);
  cost fixture atomics filter (8a89c06a5) and `--production` shape + `agx_shader_cost.py verify` (bd74069a4).
  The rule "no stand-in fixture number as a production value" stays: use `--production` / `verify` (79 of 82 GM
  compute kernels equal the guest row; 2308298b, 9aa3f9aa, e415b41e differ by <= 2 instrs / 12 bytes, cause open;
  fragment shaders are not covered by `--production`).
- T5 offline result (production-shaped harness, `.local/handoff/T5-TEXSTATE.md`): switch off = guest rows; switch on
  changes only lighting CS 806ee7b1bdb1944a (3450 -> 3424 instrs, code 23978 -> 23820) and planet PS
  428487dae22a5191 (5791 instrs, code ~39830); no GPR / thread-count gain; bindless samples in innermost loops
  16 -> 0 and 33 -> 0. ACCEPTED FOR THE BENCH; expected C4 -0.10..-0.30 ms and R5 3D -0.08..-0.17 ms per frame are
  estimates. Unknown on hardware: preamble texture-state stores to registers 16..20.
- Bench order: EXP2322 (C, launched 16:20Z) -> N4c-v2 package from the new HEAD (x0 shadow / x1 apply / x2 apply / x3 shadow, then
  T5 arms t0 off / t1 on / t1 / t0 if the builder gates pass) -> K1 package -> FS-D1 / MPO H1.
- Not verified: MSVC /W4 /WX of K2 and T5 (builder gate at package time); nothing of this update ran on hardware
  except EXP2321.

## UPDATE 2026-11-10T16:36:00Z -- architect: T6 verdict (loop-invariant device loads), EXP2322 provisional, EXP2323 rulings

- T6 (all 211 GravityMark captures, production-shaped compile, 204 equal the guest row;
  `.local/night-20261110/t6-invariant-loads/REPORT.md`, `.local/handoff/T6-INVARIANT-LOADS.md`): ACCEPTED as analysis,
  no code. 841 device loads sit in real loops, 78 have a loop-invariant address, 16 are in an innermost loop, 4 execute
  every iteration (2 in traversal 7aa43c6c, 2 in 85fb7685 which is outside the steady frame). The only kernel
  >= 0.3 ms/frame with the pattern is the traversal (b4-b8 1.597 ms, 4 constant-buffer reads through a threadgroup
  index); Apple's twin m183 keeps 3 of the same reads in its loop. k49 (6cf11e8a): 2 conditional reads, whole piece
  b13 0.343 vs Apple 0.303 ms, so <= 0.040 ms.
- Verdict: loop-invariant loads are NOT where Apple gains; the class is closed as a lever. R21 candidates 3-4 are
  closed with it. `APPLE_AGX_D3D_LICM_CONST` stays opt-in and unset (EXP2183). The proposed per-job ABBA of that switch
  is NOT scheduled: Apple keeps the same reads and EXP2183 was frame-neutral, so no new evidence raises its
  probability (anti-loop rule). It may ride as a spare arm pair only if K2 per-job data put b4..b8 more than 0.10 ms
  above its Apple twin for a reason other than the firmware start floor.
- Side facts (not levers): the LICM switch never reaches k49 because the DXIL one-pass wrapper loop starts with the
  workgroup barrier and `agx_win32_mark_invariant_shared` skips writer nests (agx_win32_soft_fault.h:196-201); the
  switch changes 31 binaries, including +12 ffma in the fc968f7a loop (cause open).
- Remaining unexplained busy excess after R21 + T6: traversal 0.170 ms (1.597 vs 1.427) and 97efffb3 0.21 ms with
  production-equal or near-equal listings on both sides. HYPOTHESIS: not instruction-level; candidates are per-thread
  first-touch / cache state and firmware-side scheduling of the compute job. Discriminator in work: N6b cut ladder.
- EXP2322 (boot C, Wom1PerfTgtUtilization=60) provisional from the bench owner: r2/r3 49.922 / 50.026, mean 49.97 =
  -0.21 % against 50.08 -> neutral by the rule; the pstate read decides the wording of the verdict (P6 >= 95 % and
  neutral fps closes dip-frame pstate as an fps lever; P6 unchanged means the target word has no effect at 60).
- EXP2323 (N4c-v2 + K2 + T5, package built from fb5c37de5, builder gates PASS): rulings given to the bench owner:
  T arms run after a fault-free x0 stop with the cross-lead in one non-apply state in all four t arms; no bit-exact
  frame is available (animated scene), so T5 correctness = shader rows + zero faults + 45 s shots of t0/t1, any
  visible difference in planet texturing or lighting rejects T5.

## UPDATE 2026-11-10T16:44:43Z -- architect: N6b cut ladder accepted, queued as EXP2324 (class D)

- N6b (`.local/night-20261110/n6b-cut-ladder/REPORT.md`, `.local/handoff/N6B-CUT-LADDER.md`): ACCEPTED. Code landed
  as 21753fed2: `APPLE_AGX_DIAG_CUT=<hash8>:@<decimal byte offset>`, accepted only where a block of the packed main
  function begins, receipt point code 4; absent variable leaves every binary unchanged (offline: uncut k48 and k49
  byte-identical to the base build). Status implemented; MSVC /W4 /WX not run.
- Ladder (production-equal compile, rows equal the guest rows of EXP2319): k48 tag 97efffb3: B2 (164), @210, @1110,
  @2118, @2776, @3228, @4128 (loop header); k49 tag b1db88bf: B1 (176), @364, @1086, @2128, @2792, @3254, @4120.
  Correction of earlier notes: k49's cut tag is b1db88bf (6cf11e8a is the high hash word); k49 has never been cut,
  the 0.0074 ms figure is k49 uncut on an empty input.
- Static counts against the Apple twins: plane groups identical (12 / 12 / 8 constant-buffer loads); ours has fewer
  loads before the planes (15 vs 20) and more before the loop (9 vs 7) and in the loop (k48 9 vs 8; k49 11 vs 8).
- WHAT IS STILL UNKNOWN (why hardware is justified): what `stop` does to a SIMD group with a partial execution mask
  (Apple: 0 of 299 stops nested; Asahi only on abort paths), and the hardware cost of each section (thread
  population per exit, loop trip counts are in no receipt). First nested arm 97efffb3:@210 answers the former; a
  fault there closes the ladder as "not usable".
- Bench order now: EXP2322 ACTUAL -> EXP2323 (N4c-v2 + K2 + T5) -> EXP2324 (N6b ladder, 16 arms, one boot, read
  against EXP2319) -> K1 package (two boots) when its hand-off is ready -> FS-D1 / MPO H1.

## UPDATE 2026-11-10T17:06:14Z -- architect: EXP2322 closed (pstate is not a lever), K1 accepted (class I opt-in), EXP2323 x0 interim

- EXP2322 (`Wom1PerfTgtUtilization=60`, ACTUAL 29ded3cab): REJECTED as a default, neutral. mean(r2,r3) 49.974 against
  50.08 (-0.21 %). P6 held 38036.6 of 38036.7 ms between the mid and post snapshots at target 60 and 39948.6 of
  39948.6 ms at target 85 (EXP2320): the GPU already sits at the top pstate for the whole GravityMark window.
  Dip-frame pstate is closed as an fps lever; no further target or pstate arm. Correction of my earlier figure: the
  86.9-89.8 % P6 share read from EXP2319 was cumulative and included idle time before GravityMark.
- K1 (7b12854de, `Wom1CrossProcessRenderPairs`, default unset): accepted as class I. Offline: the default decision
  equals the pre-K1 decision for every state and replays the EXP2318 receipt; 49 focused tests OK; full gate has no
  new failure except the environmental mu test of the integration worktree. Not verified: MSVC /W4 /WX compile,
  hardware. Runs after EXP2324 as EXP2325 (boot A, switch unset) and EXP2326 (boot B, switch 1), one package.
  Pre-registered rule: default-on only when boot B has suite 119/0, no fault / timeout / TDR / WER, RenderFailed 0,
  refusal 55 render -> render+prelude below 300 per arm, gap p50 at C2>D>R3 and C4>D>R5 at or below 1.05 ms,
  fps not below boot A by more than 0.7 %, CS gate PASS, screenshots correct. An fps gain is not required.
  Gap staying above 1.05 ms with the class kicked rejects H2; K1 then stays opt-in and is not retried.
- EXP2323 x0 (N4c-v2 shadow), interim, ACTUAL not yet recorded by the bench owner: 3006 executions, independent 0,
  all "kept 6 detail 2" with foreign kinds copy and query; copy rule of the current execution works (48096 copies
  placed, 0 conflicts, 0 overflow). The stop is on the kept side: the previous execution has a ResolveQueryData and
  a buffer copy between its last two render submissions (4 render batches, 4 clean), and
  AgxD3d12JobCrossEnd refuses on any such command. x1..x3 skipped by stop (b) as pre-registered. K2 ring v5 PASS by
  content (DWM 3D slots carry bit 0x1 and a non-zero 3D end stamp; bit 0x1 is the right test for a render without
  a prelude block). Next for N4c: a kept-side placement rule (v3) is being derived offline; no hardware until then.
- Bench order now: EXP2323 (T arms) -> EXP2324 (N6b ladder) -> EXP2325/EXP2326 (K1) -> FS-D1 / MPO H1.

## UPDATE 2026-11-10T17:24:14Z -- architect: EXP2323 closed (T5 rejected and removed, N4c-v2 kept side refuses, K2 ring PASS), N4c-v3 specified

- Result with a claim is unchanged: EXP2318 FULL f0 7923 / 47.4 fps = 79.7 % of macOS HD (9946).
- T5 (`APPLE_AGX_D3D_PROMOTE_TEX`, 60daee99e): REJECTED. EXP2323 arm t1 (switch 1) ended in guest bugcheck 0x116
  (p3 0xC0000483, p4 3) within the first ~55 s; t0 (switch unset, same boot) was clean. The measured production
  effect was -26 instructions (lighting CS 806ee7b1bdb1944a) and -69 (planet PS 428487dae22a5191) with no register
  gain; the larger figures quoted earlier came from the stand-in fixture shape and are withdrawn. No retry, no t2/t3.
  The code is removed (revert of 60daee99e = commit 2b33fb096); with the switch unset the binaries were byte-identical, so packages
  built from 7ab0604c1 / 9fb13ab25 (EXP2324, EXP2325, EXP2326) are unaffected. The minidump
  (.local/experiments/EXP2323-n4c2-k2-t5/evidence/EXP2323/exp2323-0x116-mini.dmp) is not analysed.
  What is rejected is this implementation: it wrote texture-state registers 16..20 from the shader preamble, which
  neither upstream Asahi / Honeykrisp (registers 0..15 only) nor our driver had ever exercised, and the pre-run
  record listed exactly that as unknown. What is NOT decided: the time effect of register-indexed against bindless
  sampling. t1 produced no timing, so that hypothesis is untested, neither confirmed nor rejected.
- Correction of the 17:06:14Z update (EXP2323 x0): "0 conflicts" was vacuous. KeptRender / KeptPost are cleared when
  the kept verdict refuses, so no range was ever compared. What x0 shows: 3006 executions, independent 0, all
  refusal 6 detail 2; copies placed 48096 (16 per execution), copy overflow 0; conflict count NOT testable.
  Second correction: command kind 9 is kCommandQuery (BeginQuery, EndQuery and ResolveQueryData), not the resolve
  alone.
- N4c cause, derived offline from the x0 log and source (spec `.local/handoff/N4C-V3.md`):
  - GravityMark submits C0 R1 C2 R3 C4 R5 C6 R7 per execution; it has one PIPELINE_STATISTICS heap with 3 Begin /
    3 End / 3 Resolve per execution. `Wom1Ov2NoHazardLead` is unset on the bench, so ResolveDefer is false and
    every resolve takes the native path of AgxD3d12OwnerResolveQueries.
  - The one kept-side foreign command that reaches R7's LastForeign is the third resolve: it is the active command
    while its writer flush submits R5. After Asahi's contract the copy job of that resolve goes to the compute
    batch (or the CPU), never to a render batch, and writes only [dst_offset, dst_offset + 8*values*count) of the
    destination buffer. It is therefore placeable as a write range, like the N4c-v2 copies.
  - The next refusals are deterministic and were hidden behind 6/2: 11/2 and 11/4 (descriptor tables). Any stage
    with a non-sampler heap range marks the batch "table" and the cross verdict refuses although the walk in
    SetStageHeapWalk already knows every view it binds.
- N4c-v3 (class C/I, under the existing `Wom1Ov2CrossLead` switch, default unset): A. kept-side placed resolve;
  B. descriptor-table placement into cross-only range sets (same-execution verdict inputs stay bit-identical);
  C. a full refusal-mask receipt (`measure-d3d12-ov2-mask`: every refusal class per execution, not only the
  first), so one shadow arm shows all remaining stops at once. Status: specified; implementation in progress in a
  separate worktree; not reviewed, not built, no hardware.
  WHAT IS STILL UNKNOWN (hardware justified after review): whether the 16 copies of C0(N+1) conflict with the
  ranges of R7(N) (refusal 8), and whether R7's ranges fit the 128-entry set.
  Pre-registered: one class-D boot, shadow arm first; any mask bit in more than 1 % of executions -> no apply arms.
- K2 (ring v5): PASS by content in EXP2323 (bit 0x1 test). K1: first MSVC /W4 /WX compile clean (bench owner,
  EXP2325 package from 9fb13ab25); hardware not run.
- Gap ledger change: C4 (0.19-0.30 ms) and R5 (0.17 ms) have no lever in the tree after the T5 removal.
  WHY THE HYPOTHESIS STAYS OPEN: C4 and R5 are the two jobs whose hot loops sample the shadow map bindless, and
  both run at slope 1.16-1.19 against Apple, while the texture-free chains run at 1.05-1.07 (C2) or parity (C0
  constant part); Apple's twins sample the same textures through registers. Candidate T5b (not specified yet,
  facts being collected offline): map the hot constant descriptor-table entries to the bindful registers 0..15
  that the USC TEXTURE word already loads for every draw, no preamble store, no register above 15. No hardware
  until the mapping is derived and reviewed.
- Bench order now: EXP2324 (N6b ladder, running) -> EXP2325 / EXP2326 (K1) -> N4c-v3 shadow boot after review ->
  FS-D1 / MPO H1.

## UPDATE 2026-11-10T18:08:56Z -- architect: N4c-v3 integrated; T5b and PF1 in implementation; EXP2324 partial reading; hung-job 0x116 is its own defect

- N4c-v3 (kept-side placed resolve, descriptor-table placement, 23-bit refusal mask receipt; all behind
  Wom1Ov2CrossLead, default unset): reviewed against `.local/handoff/N4C-V3.md`, integrated as bde539eb8, ledger
  row a256fe466, status implemented. Gates: baseline 1179 tests, 1 NEW = the known mu-gitlink one; builder syncheck
  SYNCHECK_OK. Hardware: one class-D boot after EXP2326, shadow arm first, stop rule "any mask bit in more than 1 %
  of executions -> no apply arms". Not verified: lock cost of the table sink with the feature on.
- T5b (bindful texture remap for constant handles below 16, plus static SRVs before arrays): facts accepted, spec
  `.local/handoff/T5B-BINDFUL.md`, implementation in progress (sub-agent), nothing on the bench yet.
- PF1 expand fold (a'): UNPARKED, because EXP2321 rejected the prelude notify gate and the three render compute
  preludes (0.195 ms busy + 0.47 ms launches per frame) have no other lever. Spec `.local/handoff/PF1-EXPAND-FOLD.md`,
  switch APPLE_AGX_D3D12_EXPAND_FOLD default off, implementation in progress (sub-agent).
- EXP2324 (N6b cut ladder; ACTUAL in the ledger, 91e34dce1): PARTIAL. k48 (97efffb3) 7 of 8 arms, k49 NOT RUN.
  b12 ms per job, window 10-84 s, recomputed by the architect from the arm logs:
  p0 0.7940; B2 (164) 0.1182; @210 0.1322; @1110 0.4214; @2118 0.5194; @2776 0.5999; @3228 0.6394.
  Intervals: [0,164) 0.118; [164,210) 0.014; [210,1110) 0.289; [1110,2118) 0.098; [2118,2776) 0.081;
  [2776,3228) 0.040; [3228,end) 0.155 (pre-loop + loop, not split). Other pieces unchanged in every arm.
  - Ladder question 1 (nested `stop`): six nested cuts at nest 1-4 ran clean and monotone; the cut at the loop
    header @4128 (nest 8) hung the job. A loop-header cut is NOT usable; k49 @4120 is withdrawn.
  - Reading by the pre-registered decision table: [164,210) = 0.014 for one all-thread load, so the prefix cost
    (0.118) is not a rate per load (barrier or first touch); [210,1110) is the largest interval while Apple executes
    MORE loads there (19 vs 14), so its cost is not a load count; the plane groups sum to 0.218 (> 0.21); pre-loop
    plus loop is only 0.155, so the loop cannot hold the whole excess against Apple.
  - HYPOTHESIS (architect, not measured): time follows load levels with per-thread addresses (about 0.026 ms per
    level in [210,1110) against 0.005-0.008 per constant-buffer level in the plane groups), not ALU count.
    Whether [210,1110) is at a hardware bound that Apple also pays is being computed offline (thread count per job,
    bytes per thread, latency / bandwidth / ALU bounds: sub-agent k48-throughput). No C0 lever chosen.
  - EXP2324b (k49 half, k48 tail split) is not staged; its arm list waits for the offline reading of the hang.
- 0x116 on a hung GPU job: EXP2323 arm t1 and EXP2324 arm k4128 ended in the same bugcheck (0x116,
  p3 0xC0000483, p4 3). CORRECTION to the 17:24Z section, which read the EXP2323 crash as specific to the T5 texture
  state promotion: the common factor of both runs is a job that never finishes followed by a failed TDR recovery.
  This is a KMD defect of its own (an application shader that hangs must not take Windows down). T5 stays removed:
  its hang cause is still unexplained. Offline diagnosis of the reset path in progress (sub-agent tdr-0x116; both
  minidumps exist). No hardware run until the fix is specified.
- K1 (Wom1CrossProcessRenderPairs): EXP2325 (boot A) launched by the bench owner, EXP2326 (boot B) follows.
- Bench order: EXP2325 -> EXP2326 -> N4c-v3 shadow boot -> T5b / PF1 arms when implemented -> EXP2324b -> FS-D1 / MPO H1.
- Best result with a claim unchanged: EXP2318 FULL 7923 = 79.7 % of macOS 9946.

## UPDATE 2026-11-10T18:12:22Z -- architect: hung-job 0x116 is the open timeout-only TDR item (addition to the 18:0xZ update)

- Addition, not a new defect: the 0x116 of EXP2323 t1 and EXP2324 k4128 (p3 0xC0000483, same as EXP2286) is the
  already recorded open item "timeout-only TDR recovery". Design: investigation/analysis/TDR-FIRMWARE-RESTART-RECOVERY.md
  (7bedfb1fa). Merged: stage 2a (shared-layer warm restart) and stage 2b + receipt v5 (e6fb8736e). Not done: hardware
  checkpoint H1 (restart drill behind `Wom1FwRestartDrillAllowed`) and the wiring of the warm restart into
  ResetFromTimeout / RestartFromTimeout / ResetEngine ("No TDR DDI wiring before H1" still holds).
- In progress (offline): comparison of the two new minidumps with EXP2286 and the list of what is missing between the
  merged code and working TDR DDIs for a hung application job; whether a looping shader with a live firmware needs the
  full restart is NOT decided. Report: .local/night-20261110/tdr-0x116/REPORT.md (not written yet).
- T5b (offline, uncommitted in .worktrees/t5b-impl, gate running): step A alone regresses shader 144a46deb6ed2427
  offline (+137 instructions, spills 106 -> 118). That hash has 0 rows in gm2319p0-umd.log, so the GravityMark arms are
  not affected; a default-on decision for step A needs a spill guard first. No hardware number yet.

## UPDATE 2026-11-10T18:25:57Z -- architect: T5b integrated (offline); k48 excess narrowed to an unmeasured grid; 0x116 = our ResetFromTimeout returns failure

- T5b is on the branch, default off, NOT run on hardware: step A c115ced95 (`APPLE_AGX_D3D12_CRAWL_BINDFUL=1`),
  step B2 5bf3f567d (`APPLE_AGX_D3D12_SRV_STATIC_FIRST=1`), ledger rows in 287a403f6. Gates: baseline suite with
  only the known NEW failure, builder syncheck SYNCHECK_OK (after an `#ifdef APPLE_AGX_D3D12_DXIL` guard for C4505).
  Offline numbers (cost fixture, not a production value): lighting CS 806ee bindless samples 29 -> 4 under A+B2
  (-25 instructions), planet PS 4284 75 -> 6 (-69). Receipt `measure-d3d12-bindful` {hash hi, hash lo,
  stage | reordered << 8, samples moved, switches}; expected on GravityMark: 806ee moved 1 (A) / 25 (A+B2),
  4284 moved 50 / 50. Not verified: the projected agx_state.c was never compiled (first compile = package build).
  Hardware arms and accept / reject rule: .local/handoff/T5B-BINDFUL.md "NEEDED RUN" (t0 tA tAB tAB tA t0, same boot).
  Step A default-on needs a spill guard first (144a46deb6ed2427: spills 106 -> 118).
- k48 (97efffb3) excess, offline report .local/night-20261110/k48-throughput/REPORT.md (sub-agent; scripts
  apple_d48.py, bounds.py). Checked by the architect against the log: all 3528 k48 dispatch receipts of
  gm2324p0-umd.log carry x = y = z = 0 (the kernel is launched through ExecuteIndirect, RecordDispatch(list,0,0,0,
  &indirect) at agx_d3d12_ddi.cpp:6299), so OUR grid was never measured.
  - CORRECTION to "equal work" for this kernel (earlier sections compare ms per job as if the grids were equal):
    that is an assumption. Apple's twin (d48 / m208) launches 256x1x1 groups with a mean of 1839.6 groups per frame
    (p10 696, p50 1872, p90 3128), 0.6515 ms per job = 44.3 ns per SIMD32 group. Ours would be 53.95 ns per group
    IF the grid is equal.
  - Ruled out for [210,1110) (0.289 ms, the largest interval): a different read set (per-path loads 12/12, 11/11,
    9/9 against m208), duplicate reads, ALU count (1.28x the single-issue ALU bound). Left: more groups on our
    side, per-group stall time, the unsplit loop. Closed lines stay closed (chain depth, slot policy, hierarchy).
  - Cut safety (hang reading): `stop` ends only the active threads (by elimination, medium-low confidence).
    k48: last safe cut @3446, nothing in (3446,4128) is safe. k49: @176 safe; @364, @1086, @4120 unsafe;
    @2128, @2792, @3254 uncertain. EXP2324b may use safe cuts only and only if it still changes a decision.
  - NEXT (smallest discriminator, diagnostic not implemented yet): read back the ExecuteIndirect dispatch
    arguments of 97efffb3 per frame (`measure-d3d12-indirect-args`), one p0 arm, GravityMark HD windowed 100k
    -temporal 0 PERF. Mean x within 5 % of 1840 -> the work is equal and the excess is per-group cost; mean
    x >= 2100 -> the excess is work and the owner is the producing kernel / its culling input.
- 0x116 on a hung job, offline report .local/night-20261110/tdr-0x116/REPORT.md (sub-agent; cdb outputs beside it).
  EXP2323 t1 and EXP2324 k4128 are the EXP2286 condition: dxgkrnl TdrResetFromTimeoutWorkItem -> DXGADAPTER::Reset
  -> PrepareToReset -> TdrBugcheckOnTimeout, p4 = 3, p2 inside our status wrapper of DxgkDdiResetFromTimeout. Our
  DDI returns 0xC0000483 from AdmissionResetEngineInternal (scheduler_windows.c: quiesce of the active job fails
  after 1000 ms at :731, or AdmissionGpuvaG3PrivateReset has no stop / TLB proof at :749, or the later branch); the
  minidumps cannot tell which. RestartFromTimeout never ran. The crashing tree already contained stage 2b (e6fb8736e).
  - Missing, in order: (1) MSVC build + inert checkpoint of the merged 2b tree; (2) H1, three idle warm restarts;
    (3) stage 3 wiring: a restart entry that accepts in-flight work (freeze dispatch, stop firmware, prove RUN = 0,
    end + release every in-flight job, complete the fences of aborted work on both nodes, release private scenes
    under the stop proof, return success) and RestartFromTimeout (ISR reopen, warm start, clear the fault latch);
    (4) stage 4 cold path (SGX power / reset cycle) -- HYPOTHESIS that a firmware stop does not stop shader-core
    threads, so a looping shader needs it; (5) SupportPerEngineTDR stays TRUE, nothing new advertised.
  - New unknown U10: whether the firmware has a host-requested kill of a running context (Asahi has none, only a
    scheduler timeout). One bounded behaviour-only reading of the macOS driver is started.
  - Step 0 (no GPU risk, bench idle only, if the file still exists): decode the Wom1GpuReset v5 record from the
    guest C:\Windows\MEMORY.DMP of EXP2324 (which branch failed, whether ResetEngine ran first, whether the
    firmware raised a timeout event); delete the dump afterwards (disk gate).
  - Hardware order after that: inert -> H1 -> stage 3 + withheld-kick drill H2 -> stage 4 -> H3 (the @4128 arm).
    "No TDR DDI wiring before H1" still holds.
- PF1 expand fold: implementer still running, nothing integrated.
- Bench: EXP2325 / EXP2326 (K1) in progress at the bench owner; no ACTUAL on origin yet.
- Best result with a claim unchanged: EXP2318 FULL 7923 = 79.7 % of macOS 9946. Nothing new measured on hardware.

## UPDATE 2026-11-10T18:45:23Z -- architect: PF1 fold integrated (offline); U10 verdict = firmware halt, not ASC restart; k48 grid needs a GPU copy

Best result with a claim is unchanged: EXP2318 FULL f0 7923 = 79.7 % of macOS 9946. Nothing below is measured.

### PF1 expand fold -- integrated, offline only
- Commits da3489ccb (job-barrier refusal 13) and 8d92ca8ec (fold), ledger rows CHG-20261110-da3489cc-PF1-JB and
  CHG-20261110-8d92ca8e-PF1-FOLD. Gates on the tree with both commits: baseline 1193 tests, only the known NEW
  (mu-flag test); builder syncheck SYNCHECK_OK. The package MSVC build is the first compile of the projected Mesa
  hunks and of the _MSC_VER-only hooks in agx_win32_gpuva_batch.c.
- Switch: APPLE_AGX_D3D12_EXPAND_FOLD=1 on the application process (default off; EXPAND_AHEAD=1 disables it).
- Receipts: measure-d3d12-counted-fold, one per present, 23 values: [0] presents, [1] seals, [2] folded kernels,
  [3] deferred hazard flushes, [4..12] refusals 1..9 (query, no compute, not writer, prelude, capacity, other
  render, order, lost, draw), [13..20] flushes of a sealed batch by cause 0..7 (other, render = expected end,
  dispatch, evict, flush-all, resource, hazard, reset), [21] OV5 renders refused by the mark, [22] OV2 computes
  refused by the mark (layout read from owner_fold_receipt and AGX_WIN32_FOLD_* in agx_win32_expand_ahead.h).
  measure-d3d12-counted-path 6th value: 0 none/off, 1 folded, 0x100|reason refused.
- NOT VERIFIED: C4 still open at R5's first counted draw; non-deferred flushes of the sealed batch on GravityMark;
  UAV write visibility to the folded kernel inside one CDM stream; adoption on render reset (mock only); ordering
  against K1/C1/C2a/depth-two (argued from render_job_window.h); any gain.
- Run and verdict rules: .local/handoff/PF1-EXPAND-FOLD.md "NEEDED RUN" (control vs fold, same boot, HD PERF traced
  r1 warm-up + r2,r3 ABBA, FULL untraced once per arm, suite with fold=1). Expected with fold: compute envelopes
  4/frame, render-prelude envelopes 0/frame (control 3), measure-gpu-prelude rows 0, frame 20.31 -> 20.0-20.1 ms.
  Failure: a v5 envelope in steady state, more than 4 compute envelopes/frame, fault/0x116/flicker, fps_frames
  below control by more than 1 %, six-job duration sum above control by more than 0.03 ms.

### K1 boot A (EXP2325) -- neutral control, as pre-registered
mean(r2,r3) 50.285 fps_frames (+0.41 % vs EXP2320 50.08), FULL f0 7911, perf gate PASS, suite 119/0. Boot B
(EXP2326, Wom1CrossProcessRenderPairs=1) decides K1; refusal-55 limit is read per PERF arm from pre/post receipts
(boot A total 14185 covers every arm of the boot, not one arm).

### k48 grid -- the CPU-side read cannot see it
Diagnostic commit 61c89ffe9 (branch gpu/k48-grid-diag, not integrated): k48's argument buffer is written by the
GPU and sits in a NOT_AVAILABLE (DEFAULT) heap = GPU-only BO without a CPU mapping (agx_d3d12_windows.cpp heap
creation; agx_win32_asahi_bo.c native_map refusal), so the receipt would carry source 0 and no grid. INFERENCE
from heap classes in gm2324p0-umd.log (67 NOT_AVAILABLE buffers), not a hardware observation.
Decision: measure with an opt-in 12-byte GPU copy of the argument record into a CPU-visible ring right after the
k48 dispatch, read some frames later without a wait (diagnostic arm only; its FPS is not a result, the grid
values are). The alternative (all DEFAULT buffers CPU-visible) changes every buffer's class and is rejected.
Verdict rule unchanged: mean x within 5 % of Apple's 1840 groups/frame -> work equal, excess is per-group cost;
>= 2100 -> the excess is work and belongs to the producing kernel / culling input.

### U10 (host kill of a hung job) -- source verdict, changes the TDR plan
Source: macOS 15.7.8 AGXG13G + IOGPUFamily disassembly, Asahi DRM, m1n1 proxyclient
(.local/night-20261110/u10-kill-op/REPORT.md). All macOS facts are for the macOS 15 firmware; ours is 13.5.
- FACT (macOS 15): hang recovery never stops or reboots the ASC and never power-cycles SGX
  (resetFirmwareForRecovery is an empty function on G13G). No per-job or per-context kill op is sent.
- FACT (macOS 15): host-detected timeout = RTKit 0x85<<48 | host_recovery_count, wait for firmware halt_count ==
  count+1, complete all stamps with error, reset per-queue channel state, initFirmwareData, then resume in two
  phases (write 2, wait for 3, rebuild channel tables, write 0). No acknowledgement = kernel panic.
- FACT (macOS 15): firmware-detected events (lockup, CDM kill timeout, MMU) need no halt message; the host errors
  only the guilty command and resumes; ChannelError is answered with RecoverChannel for that queue.
- UNKNOWN: whether firmware 13.5 accepts 0x85 and the 2/3/0 phases (Asahi and m1n1 resume by one write); why the
  looping kernel of EXP2324 k4128 produced no firmware event although the CL progress interval keeps its 250 ms
  default in our RegionC (only the 3D interval is changed, apple_agx_regionc.c).
- HYPOTHESIS kept, now better supported: an ASC stop does not stop running shader cores.
Plan change (design note investigation/analysis/TDR-FIRMWARE-RESTART-RECOVERY.md stays the base):
1. New stage 3H before stage 3: halt request, bounded acknowledgement wait, error-complete pending fences, reset
   queue/ring state, two-phase resume. No ASC stop, no UAT rebuild.
2. Warm ASC restart (stage 3) is demoted to the dead-firmware class; it is no longer the answer to a looping job.
3. Cold SGX cycle (stage 4) stays as the fallback for "halt not acknowledged".
4. "Per-engine success needs a kill op" is dropped; per-engine success comes from the firmware-initiated path.
5. First hardware step is an observation, not recovery code (H0): at DxgkDdiResetFromTimeout entry with the
   EXP2324 @4128 looping kernel, record fw_status {halt_count, halted, +0x20} and pending stamps, send the halt
   request, poll the acknowledgement for at most 1 s, record the result in the Wom1GpuReset record. No
   acknowledgement -> only cold can work; acknowledgement -> stage 3H is worth implementing. H1 (three idle warm
   ASC restarts) drops behind H0.

## UPDATE 2026-11-10T19:05:18Z -- architect: k48 grid diagnostic integrated (offline); EXP2326 K1 gap row read

K48G (class D diagnostic, commits ee05c125c + 19de509d8, ledger CHG-20261110-ee05c125-K48G-ARGS and
CHG-20261110-19de509d-K48G-RING):
- Switch APPLE_AGX_D3D12_DIAG_INDIRECT_ARGS=97efffb3 (hex cs_tag, read once; unset = one cached check per
  indirect dispatch, no buffer, copy or receipt).
- Receipt measure-d3d12-indirect-args {cs_tag, x, y, z, source, lag}. source 4 lag 7 = GPU copy of the 12-byte
  argument record, read seven tagged dispatches later without waiting; source 0 = not available, lag is the reason
  (1 slot not written or copy skipped, 2 slot's batch not complete, 3 no ring); 1/2 CPU-mapped heap; 3 CPU expansion.
- Gates on the integrated tree (run before the rebases onto a85dedac2 and ffc730dc8, which changed
  investigation/EXPERIMENTS.md only): baseline only the known NEW; SYNCHECK_OK (syncheck defines
  APPLE_AGX_GPUVA_WINSYS, so the guarded call is compiled). No hardware run yet.
- Arm: one GravityMark HD windowed 100k -temporal 0 PERF arm with the switch; FPS of that arm is not a result.
  Analysis: .local/night-20261110/k48-grid-diag/grid_stats.py <umd log> --tag 97efffb3.
- Verdict rule (unchanged): mean x within 5 % of 1840 groups/frame -> work equals Apple's, the C0 excess is
  per-group cost; at or above 2100 -> the excess is work (owner: producing kernel / culling input).
- NOT VERIFIED: native staging of the slots on hardware; coherent CPU view of the GPU write; copy in the same batch
  as k48; the ring counts tagged dispatches, not executions.

EXP2326 (K1 boot B, Wom1CrossProcessRenderPairs=1) -- gap row of the decision rule, read with the R19 method
(nb.py --dump, q1pos.py; outputs .local/night-20261110/r19-dwm-concurrent/out-q1pos-k1.txt,
out-k1-detect-split.txt, out-k1-work-split.txt). Boot A = EXP2325 (switch unset).

| arm | C2>D>R3 gap p50 A -> B | C4>D>R5 gap p50 A -> B | DWM row excess ms/frame A -> B |
| x   | 1.168 -> 0.831 | 1.171 -> 0.836 | 0.854 -> 0.710 |
| r2  | 1.172 -> 0.846 | 1.219 -> 0.848 | 0.863 -> 0.715 |
| r3  | 1.164 -> 0.843 | 1.153 -> 0.856 | 0.857 -> 0.711 |

- Rule row "gap p50 at or below 1.05 ms": PASS in all three arms (0.83-0.86 ms; predicted 0.83 +- 0.05).
  R1>D>C2 stays 0.87-0.88 ms as predicted.
- Timeline, mean of r2 and r3, A -> B (ms/frame): wall 19.887 -> 19.869 (-0.018); non-busy 1.732 -> 1.632
  (-0.100); busy union 18.156 -> 18.238 (+0.082). Gap by position: R3 0.395 -> 0.297, R5 0.256 -> 0.147
  (K1's target, -0.21 together); C0 0.082 -> 0.127, C2 0.415 -> 0.456, C6 0.058 -> 0.073, R7 0.337 -> 0.362
  (+0.125 together, in components d and c1). Busy: C0 6.184 -> 6.231, the rest within +-0.02.
- Reading: the K1 mechanism works as designed (refusal 55 gone, target gaps -0.21 ms/frame), but the frame does
  not get shorter: +0.125 ms of scattered idle and +0.08 ms of busy appear elsewhere. With one boot per arm
  neither is separable from boot-to-boot variation (C0 busy and component d differ by similar amounts between
  EXP2316, EXP2318 and EXP2325). NOT a measured K1 cost, NOT a measured K1 gain.
- Per-group comparison by DWM position is confounded: GPU work per frame follows the scene (frames without a
  DWM job 12.0 ms busy, D before C2 20.8 ms), so the groups are scene classes. Only the whole-arm numbers are used.
- Decision rule state: suite 119/0, no fault, refusal 55 = 0 per arm, gap p50 PASS, mean(r2,r3) 50.330 vs 50.285
  (+0.09 %), CS gate PASS, m1n1 BUGCHECK code=0x0 at the ordered restart (EXP2326 ACTUAL). Every row of the rule is met.
- Decision: K1 is accepted as FPS-neutral and contract-safe; the default-on flip is a single-variable package of
  its own AFTER EXP2327 and EXP2328 (both already built with the switch unset; they stay as built). That boot is
  also the second K1-on boot: if busy +0.08 and the scattered idle repeat against a same-package unset boot, they
  are K1's and get a cause analysis; if not, they were boot variation.
- Result with a claim is unchanged: EXP2318 FULL 7923 = 79.7 % of macOS (EXP2326 FULL f0 7937 is one run of a
  non-default switch, not a claim).

## UPDATE 2026-11-10T19:21:14Z -- architect: TDR-H0 firmware halt observation integrated (offline, not run)

Commit `119ba99ff` (class D, opt-in). Ledger `CHG-20261110-119ba99f-TDR-H0`. Report `.local/night-20261110/tdr-h0/REPORT.md`.

- Switch: device value `Wom1TdrHaltObserve` REG_DWORD 1, read at runtime start. Unset = the function returns before
  touching anything.
- Off-path difference in every package built from this tip: `Wom1GpuReset` is version 6, 1144 bytes (v5 1064-byte
  prefix + 80-byte halt-observe block, zero when off). `tools/decode_wom1_gpu_fault.py` reads v1..v6.
- On: at `DxgkDdiResetFromTimeout` entry (PASSIVE, backend started, RTKit running, status block mapped, `Resetting`
  taken, worker idle within 1000 ms) send RTKit `0x85<<48 | N` once on endpoint 0x21, poll FW status +0x10 every 1 ms
  up to 1000 ms for N+1, record before/after words (+0x10, +0x14, +0x20, +0x30). No resume, no ring reset; the existing
  reset follows unchanged.
- Endpoint 0x21, not 0x20 (deviation from my brief, verified by me): m1n1 `fw/agx/__init__.py` lists HaltMsg 0x85 with
  DoorbellMsg 0x83 / FWCtlMsg 0x84 on DoorbellEP; Asahi `gpu.rs` sends doorbell and fwctl on EP_DOORBELL 0x21 and
  keeps MSG_HALT commented out; macOS 15 kext send slot loads the endpoint object at +0x120, which is the 0x21 object.
- Worst-case time added inside ResetFromTimeout: about 2.1 s (TdrDdiDelay is 5 s).
- Gates: 11 touched suites 39/0; run_against_baseline = 1195 tests, 1 NEW (the known arch-integrate-only Mu flag test), nothing else.
- NOT VERIFIED: MSVC /W4 /WX build of the KMD change (first compile = the next package build); every hardware
  behaviour; that 13.5 firmware accepts 0x85 and counts halt_count as macOS 15 does.
- Experiment H0 (staged in the handoff, runs after EXP2327, EXP2328 and the K1 default-on package): the EXP2324
  k4128 arm with the switch set; ends in 0x116 by design. Reading: acked -> implement stage 3H (halt, error-complete,
  ring reset, resume); not acked with unchanged words -> one repeat on an idle firmware before any stage 3H work.

## UPDATE 2026-11-10T19:41:14Z -- architect: EXP2327 arm s0 read; N4c-v4 specified (offline, nothing run)

Evidence: `.local/experiments/EXP2327-n4c3-t5b/evidence/EXP2327/gm2327s0-umd.log`, `n4c3-s0.txt`. Spec:
`.local/handoff/N4C-V4.md`. EXP2327's own ACTUAL stays with the bench session.

- N4c-v3 did its job: reason 6 is gone (2996 executions, all reason 8 detail 2; 47936 copies placed, overflow 0).
- Every execution is still refused: mask bit 18 (8/2) 3985 and bit 21 (8/3) 3986 of 3989; all other bits <= 8.
- The 8/2 pair is one and the same: heap 172aa119 (44.7 MB), compute flags 0x101 (C0 root UAV) vs kept render
  flags 0x0c = a vertex-buffer BINDING during R7.
- SOURCE: `JobDrawRanges` records all 16 IA slots of a draw; the owner's draw path and the hardware read only the
  slots the PSO input layout names (`vb_count` = highest referenced slot + 1; Asahi uploads attrib_base only for
  layout buffers). A barrier replay does no GPU work with `Wom1Ov2NoHazardLead` unset.
- HYPOTHESIS (about 90 %, the s0 log has no per-draw slot receipt): the buffer is the asteroid vertex/instance
  buffer left bound in a slot R7's 3-element canvas layout does not name; R7 never reads it. Metal's access sets
  show no R7 -> C0 edge. 8/3 is the application's transition of the same buffer before C0.
- N4c-v4 (class C + D, UMD only, behind the cross switch): (A) the draw access set takes only layout-named VB
  slots; (B) 8/3 narrowed to barriers whose range both the compute and the kept render access, old rule kept as
  receipt-only bit 23; (C) receipts vb-skip, mask-barrier, batch-deps-exact, heap-bo.
- Criterion (g) is replaced by (g'): the Bloom receipt `measure-batch-deps` has bit 0 set for C0 in every s0
  frame, so (g) as written would always stop. (g') uses exact BO handles joined to heap ids.
- Next: one shadow arm s1 in the next package that boots anyway. Mask clean in >= 99 % and (g') clean -> apply arms.
  8/2 persisting with the slot named by the layout -> N4c CLOSED as a true D3D12 dependency, 0.27-0.35 ms leaves
  the reachable ledger. No third range-model variant without new evidence.
- NOT VERIFIED: the slot hypothesis; which BO sets the Bloom bit; anything on hardware. Implementation in progress
  on branch `gpu/n4c-v4` (not integrated).

## UPDATE 2026-11-10T20:12:07Z -- architect: N4c-v4 integrated offline (3e07a3bdc); EXP2327 T5b verdict (not a lever)

N4c-v4 (spec .local/handoff/N4C-V4.md), commit 3e07a3bdc, everything behind Wom1Ov2CrossLead:
- a draw records only the vertex-buffer slots its input layout names (AgxD3d12JobVbSlots; unknown layout keeps all
  16; a register hole keeps slot 0);
- 8/3 needs the barrier range to overlap a compute range AND a kept-render range; the old rule = receipt-only mask
  bit 23; contract: with the kept verdict 0, (mask & ~bit23) == 0 iff the verdict is 0;
- receipts: measure-d3d12-ov2-vb-skip (6), measure-d3d12-ov2-mask-barrier (9, only with bit 23),
  measure-batch-deps-exact (11, execution-first compute batch), measure-d3d12-ov2-heap-bo (3) + summary (2);
  measure-d3d12-ov2-mask-summary now 26 values; measure-batch-deps stays 4.
- Verified offline: run_against_baseline.py (only the known mailbox NEW failure in this worktree), UMD syncheck
  SYNCHECK_OK. NOT verified: MSVC compile of agx_win32_gpuva_batch.c / agx_win32_asahi_scene.c (first package
  build), hardware.
- Consumer note: with the cross switch on the same-execution draw sets are narrower too. The OV5 render-TA verdict
  acts only under Wom1Ov5TaWaiver and the OV2 verdict only under Wom1Ov2NoHazardLead; both stay unset, so arm s1
  is still a pure shadow arm. s1 is invalid if either is set.
- Next: arm s1 (Wom1Ov2CrossLead=1, apply off, HD PERF 10+60 s) in the first package built from a tree that
  contains 3e07a3bdc; stop rule and criterion (g') as in N4C-V4.md section 5. No apply arm before s1 is read.

EXP2327 T5b ABBA, architect reading (tools/agx_gm_perf.py summary, fps_frames; arms t0 tA tAB tAB tA t0, HD PERF
10+60 s; evidence .local/experiments/EXP2327-n4c3-t5b/evidence/EXP2327/gm2327<arm><r>-umd.log):
  arm   r1      r2      r3      mean(r2,r3)
  t0a   49.852  50.008  49.622  49.815
  tAa   50.031  49.919  49.634  49.777
  tABa  49.807  50.592  49.894  50.243
  tABb  50.363  50.083  49.862  49.973
  tAb   49.729  50.246  49.985  50.116
  t0b   49.874  49.924  49.620  49.772
- mean(r2,r3) over both blocks: t0 49.794, tA 49.946 (+0.31 %), tAB 50.108 (+0.63 %). All six runs per arm:
  t0 49.817, tA 49.924 (+0.21 %), tAB 50.100 (+0.57 %). GM frame mean 20.07 -> 19.96 ms (tAB).
- Mechanism receipt moved (t5b-rows.txt: hash 806ee7b1bdb1944a (1,1) -> (25,3) under A+B2).
- Per-job split of the warm runs (.local/night-20261110/t5b-split/REPORT.md, nb.py unchanged, 4 runs per arm,
  ms/frame, tAB - t0; wall = busy + gaps with residual 0.000):
    wall 20.083 -> 19.958 (-0.125) = busy union -0.092 + non-busy -0.034
    C4 busy 2.024 -> 2.009 (-0.015; predicted 0.19-0.30)    R5 busy 1.935 -> 1.906 (-0.029, all in the 3D part;
    predicted 0.17)    C0 -0.025, C2 -0.014, R1 -0.007, R3 -0.007; no row grew beyond its run-to-run range.
- VERDICT T5b (A = CRAWL_BINDFUL, B2 = SRV_STATIC_FIRST): mechanism present (receipt), effect real but 0.044 ms of
  the predicted 0.36-0.47 ms in C4+R5; fps +0.6 % < the 0.7 % flip rule. Class O without the required gain: both
  switches stay opt-in, default off; no default-flip package; T5b is CLOSED as a lever for the macOS gap. The frame
  followed the busy time exactly, so nothing absorbed a saving: the saving itself is not there.
- Consequence for the gap ledger: the rows "C4 0.19-0.30" and "R5 0.17" go back to "no lever" (R8 wording: DXIL
  heavier than Metal, not equal work). The bind-path difference (bindless crawl vs static bind) explains about a
  tenth of them. Do not design a second variant of the same lever (texture/sampler bind form) for C4/R5.
- Rule derived (process): an instruction-form difference against the Metal oracle is not a time prediction. Before a
  shader-form lever gets a package, its size must come from a measured cost of that op class on this GPU (cost
  fixture with the production shape, or a diagnostic arm), not from the oracle diff.
- Result with a claim unchanged: EXP2318 FULL f0 7923 = 79.7 % of macOS 9946.

## UPDATE 2026-11-10T20:14:34Z -- architect: CORRECTION to the 20:12Z T5b verdict scope; EXP2327 FULL f0 below the history range

Read after the 20:12Z note (evidence .local/experiments/EXP2327-n4c3-t5b/evidence/EXP2327/gm2327{ft5,f0}-out.txt):
- FULL pair of EXP2327, same boot, adjacent, order ft5 then f0: ft5 (A+B2) 7811, f0 7689 = +1.59 % for T5b.
  One pair, no ABBA, order not controlled, no UMD log (no per-scene split). The 20:12Z verdict stands for the PERF
  window (10..70 s, both pre-registered rules not met). "CLOSED as a lever for the macOS gap" is NARROWED to that
  window: the FULL score (the claim metric) is OPEN until a same-boot FULL ABBA f0 ft5 ft5 f0. HYPOTHESIS, not
  shown: the scenes after 70 s weigh the planet PS / lighting more. No default flip from one pair.
- FULL f0 of this boot is 7689. Earlier FULL f0 on five boots (EXP2320..EXP2326 packages): 7888, 7991, 7907, 7911,
  7937. So this boot is 2.5-3.8 % below that range, while PERF t0 is only 0.57 % below EXP2320 (49.794 vs 50.08).
  Observed difference of the boot: Wom1Ov2CrossLead = 1 for the whole boot (64 measure-d3d12-ov2-mask-summary rows
  in the t0 logs too), i.e. the N4c-v3 shadow range tracking ran in every arm. HYPOTHESIS: the tracking costs CPU
  where the frame is CPU-bound (fast scenes outside the PERF window). Not separated from other package content.
  7689 is NOT a baseline and not a regression claim; the T5b ABBA is unaffected (same state in every arm).
- Consequence for N4c: the lever is worth 0.27-0.35 ms (about 1.5 %); if the tracking costs 2.5 % of FULL, an
  apply arm cannot win before the tracking cost is removed. s1 therefore also needs FULL f0 on its boot and a
  FULL f0 on a boot of the same package with the cross switch unset.
- Next package (K1 default-on, built from the origin tip): boot 1 cross switch unset = K1 default check, then FULL
  f0 ft5 ft5 f0; boot 2 Wom1Ov2CrossLead = 1 = s1 (PERF r1 + FULL f0).

UPDATE 2026-11-10T20:29:01Z WHOLE-RUN MAP OF THE FULL SCORE (offline, no hardware; report
`.local/night-20261110/full-scene-map/REPORT.md`, raw data EXP2309 nhd FULL HD 7784 with `gm2309nhd-times.txt` and
the session umd.log pid 1548; macOS hd.csv 9946 and split2-hd). Raw per-frame file checked by the architect.
- CORRECTION to the line "GravityMark's camera path advances per FRAME" above: the camera follows benchmark WALL time.
  Three of our runs with 6577..7784 frames switch scene at the same times (30.0 / 59.0 / 89.1 / 111.1 / 139.1 s);
  lag ours vs macOS on the benchmark clock is 0.00 s (correlation 0.964 at 0.25-s bins). R8's 13.6 s lag was our load
  time before the benchmark starts. Scene-aligned comparison = same benchmark second, no shift.
- Where the 2162 lost frames are (9946 - 7784, residual 0): 0..10 s 368 (17.0 %), 10..70 s (PERF window) 736 (34.0 %),
  70 s..end 1058 (48.9 %). Split by kind: steady per-frame gap 2039, ONE startup stall of ours 247, macOS mid-run stalls
  (0.99 s at 123 s, 1.49 s at 131 s) -124.
- NEW ROW, startup stall: benchmark frame 2 takes 4.444 s (frame 3 0.636 s; macOS 0.21 s). Present in all 40 newest runs
  with per-frame data (5.2-10 s of frames >= 100 ms in the first 10 s). In those 4.55 s the GPU ran 62 jobs / 0.13 s.
  Intervals end in measure-gpuva-map-time 1.45 s, measure-gpuva-paging-wait 1.13 s, one measure-scene-escape 0.88 s,
  measure-bo-create 0.48 s. Cause: HYPOTHESIS (resource creation / GPU VA mapping / paging-fence waits in the UMD-KMD
  allocation path). Value if cut to macOS size: +237 frames = +2.4 % of the macOS score, as much as 0.5 ms on every
  frame. The PERF window cannot see it; only a FULL score can. Attribution agent started (`startup-stall/`).
- Shape of the steady gap: 10..70 s is a fixed +4.0 ms (b 1.00); 70..130 s is nearly proportional (a +0.36, b 1.21);
  130 s..end is a larger fixed offset (+4.95). Per scene: s3 +3.1 ms, s0-s2 +4.0, s4/s5 +5.0. The PERF window gives the
  right MEAN gap (+4.04 vs +4.26 ms/frame over 5 s..end) and extrapolates a uniform lever within 2-5 %, but it
  under-weights the render-heavy jobs.
- Per-job excess over Apple, ms/frame, 10..70 / 70..130 / 130..end (EXP2309 package; compare only within one package):
  R5 +0.50 / +0.69 / +1.03; C0 +0.52 / +0.51 / +0.68; R1 +0.24 / +0.32 / +0.45; R3 +0.26 / +0.20 / +0.14;
  C2 +0.24 / +0.16 / +0.11; C4 +0.17 / +0.14 / +0.09; non-busy +1.91 / +1.71 / +1.94 (flat).
  R5 over the three ranges fits ours = 0.22 + 1.185 x Apple (2.003/1.503, 3.553/2.868, 5.417/4.384), nearly all in R5 3D.
- Whole-run lever arithmetic (frames): fixed -0.5 ms/frame +218; -3 % of frame time +244; startup stall to macOS size +237.
- Ranking change (whole run, ms/frame or frames): (1) non-busy ~1.9 ms flat (K1 / PF1 / depth-one rows act here);
  (2) startup stall 247 frames (new, no lever yet); (3) R5 3D +0.72 whole-run, proportional 18.5 % (no lever; bind form
  closed by T5b in the PERF window; survey agent started, `r5-3d-cause/`); (4) C0 +0.56 (grid arm g0 pending);
  (5) R1 +0.31; (6) R3 / C2 / C4 0.14-0.19 each.
- FULL f0 across five boots is 7888..7991 (mean 7927 = 79.7 % of macOS); the single best is 7991 (EXP2321 f0, 80.3 %).
- PROCESS: every FULL run gets `-times <tag>-times.txt` from EXP2329 on, so a FULL score always comes with per-frame
  data (lost frames per range, startup stall length). A lever that acts on startup or on scenes after 70 s is judged on
  FULL (same-boot ABBA), not on the PERF window.
- K1 default-on commit 74469a02c reviewed by the architect: one variable (reader AdmissionPlatformSwitchDisabled:
  absent / malformed = on, DWORD 0 = off), prerequisites unchanged, test tests/test_k1_default_on.py. Hardware check =
  EXP2329 boot 1 as pre-registered.

UPDATE 2026-11-10T20:51:35Z STARTUP STALL ATTRIBUTION AND R5 3D SURVEY (offline, no hardware; two Opus studies, numbers
re-read by the architect in the out files / source where marked V)
- STARTUP STALL, scored frame 2 (EXP2309 nhd, pid 1548, execute 64 -> 65, umd.log 2484249..2491796; outputs
  `.local/night-20261110/startup-stall/out-*.txt`; V = out-analyze-1548-64-65, out-pool-nhd-pre-mid,
  out-paging-nhd-pre-mid, out-bo-totals, out-procs):
  - MEASURED (V): 4.554 s wall = BO creation 3.071 s (32 BOs, 569 MiB: `create us` = pfnAllocateCb 2.014 s,
    `bind us` 1.057 s) + one scene ACQUIRE escape 0.884 s + counted path outside BO creation 0.038 s + no receipt
    0.561 s. GPU work in the window 0.132 s (62 jobs), not on the critical path.
  - MEASURED (V), KMD pool over the load (pre -> mid snapshot): ReserveUs 3.346 s for 573 reservations (2742 MiB)
    against UMD create 3.408 s (95 BOs) -> the create time is the KMD segment-3 reservation (aggregate match 2 %).
    1452 new 2 MiB slabs: 675 from the fast large-page cache in 17.1 ms total (25 us each), 777 by the chunked
    fallback (32 x 64 KiB, after 777 FAST_LARGE_PAGES misses) in 3.921 s (5.05 ms each). Whole boot before this
    run: 6496 chunked slabs, 36.1 s (5.56 ms each).
  - MEASURED (V), bind over the load: UMD bind 4.399 s against KMD UPDATE_PAGE_TABLE build 4.313 s (2349 ops,
    2.54 M entries); inside the build REGISTER_SHARED_BACKING 189428 calls 1.236 s, UPDATE_LEAF 215112 calls
    0.653 s, MAP_AT commits 44950 granules 0.848 s. The split inside the stall window is HYPOTHESIS (scaled).
  - ACQUIRE 0.884 s: MEASURED as the escape's own us field, no GravityMark job in flight. pid 1548 TVB grew once,
    32 -> 1043 blocks (leaseBlocks 1011, V). "The growth takes chunked slabs under the G3 lock" is HYPOTHESIS
    (SlabAllocUs 3.947 s exceeds ReserveUs by 0.60 s).
  - FHD (pid 10436, execute 62 -> 63) has the same shape: create 2.017, bind 1.180, ACQUIRE 0.827, no receipt
    0.623 of 4.696 s.
  - CONTRACT (V, wdk26100 d3dukmdt.h:1484-1486): the MakeResident paging fence is to be synchronized on "before
    submitting the command that uses above resources to the GPU"; a CPU wait at creation is our choice.
  - ARCHITECT READING: one root for the 2.0 s of create and probably most of the ACQUIRE: the chunked slab path at
    5 ms per 2 MiB. The rc93 note measured 0.4 ms per slab call at boot and the cache path costs 25 us, so zeroing
    2 MiB is not the 5 ms; the cost is inside MmAllocatePagesForMdlEx for 32 aligned 64 KiB chunks. Whether it is
    per call or per chunk is NOT separable from the data in hand (HYPOTHESIS either way; Windows-internal).
  - VALUE (55 fps): chunked-slab part 2.0..2.6 s = about +110..+145 frames (+1.1..+1.4 % of the macOS score), bind
    1.06 s about +58 frames, no receipt 0.56 s. Whole stall as before +237 frames.
  - DECISIONS: (S1) offline study started (ledger evidence per call vs per byte, pool locks, rc88 prefill and rc86
    cache behaviour during this load, sketch of a multi-slab MDL allocation, TVB target rule against Asahi's 3x
    used pages, why the granule is 64 KiB). (S2) one driver-neutral measurement specified in the handoff: kernel
    CPU sampling with stacks over the first 25 s of a GravityMark HD launch, after all scored arms of a boot; first
    a 5 s idle pre-check that sampled profiling delivers samples in this guest (PMU under the hypervisor: unknown).
    (S3) candidate, not started: move the UMD bind paging-fence wait (site 1: 0.552 s in the stall, 2.83 s over the
    load) to before the first submit that uses the BO. Upper bound about +0.3 % score, so it comes after the pool.
    The residency wait (site 2) stays as it is: EXP1004 history.
- R5 3D SURVEY (evidence `.local/night-20261110/r5-3d-cause/out-*.txt`; the study's report file was not written,
  its findings are recorded here):
  - Configuration parity with Apple (survey reading, not re-checked): same BC1/BC4/BC7/BC6H assets sampled natively
    and GPU-tiled, colour target uncompressed RGBA16F on both sides, compressed D32 depth loaded, no MSAA, partial
    renders 0, 4 counted ExecuteIndirect draws. R5 3D is the planet PS 428487dae22a5191 (5860 instructions against
    Apple m245 3494: static ratio 1.68, time ratio 1.18). R5 3D = -0.11 + 1.183 x Apple; TA + prelude flat
    0.29..0.31 ms. Closed for R5 with evidence: anisotropy (EXP2048), bind form (T5b), texture format / compression
    / layout, load / store parity.
  - CORRECTION to the R6 note (survey reading): R5 = `dfew c- lZa` (no clear, loads colour and Z), R7 = `dfew cZSa`.
  - NEW CANDIDATE CAUSE. Presence MEASURED / SOURCE (V): GravityMark has one PIPELINE_STATISTICS heap with 3 Begin /
    3 End / 3 Resolve per execution; our owner begins all 11 statistics singles per D3D12 Begin
    (agx_d3d12_windows.cpp AgxD3d12OwnerQuery, heap type 2), slot 7 = PS_INVOCATIONS = D3D12 PSInvocations. While
    that query is active Asahi links a statistics prolog into every fragment shader (agx_state.c:2289;
    agx_nir_prolog_epilog.c:722-741: helper test + one global atomic add), which sets writes_memory, so tag writes
    stay on and triangle merging is off (agx_compile.c:3695, 3807-3810; agx_linker.c:91,115,217). Apple's m245 has
    no atomic. Cost on this GPU: NOT measured -> HYPOTHESIS. Not verified: that the three Begin/End pairs bracket
    R1, R3 and R5 (survey reading of the command order); the arm's reject rule covers it.
  - DECISION: diagnostic arm STATS-NOPS (`APPLE_AGX_D3D12_STATS_NO_PS=1`, opt-in, default off: slot 7 never begun,
    resolve reports PSInvocations 0, nothing else). Implementation started (Opus subagent, own worktree); spec with
    the four sections in the handoff. The switch is a measurement only: reporting 0 breaks the D3D12 contract, so
    it never becomes a default. The production design (exact PSInvocations at lower cost, or none) is decided
    after the cost is known. Per the process rule this lever gets no production package before that number.
- RANKING unchanged in order; row 2 (startup stall) now has an owner: KMD admission pool (chunked slab path), then
  KMD G3 paging + m1n1 broker (per-16 KiB calls), then UMD gpuva (site 1 wait).

UPDATE 2026-11-10T21:01:45Z POOL SLAB STUDY (offline; Opus study, V = re-read by the architect in the named file)
- MEASURED (V, `.local/night-20261110/pool-slab-study/out-slab-intervals.txt`, all 26 EXP2309 receipt snapshots):
  every GravityMark load takes about 1500 slabs (3 GiB) and returns them after exit (slabs held 1516..1556 during
  a run, 93..101 between runs). Per load 370..675 slabs come from the large-page cache (25..33 us each; 263..289 us
  in the first load after boot) and 777..1097 by the chunked path at 4515..7482 us each. So every launch pays
  4.0..6.9 s of CPU in MmAllocatePagesForMdlEx on the application's threads; the scored stall is the part of it
  that falls after the benchmark clock starts.
- MEASURED (V, out-pool-abs.txt): the slab cache was empty at load start (EmptySlabs 1, SpareLimit 64; the 2000 ms
  idle trim had returned the previous run's slabs, CacheDrops 37 -> 38) and the background prefill supplied nothing
  (PrefillSlabs 0, PrefillUs 0): it is opt-in since rc91.
- SOURCE (V, pool_windows.c comment above AdmissionPoolPrefillEnabled): rc91 turned the prefill off because its
  work item shared the system DelayedWorkQueue with the paging worker (DispatchToWorker max 7.8..20 ms before,
  1.06..1.55 s with the prefill; EXP2024 first frames 1.8 s -> 11.7 s). The reason is the shared queue, not the
  idea of allocating ahead.
- STUDY READING (ledger rows collected by the study, not re-read by the architect): the cost is per chunk, not per
  byte and not per call: 75..260 us per chunk for 1, 3, 32 or 16000 chunks per call, and 16 KiB chunks cost the
  same per chunk as 64 KiB chunks (EXP1261); a failed call costs 11 us; zeroing is about 0.2 ms per MiB. Never
  measured: chunk sizes between 64 KiB and 2 MiB, MmCached instead of MmWriteCombined, PREFER_CONTIGUOUS, NO_WAIT.
- SOURCE (study, lock order; spot-checked only): one creation holds the pool FAST_MUTEX across all its slab
  allocations; the VidMm paging worker takes the G3 lock and then the pool lock, and the ACQUIRE escape holds the
  G3 lock across TVB growth. A slab allocation on one thread therefore stalls paging and every G3 escape.
  Whether that chain accounts for the 0.884 s ACQUIRE is HYPOTHESIS.
- SOURCE (study): the TVB target rule equals Asahi's (grow to 3x used pages when 2x exceeds the current size);
  1043 blocks follows from 1390 used pages. No change planned there.
- SOURCE (V, m1n1_windows/src/hv.c:222-245): the hypervisor does not trap TLB invalidation or DC ZVA (no HCR TTLB,
  TDZ, TPU), but every interrupt and every ICC register access is trapped (IMO/FMO, ICH_HCR TC|TALL1). A per-chunk
  inter-processor interrupt to seven cores would therefore be expensive in this guest; whether Windows sends one
  per chunk is NOT known (HYPOTHESIS; numerically 7 x about 20 us would match 140 us per chunk).
- CORRECTION to the UPDATE of 2026-11-10T20:51:35Z: "per call or per chunk is not separable from the data in hand"
  is superseded for the ledger history: across call sizes the cost follows the chunk count. What one chunk costs
  inside Windows stays unknown.
- DECISIONS:
  (P1) Offline study started: static reading of the guest's own ARM64 kernel image with public symbols, to name the
       per-chunk work of MmAllocatePagesForMdlEx (search, zeroing, attribute conversion, TLB or inter-processor
       work) and what FAST_LARGE_PAGES, the chunk size and the cache type change. Behaviour only, no code copied.
       It runs in parallel with the ST-ETW rider and does not depend on sampled profiling working in the guest.
  (P2) Design direction, to be specified after P1: the request shape of the fallback rung (fewer, larger chunks:
       a 2 MiB slab needs every 64 KiB slot aligned and contiguous, which any 128 KiB..1 MiB chunk satisfies), with
       per-rung counters in the Wom1M2PoolDemand receipt so that one application load on hardware gives the cost
       per rung. No package before P1 says which rung can be cheaper.
  (P3) Candidate after P2: allocate ahead on a dedicated thread (not the shared DelayedWorkQueue that rc91 ran
       into) and outside the pool lock. It halves wall time at best and only if Windows does not serialize the two
       allocators; that is a P1 question (locks per chunk).
  (P4) Not pursued: a larger permanent slab cache. It would help a relaunch only, not a first launch after boot,
       and holding gigabytes of system memory idle is not a production design.

UPDATE 2026-11-10T21:11:36Z EXP2328 READING, STATS-NOPS CODE, PROFILE PATH (architect; numbers re-read in the evidence files)
- EXP2328 control: PERF fps_frames w/r2/r3 49.207/50.246/49.919, FULL f0 7874 (47.1 fps), suite 119/0, perf gate
  pass. FULL f0 over six boots without the cross-lead switch: 7888 7991 7907 7911 7937 7874, mean 7918 = 79.6 % of
  macOS 9946.
- PF1 (APPLE_AGX_D3D12_EXPAND_FOLD): NOT TESTED AS A LEVER. In the fold arm (pf1-rx.txt row f1ar2, 3708 receipt
  rows) seals 0, folded kernels 0, refusal reason 1 = 7.521 per present, all other reasons 0; envelopes and prelude
  rows per present unchanged (3.22). SOURCE: reason 1 is AGX_WIN32_FOLD_REFUSE_QUERY, taken from
  agx_win32_expand_queries_allow (agx_win32_expand_ahead.h), which refuses while any pipeline statistic is active;
  GravityMark keeps a PIPELINE_STATISTICS query active around its draws. The restriction came from the older
  expand-ahead mode. fps 45.406 against 45.183/44.984 in that arm is noise of an arm that changed nothing.
  DECISION PF1b (implementation started offline, UMD only, same switch): with a fold the expansion kernel's statistic
  addresses are registered on the compute batch that launches it, and the fold is refused only for predication. A
  statistic value that differs from the unfolded path is not acceptable; if the audit of the resolve paths finds a
  case that cannot be made exact, that case stays refused. The 0.233 ms non-busy row stays open until a fold arm
  with seals > 0 has run.
- k48 grid arm g0: grid_stats.py on gm2328g0-umd.log, 4042 rows: groups per frame x mean 1748.5 (p50 1896) against
  Apple 1839.6 (p50 1872), ratio 0.950, y = z = 1. VERDICT: the amount of k48 work is equal; the C0 excess
  (+0.52..0.68 ms per frame) is cost per group. The causes already closed for k48 stay closed; no new k48 step is
  justified by this result alone.
- STATS-NOPS: code 5f6b746ab (CHG-20261110-5f6b746a-STATSNOPS), class D, default off, never a default. Deviation
  from the first sketch, accepted: Begin begins and at once ends slot 7 instead of skipping it, because Asahi zeroes
  a statistic only in agx_begin_query (agx_query.c:292) and the group storage is not zeroed at creation, so a slot
  that was never begun would resolve stale data. Hardware arm EXP2331 as specified at 2026-11-10T20:51:35Z.
- Sampled CPU profile in the guest: NOT AVAILABLE (wpr -start CPU fails with 0x80070032, no profile interrupt source
  under the hypervisor). The ST-ETW rider is withdrawn. The per-chunk cost of the pool goes through P1 (kernel
  reading) and then a timing diagnostic inside the KMD.
- Bench order now: EXP2329 (K1 default-on + T5b FULL ABBA) -> EXP2330 (cross-lead receipts) -> EXP2331 (STATS-NOPS)
  -> PF1b fold arm when its commit is gated -> TDR-H0.

## UPDATE 2026-11-10T21:27:02Z -- architect: P1 kernel reading (per-chunk attribute conversion), POOL-PROBE specified, PF1b integrated offline

Nothing run on hardware. Sources: static disassembly of the guest's own kernel (ntoskrnl.exe ARM64 10.0.26100.9711,
ntkrnlmp.pdb {287D4F3D-731D-E7A3-AA2D-C7DD8F8AEF76}, identity checked against guest dumps), listings and scripts in
`.local/night-20261110/ntos-mdl-alloc/` (out-dis-*.txt, out-evidence.txt, out-dump-kvars.txt). I re-read the key
listings myself; the items marked SOURCE below are the ones I verified.

**P1 result (answers decision P1 of the 21:01:45Z entry).**
- SOURCE: with MM_ALLOCATE_REQUIRE_CONTIGUOUS_CHUNKS, MiAllocateSkipPagesForMdl (0x39cae8) loops and calls
  MiFindContiguousPagesEx (0x393550) once per chunk (call at 0x39cbd8, back edge at 0x39cc70). A 32 x 64 KiB slab is
  32 separate contiguous-memory searches.
- SOURCE: each MiFindContiguousPagesEx creates and deletes an ultra thread context and, unless flag 0x2000 is set
  (tbnz w19,#0xd at 0x393b38), calls MiConvertContiguousPages (0x433160) with the requested cache type. The skip path
  does not set 0x2000 (0x39cb94 sets only 0x60000000); MiAllocateMostlyContiguousPagesForMdl (PREFER_CONTIGUOUS
  without REQUIRE) sets it (0x39c70c) and converts once per call in the batch.
- SOURCE: the conversion reaches KxFlushEntireTb (`tlbi vmalle1is`, 0x29c61c) once per chunk and
  MiFlushCacheForAttributeChange per page (`dc civac` loop at 0x43364c, then `sys #0,c7,c1,#0` = IC IALLUIS at
  0x43367c).
- Dump values (MEMORY-2118.DMP, MEMORY-2100.DMP, boots of 2026-11-03): KiTbSendIpiForBroadcastFlushes = 0;
  entire-TB flushes due to attribute change 29,407 and 29,107; per-page cache flushes 846,398 and 867,987
  (3.2..3.3 GiB of pages, about one GravityMark load's worth).
- Subagent report, not re-verified by me: zeroing is once per call (MiInitializeMdlBatchPages, `dc zva`), and a page
  is zeroed again when its attribute changes to or from non-cached/write-combined (mask MiState+0x1418 = 5); the
  free path (MiFreePagesFromMdl) leaves the attribute in the PFN; chunk size does not select another algorithm;
  MmAllocateContiguous* converts once per block and adds a system-VA mapping.

**CORRECTION to the 21:01:45Z entry.** My working guess there that the per-chunk cost is interprocessor interrupts is
not supported: this kernel does broadcast TLB invalidation in hardware (`tlbi ...is`) and
KiTbSendIpiForBroadcastFlushes is 0 in both dumps.

**What P1 does not settle (HYPOTHESIS level).** How the ~150 us per chunk splits between (1) the attribute
conversion (per chunk: entire-TLB flush on all vCPUs; per page: 64 DC CIVAC + one IC IALLUIS, plus re-zeroing),
(2) ultra context set-up and tear-down, (3) the top-down PFN search with page trades. The cycle cost of
TLBI VMALLE1IS and IC IALLUIS under m1n1 is not derivable offline. One existing observation argues against a pure
"conversion" reading: the chunked cost is 4.5..7.5 ms per slab on every load of a boot, not only the first, although
the free path keeps the write-combined attribute; either the next load does not get the same pages back or the
conversion is not the dominant part.

**Why not simply allocate MmCached.** The slab MDL is never the CPU view in the demand design (the CPU view maps the
hypervisor window IPA write-combined), so the PFN cache attribute looks unused. It is not: (a) lease zeroing
(AdmissionPoolZeroLease) maps the slab pages with MmMapLockedPagesSpecifyCache(MmWriteCombined) "as they were
allocated" (Microsoft: the mapping cache type must match the allocation); (b) pages Windows zeroed through a cached
mapping can leave dirty cache lines that are written back after the GPU or the write-combined view wrote the page;
the per-page flush in the conversion is exactly what removes them. A cached allocation therefore needs its own
flush design and is not a drop-in change. It stays a measurement arm only (pages never mapped, freed at once).

**POOL-PROBE (class D diagnostic, in implementation by a subagent; own boot; no production behaviour change).**
REG_DWORD `Wom1PoolAllocProbe`=1: once per boot, at the first production slab that falls to the 64 KiB rung, run 12
arms of 8 x 2 MiB MmAllocatePagesForMdlEx calls (hold 8, free 8), receipt `Wom1PoolAllocProbe` v1 with per arm
Calls, Ok, SumUs, MinUs, MaxUs, GranulesOk, Contig2M, Reused (granules whose first frame the previous arm also had),
FreeUs. Order: B1 B2 (MmCached 64K) | A1 A2 (WC 64K) | B3 | C1 (WC 256K) | B4 | C2 (WC 1M) | B5 | D1 (WC,
PREFER_CONTIGUOUS without REQUIRE) | B6 | A3. The cached arms between the write-combined arms return reused pages to
the cached attribute, so every WC arm converts again; their own time, where Reused is high, is the write-combined ->
cached direction (TLB flush without cache flush).

WHY THIS HYPOTHESIS: (1) the listings show a full conversion per chunk on the production rung and none per chunk on
the FAST rung, and the FAST rung costs 25..33 us per 2 MiB against 4.5..7.5 ms; (2) the dump counters show the
conversion count scales with chunks (29 k entire-TB flushes per boot); (3) the per-load repetition of the cost is
the one observation that does not fit, which is why the probe measures instead of a fix being built.

WINDOWS CONTRACT: MmAllocatePagesForMdlEx returns zeroed pages with the requested cache attribute; with
REQUIRE_CONTIGUOUS_CHUNKS every chunk is SkipBytes long and SkipBytes-aligned; pages may only be mapped with the
cache type they were allocated with. Cost of the call is not specified.
AGX/ASAHI CONTRACT: the GPU needs 16 KiB-aligned contiguous backing per UAT page; the pool hands out 64 KiB
granules; Asahi takes its memory from shmem and has no equivalent per-chunk cost. No GPU state is touched by the
probe.
TRANSLATION: unchanged in the probe. The production choice the probe decides is the request shape of the fallback
rung (chunk size ladder) and how much allocate-ahead (P3) has to hide.
WHAT IS STILL UNKNOWN: the split of the per-chunk cost under this hypervisor and memory state, and whether larger
chunks (256 KiB, 1 MiB) are found at load time without long page trades. Windows-internal, hardware only.

Pre-registered reading (b = mean per-call time of B1,B2; a = A1; c1 = C1; c2 = C2):
- a >= 2 b: conversion dominates. Then c1 <= 0.5 a means the per-chunk part dominates and the fix is a chunk ladder
  on the fallback rung (largest size that stays cheap: 1 MiB, 256 KiB, then 64 KiB), built as its own package and
  judged on FULL with -times (startup window). c1 > 0.75 a means the per-page part dominates: no request shape
  helps, only P3 (allocate-ahead on a dedicated thread) remains, and a cached-allocation design needs its own
  four sections.
- a < 1.3 b: conversion is not the cost; c1 and c2 against a say whether the per-call search cost falls with fewer
  calls (ladder) or not (P3 only).
- A2 with Reused >= 50 % and A2 <= 0.5 A1: the attribute is sticky and reuse is cheap; then the per-load repetition
  means production does not get its pages back, recorded as a finding, no action by itself.
- A3 against A1 is the drift bracket: if they differ by more than 30 % the boot is not readable and is repeated.
- D1 is read only for GranulesOk and Contig2M (is a non-REQUIRE result usable at all) and its time.
Size of the prize: the chunked rung costs 4.0..6.9 s of CPU per GravityMark launch against a startup loss of 247
frames (about 4.1 s); a 3x cheaper rung is worth roughly +1.5 % of the macOS FULL score, an upper bound until
measured.

**PF1b integrated offline (code `ea2bb34ba`).** With APPLE_AGX_D3D12_EXPAND_FOLD=1 the launching compute batch Wb is
registered as a writer of the five IA/VS/clipper statistics (agx_get_query_address on Wb as well as on the render);
only predication still refuses the fold, as reason 1; EXPAND_AHEAD keeps its restriction; switch off is unchanged
(existing byte-identity test passes). Review: the resolve paths go through Asahi's writer-generation helpers, which
do not assume a render writer; no owner or D3D12 code changed. Gate: run_against_baseline 1201 tests, 1 NEW (the standing mu-flag case), new fold test green. The builder syncheck does not compile
these two headers; their first MSVC compile is the next package build. NOT verified: that GravityMark now takes
folds, that the statistics read back are exact on the GPU, FPS effect. Bench arm: same table and receipts as
EXP2328 PF1 (first check seals > 0, refusal reason 1 = 0), plus the statistics values of the fold arm against the
no-fold arm of the same boot.

Bench order after EXP2329: EXP2330 (cross-lead receipts) -> EXP2331 (STATS-NOPS) -> PF1b fold arm -> POOL-PROBE
boot (when its commit is gated) -> TDR-H0.

## UPDATE 2026-11-10T21:39:21Z -- architect: POOL-PROBE code integrated (a90f6e193), decoder f5c6c99fe; ready for its own boot

Nothing run on hardware. The probe specified in the 21:27:02Z entry is on origin: KMD code `a90f6e193`, receipt
decoder `tools/agx_pool_alloc_probe.py` `f5c6c99fe`. Gate: run_against_baseline 1201 tests, 1 NEW (the standing
mu-flag case). I reviewed the diff: the trigger is reachable only through Slabs.NewSlab with the pool fast mutex
held (the unlocked prefill calls AdmissionPoolAllocSlab directly and cannot trigger it); the allocation and free
calls are the ones production already makes under that lock; the receipt is staged under the receipts spin lock and
written by the existing worker with a flush. Arm table, field order and switch are as specified.
NOT verified: MSVC /W4 compile (first is the package build); the probe's real duration under the pool lock
(estimate 0.5..2 s, once, inside the GravityMark load); whether the PREFER_CONTIGUOUS arm (SkipBytes 0) returns
pages at all on this kernel (its Ok and GranulesOk fields answer that).
Two details of the implementation that matter when reading the receipt: GranulesOk applies the production
alignment and consecutive-frame check without the physical-limit test (the allocation bound already enforces it);
Reused counts 64 KiB-granule first frames shared with the previous arm, so it is at most 256 and is 0 for arm 0.
The reading rules of the 21:27:02Z entry are unchanged and are applied to the decoder's POOL_PROBE_RATIOS line.

## UPDATE 2026-11-10T21:43:37Z -- architect: allocate-ahead (P3) is not a free branch -- prefill history read; amendment to the POOL-PROBE reading made before any probe data

Source: ledger and code digest of the rc86..rc114 pool prefill (EXP2018, EXP2019a, EXP2022, EXP2024, EXP2025, EXP2060,
EXP2061; pool_windows.c 1296-1372, 1396-1416, 1604-1630; render_pool_slab.c 29-89; paging_windows.c 527-532). The
decisive lines were re-read in EXPERIMENTS.md (EXP2060 ACTUAL) and in the code before this entry.

What the record says:
- Allocating slabs ahead on another thread has been run on hardware twice. rc88/EXP2022: bo-allocate 5.3 -> 2.8 s,
  score unchanged; EXP2024: D3D11 2529 -> 2279 with the prefill item and the paging worker on one DelayedWorkQueue
  (rc91 turned it off). rc114 moved the paging worker to CriticalWorkQueue; EXP2060 with Wom1PoolPrefill=1 on that
  build: bo-allocate 3.23 -> 2.08 s, bo-bind-paging 2.24 -> 3.54 s, stall 8.29 -> 8.15 s, paging worker body max
  21 -> 223 ms. Sum of the two stall parts 5.47 -> 5.62 s: the work moved, it did not shrink. Verdict then:
  "prefill rejected again".
- The existing prefill already allocates outside the pool lock and adopts under it; it never takes the G3 lock. So
  "outside the lock, on its own thread" is not a new variable against EXP2060.
- HYPOTHESIS (not measured): the P1 kernel reading gives EXP2060 a mechanism. Each 64 KiB chunk of the chunked rung
  issues an entire-TLB invalidate that is broadcast to every CPU (tlbi vmalle1is) and a per-page cache flush. A second
  thread doing this while the paging worker runs costs the paging worker its TLB about 32 times per slab. That would
  explain a slower worker body with no queueing.
- Memory floor: growth must stop under LowMemory and be handed back on pressure reclaim (EXP2033e, EXP2061b; rc104,
  rc109, rc115). The spare cache is 64 slabs (128 MiB) with a 2 s idle trim; a load asks for about 1450 slabs.

AMENDMENT to the pre-registered POOL-PROBE reading (entry 21:27:02Z; made before the probe boot, no probe data exists):
- The branches that ended in "only P3 (allocate-ahead on a dedicated thread) remains" are withdrawn in that form.
  Moving the unchanged chunked allocation to another thread is EXP2060 again and gets no package.
- Allocate-ahead may come back only on top of a cheaper per-slab allocation that the probe has shown (chunk ladder
  arm C1/C2 at <= 0.5 of A1, or a cached form with its own flush design), and then its package must report
  bo-bind-paging, paging worker max and score next to bo-allocate, as EXP2060 did.
- If the probe shows no cheaper form (c1 > 0.75a, c2 > 0.75a and a < 1.3b), the allocation-cost line stops there and
  the remaining question for the startup stall is the number of slabs a load asks for (accounting below), not their
  placement on threads.

Next offline step (no hardware): account for the ~1450 slabs (2.9 GiB) of one GravityMark HD load from existing
receipts and logs -- which allocation classes ask for them and whether any class is backed at creation without
being used. Read-only study started; result will be recorded here.

## UPDATE 2026-11-10T21:56:52Z -- architect: EXP2329 read (K1 default-on rows, late-boot out-of-memory = guest servicing, not a pool leak); slab demand accounted; quiet-guest validity gate

Evidence dir `.local/experiments/EXP2329-k1on-a/evidence/EXP2329/`; raw result in EXP2329 ACTUAL (bench owner).
Receipt fields read with the v15 layout of `render_pool_demand.h`; xproc receipts parsed as JSON.

### K1 default-on check (74469a02c) -- rows against the pre-registered text of EXP2329 BEFORE
- RenderKicked > 0, RenderFailed 0: MET. b0 considered 787, kicked 760, failed 0; whole boot considered 21448,
  kicked 21322, failed 0, fell_back 0, refused_other 0, refused_same_vm_slot 123.
- "refusal 55 = 0", receipts "as EXP2326" (EXP2326 method, entry 2026-11-10T18:45:23Z, "K1 boot A": per PERF arm from pre/post receipts):
  MET for the PERF arms w/r2/r3 (count 1 at post-b0 and still 1 at post-gm, post-x and final = 0 per PERF arm).
  Whole boot: 1, in arm b0 (last_refusal code 55, fence 18658), kind "compute -> render", capability_off 0,
  no_process 0. That pair kind is outside K1's class and keeps refusal 55 by design (render_job_window.h:100).
  It is recorded as a fact, not as a K1 failure and not as a pass of a whole-boot row.
- fps_frames mean(r2,r3) within +-0.7 % of 50.33: NOT MET by the letter. w/r2/r3 50.688 / 50.941 / 50.472,
  mean(r2,r3) 50.707 = +0.748 %, on the fast side.
- suite 119/0: MET. BUGCHECK 0: MET (STOPS 0).
- Verdict by the pre-registered rule ("Fail of any = the K1 default flip is reverted (new row), not patched"): one
  row is not met, so the default flip 74469a02c is REVERTED (bench owner: git revert, new CHANGES rows). K1 stays
  the accepted opt-in (`Wom1CrossProcessRenderPairs`=1), contract-safe and FPS-neutral as recorded at EXP2326.
  No second default-on attempt is planned: the flip has no measured gain, so it returns only when a lever needs it.
- The rule is not reinterpreted. Process finding for future default flips: the band was narrower than the spread of
  the control boots (mean(r2,r3) 50.08 EXP2320, 50.285 EXP2325, 50.330 EXP2326, 50.08 EXP2328, 50.707 here =
  1.25 % range). A band for a default flip is derived from the measured spread of at least three control boots and
  is written one-sided when only a loss is a failure.
- Packages already built with K1 default on (EXP2330, EXP2331, EXP2332) run as built; each BEFORE states the K1
  state of its package. Comparisons inside one boot are unaffected.

### Late-boot defect: device removed at 24 s of FULL ft5b -- cause is memory pressure from guest servicing
- Sequence: f0a 7977 (uptime 13m29s), ft5a 7678 (17m50s), ft5b "D3D12::error(): out of memory" at 23.9-24.0 s then
  device removed (22m21s), f0b 7246 (24m29s); CS warm/r2/r3 35.659 / 44.808 / 49.208 -> perf gate cs16 FAIL -10.28 %.
- Pool receipt Wom1M2PoolDemand at pre-b0 / post-b0 / pre-gm / post-gm / post-x / final: Slabs 82 / 92 / 89 / 89 /
  102 / 100, ReservedBytes 162 / 165 / 157 / 153 / 167 / 167 MiB, PeakReserved 2979 MiB. The pool returns to the
  same level after every process: NO leak between process lifetimes.
- Final receipt: ReserveFailures 5, FloorRefusals 5, all reason 1 (LOW_MEMORY); PressureWaits 1 (2050 ms, failed),
  PressureFastFails 4; HeadroomRefusals 0, NewSlabFailures 0, CommitFailures 0. LastReserveFailure: request 49 MiB at
  2709 MiB reserved, LowMemory 1, HighMemory 0, TimeMs 1363949. So the memory floor (rc104/rc109/rc115) refused
  growth under the system low-memory condition, the application got E_OUTOFMEMORY and removed the device. The driver
  followed its contract; no bugcheck, no hang.
- procs-final: process sum 3821 MB with TiWorker 920 MB (15855 handles), TrustedInstaller, wuaucltcore and
  MoUsoCoreWorker present. EXP2328 (2820 MB) had none of them; EXP2327 (2898 MB) had MoUsoCoreWorker only;
  TiWorker is absent in EXP2322..EXP2328. One GravityMark load needs about 2.9 GiB of pool on a 6.69 GB guest.
- Reading: Windows Update servicing started in the guest during the boot and took about 1 GB plus CPU and disk.
  This is an inference from the end-of-boot process list and the LOW_MEMORY refusals; the start time of servicing
  inside the boot is not yet read (bench owner fetches the event-log times read-only).
- Under that pressure the chunked slab cost rose from 5.7-7.4 ms to 11.7 ms per slab, SlabAllocUsMax 64 ms -> 2.5 s,
  ReserveUsMax 0.76 s -> 4.9 s.
- NOT judged from this boot: the T5b FULL rule (one ft5 lost, ft5a and f0b under pressure), any FULL baseline
  (f0a 7977 is inside the 7874..7991 range but its quietness is unproven; it is not added to the mean), the CS gate
  of the package (FAIL stands as recorded; rerun = the CS gate of EXP2330, same package, on a quiet guest; a second
  FAIL on a quiet guest stops the package).

### Process rule (new): quiet-guest validity gate, from EXP2330 on
- Before the first arm and before every FULL arm the probe saves a process snapshot (`procs-<tag>.txt`, format of
  procs-final) and the available physical memory.
- An arm is valid only when none of TiWorker, TrustedInstaller, wuauclt*, MoUsoCoreWorker, usoclient, DismHost,
  msiexec, CompatTelRunner is running. Otherwise the probe waits (60 s steps, 20 min bound). Still present after
  the bound: the FULL and gate arms are skipped, the boot is recorded INVALID-ENV and is not counted. The probe
  never kills servicing processes and never changes update settings.
- Pausing Windows Update on the bench guest is a system setting of the user's machine: offered to the user, not
  changed by the sessions.

### Slab demand of one GravityMark HD FULL launch (offline, EXP2329 arm x; closes the "slab count" question of 21:43:37Z)
- Reservation +2756.1 MiB in 571 allocations; slabs 89 -> 1528 (643 contiguous, 825 chunked).
- The application's own D3D12 objects are 2627.4 MiB = 1314 slabs = 91 % of the 1439 new slabs: committed buffers
  1409.3 MiB in 67 objects (largest 256, 2 x 210.9, 128, 109.9, 2 x 85.4, 60.3, 49.1 MiB), upload buffers 480 MiB in
  6 objects, committed textures 565.3 MiB in 27 objects, placed textures 110.1 MiB in 32 objects, shader-visible
  descriptor heaps 60.75 MiB, readback 2.1 MiB. FULL arms run -Asteroids 100000.
- Driver-internal: UMD-internal about 128.7 MiB (64 slabs), TVB private leases 126.4 MiB (63 slabs; TvbMaxPages
  peaks once at 1390 pages, steady 769 pages = 24 MiB, growth rule is Asahi's), free slots 10 slabs.
- No object is backed twice. Everything is reserved and committed at creation (WDDM residency at creation).
- Reading: the slab count is the application's request; there is no driver-side double count to remove. TVB lease
  trimming is worth at most about 50 slabs (3 %) and gets no package of its own.
- Still open on this line: (a) cost per slab = POOL-PROBE (staged); (b) HYPOTHESIS, not measured: part of the
  1409 MiB of committed buffers is never referenced by a GPU batch in the first seconds or at all (the largest
  buffers do not scale with the asteroid count), and macOS backs buffers by demand-zero pages. The measurement is a
  per-BO first-GPU-reference census (class D receipt, UMD winsys, rides on a later package, no boot of its own).
  A deferred-commit lever is designed only if the census shows a large never-referenced or late-referenced share.

### Bench order now
EXP2330 (same package, Wom1Ov2CrossLead=1: s1 PERF r1 + FULL f0 + CS gate; GO behind the quiet-guest gate) ->
EXP2331 (STATS-NOPS) -> EXP2332 (PF1b) -> POOL-PROBE boot -> T5b FULL ABBA rerun (f0 ft5 ft5 f0 with -times on a
quiet all-default boot; may share the POOL-PROBE package as a separate boot) -> TDR-H0 -> FS-D1.

## UPDATE 2026-11-10T22:33:12Z -- architect: EXP2329 servicing times confirmed; Windows Update pause approved by the user; buffer first-reference census (receipt-only, own switch) and its reading rule

### EXP2329 follow-up (bench owner, EXPERIMENTS.md "EXP2329 ACTUAL 2026-11-10T22:24:43Z")

- The servicing reading of the late-boot out-of-memory is now timed, not inferred: guest System log shows Windows
  Update downloads at 21:29:40Z-21:29:54Z and Windows Modules Installer activity 21:30:41Z-21:44:27Z, inside the
  FULL arms; again 22:21Z-22:24Z after the restart. Pending-reboot keys after recovery: all False.
- The first rollback was preempted by a servicing restart; the rerun at the proxy ended RECOVERY_DONE; package,
  service absent, devnode inert (read-only check by the bench owner).
- K1 default flip reverted as its rule requires: b81265c7d (code), 80736842a (CHANGES rows). Reviewed: the revert
  restores `AdmissionPlatformSwitchEnabled(Wom1CrossProcessRenderPairs)`; K1 stays opt-in.

### Bench environment: Windows Update pause (user decision)

- Asked: «Поставить Windows Update на паузу в стендовом госте?». User: "да, на паузу".
- Mechanism (Microsoft Learn, Update Policy CSP and Windows Update client policies): string values
  `PauseQualityUpdatesStartTime` / `PauseFeatureUpdatesStartTime` (yyyy-mm-dd) under
  `HKLM\SOFTWARE\Policies\Microsoft\Windows\WindowsUpdate`; 35 days or until cleared; Pro / Enterprise / Education.
- Handed to the bench owner (handoff 2026-11-10T22:26:49Z). NOT applied yet at this stamp. Applied only on an idle
  guest, verified by `PausedQualityStatus` / `PausedFeatureStatus` at a later boot. No process kill, no service
  change. The quiet-guest gate stays.

### Buffer first-reference census (class D, receipt-only)

- Code: 127a235a7 (receipts `measure-bo-born`, `measure-bo-first-ref`, `measure-bo-census`; tool
  `tools/agx_bo_life.py`; test `tests/test_umd_bo_census.py`) and 1b9f68f6f (own switch).
- On only when the process has both `APPLE_AGX_UMD_TRACE_FILE` and `APPLE_AGX_D3D12_BO_CENSUS=1`. Default off: a
  traced launch without the switch writes the same trace lines as before, so scored arms stay comparable across
  packages. Off cost: one branch per accepted batch and one per buffer creation.
- Verified: builder compile gate SYNCHECK_OK for the owner file; test gate 1203 tests, 1 NEW = the Mu-flag test that fails only in this checkout (known, unrelated), 0 other NEW; the three winsys C files
  are compiled by MSVC for the first time in the next package build (NOT verified here; the same initializer and
  container-of forms already exist in agx_win32_gpuva_batch.c).
- Review notes: every batch reference comes from `add_bo` and is `&windows_bo.Gpuva`, so the container-of in
  `AgxWin32AsahiCensusRefs` is valid; buffers are zero-allocated, so a buffer born with the hook unset is never
  pending.

WHY THIS HYPOTHESIS (deferred backing): (1) the startup stall (247 frames) is owned by the pool's slab path and 91 %
of the 2756 MiB reserved at startup is the application's own objects, 1409 MiB of it in 67 committed buffers
(accounting of 2026-11-10T21:56:52Z); (2) macOS backs such buffers with demand-zero pages, so memory a GPU job never
names costs it nothing at creation. Unknown: how much of our startup reservation no GPU batch ever names.

Reading rule (written before any census data exists):
- Arm: one extra GravityMark launch in the FULL configuration (-Asteroids 100000, HD), with the trace and the
  census switch, run after every scored arm of its boot, behind the quiet-guest gate. Its score is not used.
- N = MiB of buffers of at least 1 MiB with origin 1 (new allocation; cache reuse lines are excluded) whose class
  is an application object (committed buffer / committed texture / heap / placed or reserved) and that have no
  first-reference line while the process lived.
- N is an upper bound of what deferred backing could save: a buffer no batch names may still be written by the CPU
  or read by the kernel copy path, and then it needs its pages anyway.
- Estimate of the bound, labelled as an estimate (uniform cost per MiB assumed): frames = 247 * N / 2756.
- N < 550 MiB (bound under 50 frames, about 0.5 % of the macOS score): the deferred-backing hypothesis is CLOSED.
- N >= 550 MiB: no design yet. Next step is offline: for the ten largest never-referenced objects read the existing
  map / upload / copy receipts to see whether the CPU or the copy path touches them. A deferred-commit design is
  written only for the part that nothing touches, and only if that part alone is >= 550 MiB.
- Buffers first referenced late (any bucket) are NOT a gain: deferring them moves the same slab cost into the
  scored scenes (allocate-ahead of unchanged slabs is closed, EXP2022 / EXP2060).
- Whatever N is, the per-class table goes into the ledger as recorded; no other conclusion is drawn from it.

### Bench order

EXP2330 (running at this stamp) -> EXP2331 -> EXP2332 -> POOL-PROBE boot with the package rebuilt from the tip
(census arm last) -> T5b FULL ABBA rerun -> TDR-H0 -> FS-D1.

## UPDATE 2026-11-10T22:44:54Z -- architect: Windows Update pause written on the bench guest; EXP2330 invalid

- The pause approved by the user ("да, на паузу") was written at 2026-11-10T22:42:24Z: two REG_SZ policy values, start date 2026-11-10, read back. Guest is Windows 10 Pro, build 26200. Full record: `investigation/EXPERIMENTS.md`, entry `ENV-CHANGE 2026-11-10T22:42:24Z`. This replaces the "NOT applied yet" line of the 22:33:12Z entry.
- Effect NOT verified: both client status values were still 0 right after the write. Done means both read 1 at a later boot and the next experiment boot logs no update download. fork 6 saves the status query as `wu-pause-status.txt` at the first quiet gate of the next two boots.
- The pause ends by itself on 2026-12-15. Undo = delete the two values.
- EXP2330 is INVALID-ENV (ACTUAL in 7de424a8c): the guest reset itself after the ordered restart, before any arm, with update downloads at 22:37:58-22:39:38Z in the System log. The quiet gate cannot catch this case because it runs only once the experiment boot is reachable. No data from EXP2330 is used.
- Bench order: EXP2334 (the EXP2330 package again, pinned source 6b6f71668) -> EXP2331 -> EXP2332 -> EXP2333 (package rebuilt from the tip, census launch last) -> T5b FULL rerun -> TDR-H0 -> FS-D1.

## UPDATE 2026-11-10T22:54:29Z -- architect: update pause taken by the client

- Both client status values read 1 about 12 minutes after the write, same boot. Record: `investigation/EXPERIMENTS.md`, `ENV-CHANGE follow-up 2026-11-10T22:54:29Z`.
- Still open: no update download in the System log of the EXP2334 boot.

## UPDATE 2026-11-10T23:11:11Z -- architect: EXP2334 reset is undetermined, not servicing-proven and not K1+CrossLead-proven; the Windows kernel changed after EXP2328

Evidence read: `.local/experiments/EXP2334-k1on-s1-rerun/evidence/full-at-reset.log`, the `full.log` of EXP2309..EXP2334, m1n1 `src/hv_exc.c` (bench line, 06616be7).

- The experiment boot of EXP2334 reset about one hypervisor period after the KMD started (retained root active, 32 scanout swaps, three long broker holds at 43998..44701 ms, then silence until the reset). Every hypervisor status before it is 0. The hypervisor side shows no fault.
- `BUGCHECK ok ... code=0x0` is NOT evidence of a clean restart on the current guest. m1n1 reads KiBugCheckData at a fixed RVA 0xdbc9c0 pinned to ntoskrnl 26100.9711 (`hv_exc.c`, reset handler). The reset call site moved from elr+0x635138 / x30+0x497e04 (every ordered restart EXP2309..EXP2328) to elr+0x6430a8 / x30+0x49bab4 (EXP2329 recovery log, EXP2330, EXP2334): the Windows update replaced the kernel, so the pinned RVA reads an unrelated word.
- All 21 ordered restarts scanned have an xHCI "IRQ route disabled" line right before the reset. The EXP2334 reset has none. It looks like a bugcheck, not an ordered restart. Labelled inference until the guest log is read.
- `EXP2330-k1on-s1/full.log` is not the EXP2330 experiment boot (0 retained operations, ordered-restart shape: it is the recovery relaunch). The EXP2330 experiment-boot log is lost. The "servicing" reading in the EXP2330 ACTUAL is therefore unproven, and so is "the same reset twice".
- EXP2330 and EXP2334 are the first two package boots on the new Windows build. EXP2329's arms ran before the kernel was swapped (inference from the servicing times; to be confirmed from the guest's update history).

Open hypotheses, none ranked above the others without the dump:
- A: K1 default-on together with Wom1Ov2CrossLead=1 (EXP2327 had the switch without K1 and booted; EXP2329 had K1 without the switch and booted, on the old kernel).
- B: the new Windows build with our KMD, any package.
- C: a servicing restart.

Discriminator, offline first: System log events 41 / 1001 / 1074 / 6008 of that boot, dump files and their write times, kernel file version and update history; cdb on a new dump if one exists. Handed to fork 6 (message of 2026-11-10T23:11Z). No package runs until this is read. If the guest holds no bugcheck record, the boot of EXP2331 (K1 on, no CrossLead) is the single discriminator between A and B: reset inside the first 120 s = B, stop every package; clean boot = B rejected.

Consequences already fixed:
- The FULL baseline mean 7918 and the control fps rows were measured on the old Windows build. The first FULL f0 on the new build is a new control and is not averaged into the old mean.
- Process: after SSH_NEVER_UP the launcher log is copied to `evidence/full-at-reset.log` before any recovery (it was overwritten in EXP2330).
- m1n1 diagnostics, two defects for the m1n1 line, no package depends on them: (1) the bugcheck reader must verify the kernel identity (PE TimeDateStamp and SizeOfImage) and print "unknown kernel" instead of a code; (2) in a boot with the GPU powered down the reset handler takes a data abort in m1n1 reading the ASC outbox at 0x206408114 and loses the stack print (EXP2330 recovery log).

## UPDATE 2026-11-10T23:29:49Z -- architect: EXP2330 and EXP2334 are both 0x116; the hung job follows the last cross-process render pair; next boot = same package line with K1 off by registry

Evidence read: `.local/experiments/EXP2334-k1on-s1-rerun/evidence/{sys-reset-events.txt,dumps.txt,kernel-id.txt,rec2334h/kmd-receipts-pre-disable.txt}`, `.local/experiments/EXP2330-k1on-s1/evidence/rec2330h/kmd-receipts-pre-disable.txt`, `.local/experiments/EXP2329-k1on-a/evidence/EXP2329/kmd-receipts-{pre-b0,pre-suite,pre-gm,final}.txt`, `git show 6b6f71668:drivers/apple-agx/render-admission/src/backend_platform_windows.c` (K1 reader), `render_job_timing.h` (slot layout and flags).

Verdicts:
- Hypothesis C (servicing restart) is REJECTED for both boots. WER 1001 records a bugcheck each time: EXP2334 `0x116 (0xffff978310627010, 0xfffff803e8a41ec0, 0xffffffffc0000483, 0x3)`, EXP2330 `0x116 (0xffffaa828b2aa2d0, 0xfffff8031fac1ec0, 0xffffffffc0000483, 0x3)`. The "invalid: servicing reset" reading in the EXP2330 ACTUAL (7de424a8c) is wrong; fork 6 appends the correction. The m1n1 log of the EXP2330 experiment boot is lost; the guest minidump `C:\Windows\Minidump\111026-6750-01.dmp` still exists.
- P2 is AppleAgxRenderAdmission.sys+0x1ec0 in every 0x116 of this project: it does not discriminate. P3 = 0xc0000483: the reset failed because the KMD quiesce timed out. Firmware alive and silent in both boots (three resets each, all 0xc0000483).
- The guest runs ntoskrnl 10.0.26100.9974 (KB5132688 and KB5132061 installed 2026-11-10). Two package boots on this kernel, two 0x116.
- Event-log `Date:` values are guest local time printed with a "Z"; not usable for sub-minute timing.

Decoded job-timing rings (QPC 24 MHz, times relative to the oldest slot of the ring):
- EXP2334: 262 timed jobs in the boot. Fences 0x30f (pid 1268, the DWM-like process) + 0x310 (pid 5392) are the single K1 pair of the boot; both complete (147.39 / 147.62 ms). Then only submit-only slots, about 53 ms with no firmware job, then 0x31d (pid 1268): submit 200.82, worker 205.31, kick 205.41, no first progress, never completes. Its successor 0x31e is the SAME process and context: begin 4.85 ms (normal about 0.03 ms), kicked, never completes. The hung pair is not a K1 pair.
- EXP2330: 1395 timed jobs. K1 class considered 30, kicked 11, refused same-vm-slot 19. Last K1 pair 0x849 (pid 1284, DWM-like) + 0x84a (pid 5800), completes. Three more jobs of pid 5800 complete (last at 294.44 ms), about 41 ms idle, then 0x877 (pid 1284): submit 335.36, kick 336.01, never completes; lone head, no successor.
- Common shape: the hung job is the first render of the DWM-like process after the last K1 pair in which that process was the lead, after an idle gap of 40 ms or more. First ResetEngine failure at 14.8 s (EXP2334) and 19.2 s (EXP2330) of boot QPC.
- Correction to the hand-over wording "hung right at the first GPU work of the boot": the receipts show fence 797 after 86 depth-two kicks in EXP2334 and fence 2167 in EXP2330. Not the first GPU work.
- Old kernel 26100.9711, EXP2329 (K1 default on, Wom1Ov2CrossLead unset): K1 pairs kicked 9 at the idle desktop (pre-b0), 762 pre-suite, 763 pre-gm, 21,322 at the end, no hang. K1 alone on the old kernel survives desktop pairs followed by idle.

Open causes (labelled hypotheses):
- A': K1 pairs on the new kernel. A'': K1 pairs together with Wom1Ov2CrossLead=1 (its receipt is all zeros in the EXP2334 boot, so the switch never acted). B: the new kernel alone, K1 proximity a coincidence.
- No mechanism is derived for A'. `decode_ddi_status.py` is a failure-status table and shows no power-transition evidence. Offline pass on B stopped (anti-loop).

Decision:
- EXP2331 as staged (K1 on, CrossLead off) does NOT separate A' from B: both predict the hang. It is not run in that form.
- Next boot: the EXP2331 package (pinned source 6b6f71668) with `Wom1CrossProcessRenderPairs = 0` (REG_DWORD) set at staging and `Wom1Ov2CrossLead` unset. The 6b6f71668 reader is `!AdmissionPlatformSwitchDisabled(Context, L"Wom1CrossProcessRenderPairs")`, so the value 0 turns K1 off in that package. Against the last known-good configuration (K1 off, CrossLead off, old kernel) the single changed variable is the Windows build.
- Pre-registered reading. (a) Boot, suite and GravityMark complete with `Wom1GpuReset` resets 0 and xproc `render_pairs.kicked` 0: B is rejected, K1 is a necessary condition for the hang on this kernel, K1 stays off (already REJECTED as a default, the tip is opt-in), the STATS-NOPS arm proceeds in that boot and its FULL f0 is the first control on the new build. EXP2332 (same pinned source) gets the same value. The value is removed in rollback. (b) 0x116 again with `render_pairs.kicked` 0: K1 is not necessary, the cause is B or another one; no further package, the target becomes "first render after idle hangs on the new kernel".
- The K1+CrossLead package gets no third run. K1 default-on gets no second attempt unless a lever needs it; the K1 after-effect mechanism is therefore not pursued now.
- In-run checks: at the first qgate read `Wom1GpuReset` and `Wom1CrossProcessPairsReceipt`; on any failed boot save kmd-receipts and copy full.log before recovery.

Other facts:
- The kept `C:\Windows\MEMORY.DMP` of EXP2324 was overwritten by Windows when the new bugchecks wrote their dumps. The EXP2323/EXP2324 minidumps are on the Mac.
- Bench order after this boot: EXP2332 (K1=0) -> EXP2333 (rebuilt from the tip, K1 opt-in unset, census launch last) -> T5b FULL rerun -> TDR-H0 -> FS-D1.

## UPDATE 2026-11-10T23:33:48Z -- architect: correction to the UPDATE of 2026-11-10T23:28Z (package source of EXP2331 / EXP2332)

- The EXP2331 package is built from f5a3fc4b1 and the EXP2332 package from 05afd0ee6, not from 6b6f71668 as written there. Checked: 6b6f71668 is an ancestor of both, `git diff 6b6f71668 <commit> -- drivers/apple-agx/render-admission drivers/apple-agx/windows` is empty for both, and both carry the reader `!AdmissionPlatformSwitchDisabled(Context, L"Wom1CrossProcessRenderPairs")` (line 9585). The decision and the reading rules are unchanged.
- EXP2331 is launched with `Wom1CrossProcessRenderPairs = 0` (BEFORE 72b4affc3). EXP2330 correction and EXP2334 ACTUAL are in 081c068c7; the EXP2330 minidump is at `.local/experiments/EXP2330-k1on-s1/evidence/exp2330-0x116-mini.dmp`.

## UPDATE 2026-11-10T23:56:46Z -- architect: EXP2331 boot with K1 off does not hang; its quiet gate failed by a tool defect of mine (fixed); GravityMark arms not run

Evidence read: `.local/experiments/EXP2331-stats-nops/evidence/EXP2331/{qgate.txt,wu-pause-status.txt,kmd-receipts-pre-suite.txt,rcpt-final/Wom1JobTiming971.bin,state-end.txt}`, `full.log`, two read-only guest queries at 23:54Z / 23:55Z.

- Quiet gate: every poll of the first-arm gate read "no-answer" for 20 minutes and the probe ended as "INVALID-ENV servicing still running". That verdict is VOID. Cause, reproduced: the quoted PowerShell query of `quiet-gate.sh` broke in cmd.exe (the regex "|" was taken as a pipe: "'TrustedInstaller' is not recognized"). The guest was quiet: no TiWorker by tasklist, fixed query answers `BUSY= AVAIL_MB=3740`. Fix: the query is sent as -EncodedCommand; an empty answer after 20 minutes now ends as `QGATE <tag> TOOL-FAIL` (return 2) instead of a servicing verdict. Installed in fork 6's scratchpad and in `.local/scratchpad-tools-backup/quiet-gate.sh`. The gate had never run to a live guest before this boot (EXP2330 and EXP2334 died earlier).
- K1 was off in this boot: `Wom1CrossProcessRenderPairs = 0` in the receipts, K1 class considered 0 / kicked 0, 18 cross-process candidates refused with code 55.
- No hang: the DDI failure table at 55 s has no ResetEngine entry (the hung boots failed at 14.8 s and 19.2 s). The final job-timing ring counts 37,615 timed jobs up to fence 0x2ec8; its last slots are renders of dwm (pid 1244) and explorer (pid 5244) one per minute over the 20 idle minutes, each after an idle gap of about 60 s, all completed. The suite completed.
- Reading so far: the form of B written down before the run ("first render after idle hangs on the new kernel, with no K1 pair") is contradicted by this boot. The full reading (a) needs the GravityMark arms, which did not run. One clean boot against two hung ones; the K1-necessary verdict is recorded only after the arms complete with resets 0.
- Update pause: both Paused*Status values are 1 in this boot (`wu-pause-status.txt`). The System-log half of the criterion is still open.
- `wer-final.txt` reports 2 files in C:\CrashDumps; their dates are not read yet (fork 6 lists them in the ACTUAL).
- Next: fork 6 reruns the arms with the fixed gate, same package and the same value 0 (same boot if it is still up, else a new ID). No other change.

## UPDATE 2026-11-11T00:31:12Z -- architect: EXP2331 verdicts: K1 is a necessary condition of the hang on 26100.9974 (B rejected); first control on the new Windows build is lower; STATS-NOPS shows no R5 3D effect in the PERF window

Evidence read (files, not reports): `.local/experiments/EXP2331-stats-nops/evidence/EXP2331/{qgate.txt,kmd-receipts-checkpoint.out,kmd-receipts-post-t0a.out,kmd-receipts-post-t1a.out,kmd-receipts-post-t1b.out,kmd-receipts-post-t0b.out,kmd-receipts-final.txt,wer-final.txt,perf-gate.txt,tool-suite-std.txt,nops-valid.txt,gm2331f0-out.txt}`, the UMD logs `gm2331{w,r2,r3,t0a,t1a,t1b,t0b}-umd.log` through `tools/agx_gm_perf.py summary` and `tools/agx_gpu_frame_breakdown.py --jobs`, and fork 6's EXP2331 ACTUAL (01b58b6d9), which agrees with the files.

### 0x116: pre-registered reading (a) applies
- K1 off by registry for the whole boot (`K1_CHECKPOINT render_pairs considered/kicked 0 0`). `GPU_RESET none` at the checkpoint and after each of the four PERF arms; no ResetEngine entry in the final receipts; `STOPS 0`; suite 0 FAIL; w/r2/r3, four PERF arms, FULL f0 and the CS bench all completed after the 20 idle minutes.
- Verdict: hypothesis B (the new build alone hangs the first render after idle) is REJECTED. On ntoskrnl 26100.9974 the K1 cross-process render pair is a necessary condition of the firmware hang seen in EXP2330 and EXP2334. Weight: one clean boot against two hung ones, each hung within 20 s of GPU work.
- Decision: K1 stays off. The tip already reads it as opt-in (b81265c7d, 80736842a); packages built before the revert (EXP2332 = 05afd0ee6) get `Wom1CrossProcessRenderPairs=0`. The mechanism (what a K1 pair leaves behind for the lead process's next render after idle on the new kernel) is not derived and is not pursued unless a lever needs K1.
- `wer-final.txt` says `WER_REPORTS 2`, `STOPS 0`; the dates of the two files in C:\CrashDumps are still not read.

### New Windows build: first control numbers (one boot, not averaged with the old build)
- FULL f0 = 7722 (46.2 fps) = 77.6 % of macOS 9946. Old-build mean of six boots: 7918.
- Control fps_frames w/r2/r3 = 49.537 / 48.894 / 48.927, mean(r2,r3) = 48.910. Old-build control boots: 50.08 .. 50.71. CS r2/r3 50.684 / 50.444, PERF_GATE_PASS.
- Where the loss sits (r2/r3 of EXP2325, EXP2326, EXP2328, EXP2329 against EXP2331, same tools):
  - GravityMark frame time per 7.5 s block of the PERF window: old mean 21.00 21.30 23.28 23.71 12.81 17.23 18.33 21.22 ms; new 21.34 21.73 23.68 24.07 13.93 18.25 18.85 21.67 ms. Every block is above the old maximum; +0.34 .. +0.52 ms in the GPU-bound blocks, +1.0 .. +1.1 ms in the two light blocks (5 and 6).
  - GPU work per job is unchanged: compute exact 13.46 / 13.48 ms (old 13.45 .. 13.59), main 3D 1.575 / 1.586 (old 1.581 .. 1.611), big 3D 1.796 / 1.797 (old 1.799 .. 1.851).
  - Median completion-detect per frame 1.319 / 1.404 ms against old 0.887 .. 1.313 (mean 1.107).
  - Startup anchor 19.0 s / 17.0 s against old 15.7 .. 16.7 s.
- Reading: the loss is not in GPU work. It is on the CPU / kernel side of the frame (largest where the frame is not GPU-bound). HYPOTHESIS (labelled, not proven): the new kernel build or the state of this one boot changed CPU-side cost or completion-detect latency. One boot cannot separate build from boot. No band, no default flip and no lever verdict is derived from cross-build comparisons; same-boot ABBA stays valid.
- Rule for the next boots (appended, the old rule is not edited): the 49.1 floor of the probes was derived on the old build. Until three control boots on 26100.9974 exist it is recorded but is not a skip criterion; validity is the quiet gate, `GPU_RESET none` and the arm validity rows. Every next boot runs w/r2/r3 and FULL f0 as new-build controls.

### STATS-NOPS (`APPLE_AGX_D3D12_STATS_NO_PS=1`, class D measurement)
- Validity: stats-no-ps rows 0/1/1/0. PERF ABBA fps_frames t0a 47.844, t1a 49.196, t1b 49.693, t0b 49.125. t0a is the low outlier of the boot (its detect median is 1.668 ms against 1.020 .. 1.395 in the others).
- Per-job medians, positions 5 / 3 / 1 = R5 / R3 / R1 3D (t0a, t1a, t1b, t0b): R5 1.698 1.668 1.666 1.676; R3 1.102 1.075 1.078 1.083; R1 0.801 0.800 0.799 0.798. Pair deltas (t1 - t0): R5 -0.031 and -0.010 ms; R3 -0.027 and -0.005 ms; R1 0.000.
- Against the pre-registered rule: expected -0.1 .. -0.4 ms on R5 3D if the cause were real; both PERF pairs are below the 0.04 ms reject threshold. The second clause (FULL mean(fn) - mean(f0) < +0.5 %) is NOT measured: the probe skipped fna/fnb by the floor rule. Status: first reject clause met, verdict open until the FULL arms run. They get no boot of their own; they ride as two centred FULL arms in the T5b FULL boot (order f0a, ft5a, fna, fnb, ft5b, f0b). The switch stays off by default either way.
- Side observation, not a lever: R1 span is +0.17 .. +0.22 ms in the NOPS arms with unchanged 3D and TA (its to_ta grows by 0.08 .. 0.11 ms). Not explained; not pursued unless the FULL arms show an effect.

### Next on the bench (fork 6)
EXP2332 (PF1b, package 05afd0ee6, `Wom1CrossProcessRenderPairs=0`) -> EXP2333 POOL-PROBE rebuilt from the tip (K1 opt-in unset, `fcensus` arm last) -> T5b FULL boot with the two STATS-NOPS FULL arms -> TDR-H0 -> FS-D1. In the next boot one read-only line more: `powercfg /getactivescheme` and the processor min/max state, to exclude a power-policy change by the update.

## UPDATE 2026-11-11T01:09:17Z -- architect: EXP2332 reading: PF1b fold acts, one prelude per frame stays through an over-wide ORDER re-check of an already sealed pair (PF1c in implementation); second new-build control 7680; CS gate run invalid (Defender page-in burst)

Evidence read (files): `.local/experiments/EXP2332-pf1b-fold/evidence/EXP2332/{pf1-rx.txt,qgate.txt,perf-gate.txt,powercfg.txt,crashdumps-dir.txt,gm2332f0-out.txt,procs-final.txt,cs-warm,cs-r2,cs-r3}`, the UMD logs `gm2332{p0a,p1a}-umd.log` (rows `measure-d3d12-counted-fold`, `measure-d3d12-counted-path`, submit rows in record order), the EXP2331 CS files for comparison, and the package source at 05afd0ee6. fork 6's EXP2332 ACTUAL is not on origin yet; its message agrees with the files except one label (below).

### PF1b (class O, `APPLE_AGX_D3D12_EXPAND_FOLD=1`, package 05afd0ee6)
- The pre-registered stop ("v5 envelope in steady state" in a p1 arm) tripped after p1a: `PF1_RX p1a ... prelude_per_frame 1.091 STOP v5-envelope`. p1b, p0b and the FULL ABBA were not run. Rule applied as written: no FPS verdict. Single arms p0a 48.433 and p1a 49.558 fps_frames are one pair and are not a claim.
- What PF1b changed (per present, p0a -> p1a): compute envelopes 4.284 -> 4.372, plain v3 renders 1.141 -> 3.353, v5 prelude envelopes 3.211 -> 1.091. Query refusal (reason 1) is 0. Two of three renders per frame fold completely.
- Fold counters per present in p1a (meanings from the enum in `agx_win32_expand_ahead.h`): seals 3.279, kernels 6.559, deferred hazard flushes 47.0, refusals NOT_WRITER 3.28 and ORDER 1.092, seal drops by cause OTHER 0.778, RENDER 1.409, RESOURCE 1.092. Correction of a label in fork 6's message: the 1.092 drop is cause 5 RESOURCE (explicit resource flush), not HAZARD (cause 6 = 0).
- Which draw keeps the prelude (record-order rows of p1a): the shadow-map render (4096 x 4096, DSV) has two counted draws with the same PSO hash e17b536e. Draw 1 seals Wb and folds. Draw 2 passes the candidate, then `agx_win32_fold_prepare` repeats `agx_win32_fold_render_writes(ctx, wb, render)` although Wb is already sealed for this render, gets true, refuses with ORDER (path row 0x107), and its refusal path flushes Wb for the arguments (`agx_flush_writer`, the RESOURCE drop). The render is then submitted as a v5 envelope. The other two renders do not hit this: their later counted draws fail the candidate with NOT_WRITER and fold through the existing link (`agx_win32_fold_linked`), which has no such re-check.
- Reading (derived from source, not from a hardware run): the re-check is wider than the contract needs for a sealed pair. After the seal R becomes the recorded writer of BOs in Wb's list only through hazards that `agx_win32_fold_defers` defers (47 per present), and the design already expects that state (`agx_win32_fold_follows` lets Wb's rc105 scan skip R). The only inputs a later kernel reads are the argument and count BOs; any writer of those other than Wb is flushed by `agx_win32_stream_flush_inputs` before prepare runs. Which BO makes the check true at draw 2 is NOT identified (candidates: the shadow map as a heap-reachable read of Wb written by R at batch init; heap-reachable UAVs that `bind_heaps` marks as written by the draw's batch). The fix does not depend on it.
- Decision: PF1c = the ORDER check applies only at the first seal (`agx_win32_fold_owner(wb) != render`). An implementer is on it in an isolated worktree with an adversarial check of the order argument first and a RED/GREEN case in `tests/test_d3d12_indirect_stream_glue.py`. No rerun of package 05afd0ee6 with a relaxed stop. The PF1 ABBA is repeated only on a PF1c package, with the same stop rule (PF1c predicts 0 v5 envelopes and 0 ORDER refusals in steady state).

### Second control boot on ntoskrnl 26100.9974 (K1 off)
- Suite 119/0, `GPU_RESET none`, STOPS 0, quiet gates `first-arm BUSY= AVAIL_MB=3760`, `full-f0 BUSY= AVAIL_MB=4567`, Update pause status 1/1.
- FULL f0 = 7680 (46.0 fps) = 77.2 % of macOS 9946. Control fps_frames w/r2/r3 = 49.195 / 49.092 / 49.799, mean(r2,r3) = 49.445.
- Two new-build boots now: FULL 7722 and 7680 (mean 7701 = 77.4 %); PERF mean(r2,r3) 48.910 and 49.445. Old-build mean of six boots: 7918. A third control boot is still needed before any band.
- Power policy (read-only, end of boot): Balanced (381b4222-f694-41f0-9685-ff5bb260df2e), processor min state 5 % AC/DC, max 100 % AC/DC. There is no reading from the old build to compare with, so this neither supports nor rejects the labelled hypothesis of a CPU-side change.
- The two files in C:\CrashDumps are dwm-live983.dmp (2026-10-06 22:48) and explorer-live984.dmp (2026-10-06 23:20): old, not from these boots. That open check is closed.

### CS 1.6 perf gate: FAIL recorded, run judged invalid
- `GATE PERF_GATE cs16_benchmark FAIL worst=44.907 baseline=49.943 delta=-10.08%`. Warm/r2/r3 = 46.337 / 44.907 / 48.573; EXP2331 on the same Windows build: r2/r3 50.684 / 50.444.
- `cs-r2/cs-bench-mem.csv`, mid-run row: pages_in 19635/s, cache faults 35616/s, top page-faulting process MsMpEng.exe 31673/s. The same row of EXP2331 r2: pages_in 0, MsMpEng 303/s. EXP2332 r3 mid-run: pages_in 74.9/s, top steamwebhelper 735/s. hl.exe CPU seconds 63 / 63 / 59 against 59 / 59 in EXP2331.
- Reading: r2 ran during a Defender page-in burst and is not a measurement of the driver. r3 is 2.7 % under the baseline with mild foreign activity; it is not cleared by this file. The package differs from EXP2331's only by PF1b code that is off without the switch, and no default changed in this boot, so nothing is reverted.
- Consequence of the stop rule: no package is accepted on this result. The next boot re-measures CS warm/r2/r3. Validity rule for CS runs from now on (derived from the quiet rows of EXP2331: pages_in 0 .. 4/s, foreign top faulter 303 .. 1455/s): a run whose mid-run csv row shows pages_in above 1000/s or a foreign process above 10000 page faults/s is rerun once and the disturbed run is recorded, not gated. If two valid runs of the next boot are again more than 2 % under the baseline, CS is bisected (f5a3fc4b1 against 05afd0ee6) before any other package. Defender and every other guest security setting stay untouched.

### Next on the bench (fork 6)
EXP2333 POOL-PROBE from the tip as specified (K1 opt-in unset, `fcensus` last, floor print-only, w/r2/r3 + FULL f0 controls = third new-build control, CS warm/r2/r3 with the validity rule above) -> PF1c package: PF1 ABBA with the unchanged stop rule -> T5b FULL boot with the two STATS-NOPS FULL arms -> TDR-H0 -> FS-D1.

## UPDATE 2026-11-11T01:32:59Z -- architect: PF1c on origin (8d3f43d32), software-verified only

- PF1c (class O, part of PF1, still behind APPLE_AGX_D3D12_EXPAND_FOLD=1, default off): commit
  8d3f43d32d8b6491edb6bcf9c1f5448aeede8df0. agx_win32_fold_prepare checks the order
  (agx_win32_fold_render_writes) only at the first seal of a pair; later counted draws of
  the same render fold into the sealed compute batch.
- Verification so far: RED/GREEN case in tests/test_d3d12_indirect_stream_glue.py;
  run_against_baseline 1204 tests, 1 NEW = the known checkout-only Mu-flag failure; an
  independent attempt to break the order argument (points a-e of the handoff section
  2026-11-11T01:09:17Z) found no counter-example. Not checked: MSVC compile of the winsys
  header (first at the package build); an exhaustive audit of projection scripts that
  replace rsrc->bo while a pair is sealed (expected harmless: a new handle is not in the
  sealed batch's list). No hardware result.
- Still unknown and not needed for the fix: which BO made the repeated check true at the
  shadow render's second counted draw.
- Next for PF1: package from this tip after EXP2333; PERF ABBA p0a p1a p1b p0b and FULL ABBA
  with the unchanged v5 stop; validity rows for p1 arms: order refusals 0 per present,
  RESOURCE seal drops 0, v5 prelude envelopes 0 in steady state, seals about 3.28 and
  folded kernels about 7.65 per present. No FPS claim for PF1 exists yet.

## UPDATE 2026-11-11T01:45:42Z -- architect: EXP2333 reading: pool cost is per chunk (256 KiB rung = POOL-LADDER), deferred backing CLOSED, new-build control band, CS valid

Evidence: `.local/experiments/EXP2333-pool-probe/evidence/EXP2333/` (pool-probe-post-f0.txt,
bo-life.txt, fcensus-umd.log, kmd-receipts-pre-gm/post-f0.txt, gm2333f0-times.txt, perf-gate.txt);
EXPERIMENTS.md `EXP2333 ACTUAL 2026-11-11T01:42:11Z`.

POOL-PROBE, read by the rule of UPDATE 2026-11-10T21:27:02Z (rule unchanged):
- Validity met (Version 1, TriggerSlabChunked 1, 8 calls per arm). The probe fired in the warm launch
  (value already present in kmd-receipts-pre-gm.txt), so FULL f0 7596 did not contain the 0.53 s lock hold.
- a1/b = 1.00 -> branch "a < 1.3 b": the cached -> write-combined conversion is NOT the cost.
  Reuse does not matter either: B2 reused 119 pages, A1 took 103 of B2's cached pages, A2 reused 0;
  all three cost 5.6-5.8 ms per 2 MiB.
- c1/a1 = 0.31 (256 KiB chunks, 1751 us, all 256 granules pass the slab check), c2/a1 = 0.60 (1 MiB,
  3332 us, min 66 / max 9892: bimodal, not cheap) -> the cost falls with fewer chunks: about 175 us
  per 64 KiB chunk, about 219 us per 256 KiB chunk. By the rule this is the chunk ladder, and the
  largest size that stays cheap is 256 KiB.
- Drift bracket a3/a1 = 1.27 < 1.30: readable by the rule. The cached arms drift upward through the
  probe (5610 -> 6532 -> 7084 -> 8601 -> 7607 us); against its neighbours C1 is 0.26, C2 0.42. Same reading.
- A2 clause (Reused >= 50 %) not applicable (0/256). D1 (PREFER, no REQUIRE) returns no usable granule
  (GranulesOk 0); its 440 us bounds zeroing plus one conversion per 2 MiB.
- Verdict: per-chunk search cost CONFIRMED as the owner of the chunked slab cost; conversion and
  reuse direction REJECTED as owners. Lever: POOL-LADDER (below).

Where the chunked path sits in the scored run (new, from Wom1M2PoolDemand deltas and the census launch):
- Per FULL f0 launch (pre-gm -> post-f0): 1463 new slabs = 773 fast 2 MiB (20 ms in total) + 690 chunked
  (4 373 082 us, 6.34 ms each). Every launch gets about 750 fast slabs (1.5 GiB) before the fast path
  misses; peak 1547 slabs. Chunked ms per slab over the three new-build boots: 5.59 / 6.20 / 6.38.
- Census launch, same clock for BO births and presents: new BO bytes pass 1.5 GiB at 6.1 s; first
  benchmark frame at 13.2 s; the gap after it (13.59 -> 18.34 s, 4.75 s) creates 36 BOs / 587 MiB =
  about 293 chunked slabs = about 1.86 s. The 667 MiB created in the 7.76 -> 13.20 s gap is before the
  first benchmark frame (not scored).
- Frame-1 -> frame-2 gap of the scored FULL f0, three control boots: 4.881 / 5.472 / 5.079 s
  (mean 5.144, sd 0.30).
- Prize of the ladder: 293 slabs x (6.34 - 1.75) ms = about 1.35 s of the scored window = about 62
  frames = +0.8 % of our score = about +0.6 % of the macOS score. This replaces the "+1.5 %" upper
  bound of the 21:27:02Z entry: more than half of the chunked time is before the first benchmark frame.
- Rest of that gap in the census launch (traced, 383 MB log): intervals ending in gpuva-paging-wait
  0.82 s, in bo-create 0.49 s, one 854 ms counted-path -> scene-escape interval. Not attributed further.

Census, read by the rule of UPDATE 2026-11-10T22:33:12Z: valid (296 born rows, no out of memory).
N = never-referenced application classes with origin 1 = committed-buffer 166.6 + committed-texture
16.1 + placed/reserved-texture 19.1 + heap 60.2 = 262.0 MiB < 550 MiB -> the deferred-backing
hypothesis is CLOSED. (290.0 MiB in the ACTUAL entry is the all-class total incl. driver General 1.4
and cache-reuse 26.6; same verdict.) No further hardware on deferred backing or slab count.

New-build control band (ntoskrnl 26100.9974, K1 off, defaults; EXP2331 / EXP2332 / EXP2333):
- FULL f0 7722 / 7680 / 7596: mean 7666 = 77.1 % of macOS 9946, sample sd 64.
- PERF mean(r2,r3) 48.910 / 49.445 / 49.189: mean 49.181, sd 0.268.
- Band (mean +- 2 sd): FULL f0 [7538, 7794], PERF mean(r2,r3) [48.65, 49.72]. Limitation: n = 3; a
  95 % prediction interval at n = 3 is about +- 5 sd, so the band is a screening line, not a test.
  One-sided use: a package below 7538 or 48.65 stops for a rerun; a single boot above the band is not
  a gain without same-boot ABBA or a direct metric.
- The old-build numbers (7918 mean, 49.1 PERF floor) are not comparable and take no part in verdicts.

CS 1.6: EXP2333 r2 50.979 / r3 50.872, both VALID by the disturbance rule, PERF_GATE PASS (+1.86 %).
The EXP2332 CS FAIL was the disturbed run; no CS regression, no bisect.

Ranking (whole run) after this entry: (1) non-busy about 1.9 ms per frame; (2) startup: frame-2 gap
4.9-5.5 s, of it chunked pool about 1.9 s (POOL-LADDER, about +0.6 % of macOS), remainder unattributed;
(3) R5 3D +0.72; (4) C0 +0.56; (5) R1 +0.31; (6) R3/C2/C4. PF1c (EXP2335) is running next.

POOL-LADDER (class O, KMD, one variable; spec in OV-ARCHITECT-HANDOFF.md, same stamp):
REG_DWORD Wom1PoolLadder=1 inserts a 256 KiB-chunk attempt between the fast 2 MiB attempt and the
64 KiB attempt of AdmissionPoolAllocSlab. Default off. Pre-registered metrics for its boot
(switch on for the whole boot, controls = the three boots above):
- M1 non-fast cost per slab in FULL f0 = (delta of all non-fast attempt us incl. failed 256 KiB
  attempts) / (delta of slabs that missed the fast path). Control 5.59 / 6.20 / 6.34-6.38 ms.
- M2 frame-1 -> frame-2 gap of FULL f0. Control 4.881 / 5.472 / 5.079 s.
- M3 frames in the first 10 s after the first frame. Control 201 / 179 / 194.
- ACCEPT (default on in the next package) only if M1 <= 3.2 ms AND M2 <= 4.29 s (control minimum
  minus the control range 0.59 s) AND FULL f0 >= 7538 AND PERF mean(r2,r3) >= 48.65 AND perf gate
  PASS AND new_slab_failures 0. REJECT if M1 > 4.7 ms or M2 > 4.88 s. Anything between: one more
  boot, then decide. FULL score alone decides nothing here (expected +62 frames is under one sd).

## UPDATE 2026-11-11T01:48:44Z -- architect: addendum to UPDATE 2026-11-11T01:45:42Z: frame-2 gap split from the receipts' own durations

The "rest of that gap" line of the 01:45:42Z entry used intervals between rows. The receipts carry
their own durations (field meanings from the emitter comments: measure-bo-create
{MiB, class, allocation us, GPU VA bind us, association us}, measure-scene-escape {operation, us, ...});
this split replaces that line. Census launch of EXP2333, gap 13.59 -> 18.34 s (4.75 s):
- BO creation, 36 rows / 575 MiB: allocation 1.97 s (3.4 ms per MiB = 6.8 ms per 2 MiB, i.e. the
  chunked slab rate) + GPU VA bind 1.16 s (2.0 ms per MiB; of it MakeResident residency 0.40 s in
  10 measure-gpuva-map-time rows, bind paging-fence waits 0.48 s in 14 rows).
- One scene escape, operation 1, 1280x720: 0.85 s. Present once in every traced launch of the three
  control boots: 0.76 .. 1.13 s (12 launches). It is longer than the largest single pool reserve of
  the boot (ReserveUsMax 0.77 s), so it is not one pool reserve; its content is unknown.
  Research started (read-only): `.local/night-20261110/scene-acquire-cost/REPORT.md`.
- Remainder about 0.77 s (uploads, copies, 69 submits).
POOL-LADDER addresses the 1.97 s allocation part (expected saving about 1.4 s, same as the
01:45:42Z estimate). Whether the bind part also falls with 4x fewer physical runs per slab is
unknown; the ladder boot shows it in measure-bo-create of the traced r2 launch. Metrics and
ACCEPT / REJECT lines of the 01:45:42Z entry are unchanged.
EXP2335 keeps no post-f0 receipt dump (probe already running when asked); the fourth control for
M1 comes from its final dump only as a per-slab rate, for M2/M3 from gm2335f0a/f0b-times.txt.

## UPDATE 2026-11-11T02:06:04Z -- architect: POOL-LADDER implemented (098a9c60b), software-verified, default off

Commit 098a9c60b: `Wom1PoolLadder=1` adds the 256 KiB rung (2 MiB fast -> 256 KiB -> 64 KiB), back-off 16 slab
allocations after a failed 256 KiB attempt, receipt `Wom1M2PoolDemand` v16 (808 bytes; decoder reads v7-v16).
Switch off = the previous attempts exactly. Reviewed by the architect: attempt order, both call sites update the
back-off under the pool lock, the POOL-PROBE trigger ignores 256 KiB slabs. Tests: pool suites pass except one
checkout-only error, baseline gate 1 NEW = the known checkout-only case. Not verified: MSVC /W4 /WX compile
(package build), any hardware effect.
Decoder fields for the boot: `pool_ladder.nonfast_ms_per_slab` is cumulative; M1 of the 01:45:42Z entry is the
same formula on the pre-gm -> post-f0 deltas: (delta SlabChunkedUs + delta Rung256FailUs) / delta SlabChunked.
Bench order: after EXP2335 -> POOL-LADDER boot (switch on for the whole boot, deleted in rollback) -> T5b FULL boot.

## UPDATE 2026-11-11T02:08:20Z -- architect: the 0.76-1.13 s scene escape at launch = one TVB growth step (126 MiB) under the G3 lock

Source of the first long `measure-scene-escape` operation 1 (ACQUIRE) of every GravityMark launch, checked by me
against code and receipts (research returned as text; no REPORT.md exists, its scripts are in
`.local/night-20261110/scene-acquire-cost/`):
- Code: `AdmissionGpuvaG3PrivateEscape` calls `AdmissionG3TvbGrow` (gpuva_g3_windows.c, rc80, Asahi auto_grow) when
  the process is idle. The render before it overflowed the 8-block tiler heap: `measure-gpu-tvb` row of the census
  launch shows 53 overflows (partial renders), max_pages 1390 -> target 1043 blocks; the step maps 1011 blocks
  (126.4 MiB) in this one call. Each block takes 2 pool granules through `AdmissionMemoryPoolTvbTake` ->
  `AdmissionPoolSlabTakePrivate` -> `AddSlab` -> `AdmissionPoolNewSlab` (about 64 new 2 MiB slabs, past the
  large-page cache, i.e. chunked) and maps 8 GPU pages through the broker (about 16.2k calls).
- Receipts: `Wom1G3LockProfile` site gpuva_g3_windows.c:2860 HoldMax 1125.79 ms at pre-gm = the r2 escape of
  EXP2333 (1125.8 ms); largest wait behind any G3 lock site in the boot 76.65 ms. The escape is lock hold time,
  not a wait for jobs.
- Correction to the 01:5x addendum: "longer than ReserveUsMax, so not one pool reserve" compared the wrong path;
  this path does not use the reserve at all. ReserveUsMax 0.77 s belongs to the reservation that ran the one-shot
  pool probe.
- Split (estimate from existing per-unit receipts, not measured inside the call): chunked slabs 64 x 5.6-6.4 ms =
  0.36-0.41 s; broker grants about 0.09 s; slab-list walks (`TakePrivate` walks the whole list twice per granule,
  about 1500 slabs) and graph bookkeeping 0.04-0.13 s; sum 0.44-0.86 s against 0.76-1.13 s observed (16 launches,
  median 0.866 s). About 0.3 s of the median is not attributed.
- Consequence for POOL-LADDER: these 64 slabs go through the same `AdmissionPoolNewSlab`, so the ladder also acts
  here. Slabs of the frame-2 gap: about 293 (BO creation) + 64 (TVB) = 357; expected saving 357 x 4.6 ms = about
  1.6 s (was 1.35 s). ACCEPT / REJECT lines of 01:45:42Z are unchanged.
- OBSERVATION RULE for the POOL-LADDER boot (written before the run; not part of ACCEPT / REJECT): first
  operation-1 escape of the traced launches (w, r2, r3), control 0.76-1.13 s, median 0.866 s.
  If M1 <= 3.2 ms and the boot's median escape <= 0.70 s: slabs own the predicted share; the rest gets the split
  receipt only if it is still above 0.4 s. If M1 <= 3.2 ms and every escape stays >= 0.76 s: slab cost is not the
  main part; next step is a receipt-only split of the growth call (stamps around lease takes and page maps,
  counter deltas), one boot. If M1 > 3.2 ms the escape says nothing.
- Not pursued now: the 53 partial renders of the overflowing frame (same policy as Asahi: start small, grow 3x
  after overflow; cost of that frame not measured), and moving the growth off the acquire path (spreads the same
  work, the score counts total time).
Ranking unchanged: (1) non-busy about 1.9 ms per frame; (2) startup gap 4.9-5.5 s: chunked slabs about 2.3 s
(BO 1.97 + TVB about 0.4; POOL-LADDER), bind 1.16 s, TVB growth rest about 0.45 s, remainder; (3) R5 3D;
(4) C0; (5) R1; (6) R3/C2/C4.
