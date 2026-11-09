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
