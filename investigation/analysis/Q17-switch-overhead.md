# Q17 diagnostic overhead implementation plan

Goal: remove optional job instrumentation from the default hot path while preserving queue admission, publication, completion and predictive polling. Spec: `.local/handoff/codex/q17-switch-overhead/TASK.md` in the shared handoff directory. Execution is inline under the user's end-to-end instruction; no hardware, guest, builder, other worktree or ledger changes.

Sources inspected: backend_platform_windows.c (switch initialization, worker poll, timing/export, manager save); shared apple_agx_render_shared_memory.c (manager slots, notifier flight transaction); render_rc_progress.h; q1/q2/q6 DESIGN.md; EXP2102/2103/2105; local Asahi queue/mod.rs:284-330 and buffer.rs:684-722; WDK 26100 d3dkmddi.h:1009-1021,5218-5232. The pinned m1n1/Mu gitlinks have no checked-out sources here. No inherited hardware contract changes are needed and no live state is collected.

Contract: KMD owns diagnostics, publication, DMA lifetime, interrupt/completion and recovery; firmware updates shared queue/stamp/manager state. Asahi publishes notifier thresholds only when committing and retains live manager ownership. Windows reports original submission fences. Preserve all these functional operations, current power/mapping/ACPI and launch/recovery contracts. Diagnostic shared-memory reads are optional; completion reads and submission/complete identity used by poll prediction and UMD completion records are functional.

- [x] Reproduce implicit RcReceipt enablement and diagnostic work with diagnostics off using compiled production configuration/export/poll blocks.
- [x] Make Wom1RcReceipt the sole receipt opt-in; remove RcDepth2/General forcing. Add Wom1JobDiagnostics for optional job timing/window work and firmware sampling/export. Preserve submit metadata, complete mark, UMD timestamps/completion records, predictive poll learning, fault evidence and explicit drills.
- [x] Test disabled/enabled paths, functional prebind equivalence, separate receipt-only exports, functional completion identity, and existing manager/notifier/recovery tests.
- [x] Run full prescribed baseline to BASELINE_OK; review /W4 type discipline and protected invariants; implementation committed; final REPORT.md records identities.

Review focus: diagnostics must never gate admission/publication/retirement; disabled instrumentation must return before QPC/locks/reads/copies/registry; explicit short receipt probes retain immediate exports; explicit drill evidence must survive with ordinary diagnostics off; compile flags excluding G3 must remain valid.

Hardware ranking, counts, checkpoint expectations and final file:line evidence follow below after verification. All time estimates are hypotheses, not measured costs.

## Source audit and ranking

Paths below abbreviate `drivers/apple-agx/render-admission/src/backend_platform_windows.c` as **backend**, and `drivers/apple-agx/shared/src/apple_agx_render_shared_memory.c` as **shared**. Line numbers refer to the Q17 candidate unless explicitly labelled base. Comparison is the supplied same-source EXP2103 versus EXP2105, not an older implementation. EXP2105's HD 5613 is supplied by TASK.md; this checkout's ledger contains its BEFORE entry only. EXP2103 ACTUAL is at investigation/EXPERIMENTS.md:67656; EXP2102 ACTUAL at :67636.

| EXP2103 switch | Effective with JobDepth2=0? | Work / evidence |
|---|---|---|
| Wom1GpuFaultInfoRead=1 | Yes, same in both controls | Fault-only register capture, backend:2789,2819,2851. No healthy per-poll FAULT_INFO read. |
| Wom1ManagerSlots=1 | Yes | backend:7311, shared:1279,1570: search at most three managed slots by full key, choose free/LRU on miss, relocate manager objects. Live slots avoid restores/InitBM. No additional per-poll manager scan. Manager flags are separate `Wom1ManagerReceipt`, absent here (backend:3766). |
| Wom1JobBuildAhead=1 | Yes, already default | backend:7321: default-on unless DWORD 0. Per-poll prebind, template release, scheduler locks and chaining are common to minimal and full masks, not an incremental EXP2103 cost. |
| Wom1JobDepth2Caps=1 | No | backend:7328: requires JobDepth2. Capability mask is 0, no second-sidecar capability from this switch. |
| Wom1RcReceipt=1 | Yes | Base backend:7290,5361,5375,6116,916. Every skipped-kick poll takes SchedulerLock, fetches successor fence, QPC and updates a typed decision histogram/ring (reason **70**, kick disabled, takes precedence over 66). Marks RcExportPending. Export at job end bypasses two-second throttle, even with empty progress ring. Strongest differential candidate. |
| Wom1NotifierFlight=1 | Yes | backend:7262 initialization before provider prepare; shared:499-564: reserve/retire a 48-byte exact-fence record, bounded two-entry scans, read old threshold, stage event_count in CPU memory, publish one aligned 4-byte threshold at commit plus flush/barrier. No per-poll scans unless receipts sample a live pair. |
| Wom1RcDepth2=1 | Partly | Base backend:7289-7303: forces RcReceiptEnabled even without JobDepth2; JobDepth2Kick remains false without capabilities. Sets a class mask but no kick uses it. Q17 removes only the receipt side effect. |
| Wom1Depth2General=1 | No | backend:7351: requires ManagerSlots, NotifierFlight and nonzero capabilities. With JobDepth2=0 it cannot enable SaveOnEvict, live-manager sharing, fault suffix or receipts. At depth two it formerly forced receipts independently of RcReceipt; Q17 removes that forcing too. |
| Wom1Depth2TaOn3d=1 | No | Read inside effective General block only, backend:7360. |
| Wom1Depth2TaOnCompute=1 | No | Same prerequisite. |
| Wom1JobDepth2=0 | Effective off | Neither early kicks nor rc95 TA/prelude sampling. Functional prebind remains default-on. |

SaveOnEvict is **not** separately set in EXP2103. General is false, so backend's ManagerSaveOnEvict expression is false. shared:1496-1528 saves the full 380 firmware bytes (Info 188 + BlockControl 64 + Counter 64 + Stats 64) per completed render, as on the legacy serial path. When explicitly enabled at depth one, or forced by effective General at depth two, it saves Stats only (64 bytes), and snapshots the full state only on idle eviction/forced reinit (shared:1361,1405; backend:3130-3187). q1 already removed the avoidable per-render snapshot at its owner. Enabling it implicitly here would change the functional experiment, so this patch does not do that.

NotifierFlight does not double every object copy. Object12 redirects to the flight's 4-byte EventCount (shared:669); normal command copies remain the same. Both parity copies are seeded on queue initialization only (shared:684). Per-render active object copies are shared by both masks. Backend job images are 216 bytes on this 64-bit host, including retained timestamp pointers; candidate/active/pending copies and widened structure storage are common compiled code (shared:1172,1208; apple_agx_backend_runtime.c:285), not dynamically widened by a switch. Do not attribute that fixed size to EXP2103's mask.

### Counts and timing estimates

Use the task's **8 jobs/frame, 30 frames/s, 20 polls/job** only as a workload estimate: 240 jobs/s, 160 polls/frame, 4800 polls/s. EXP2102 reason-66 counts are not direct measurements of EXP2103's reason-70 path.

1. **RcReceipt-triggered registry export, highest causal rank.** Every job with a skipped-kick poll dirties decisions. Base exports timing (18,464 bytes), optionally dense timing (another 18,464), FW timing (5,520), GPU perf (1,936), poll state (2,904), RcProgress (19,984 even when empty), RcDecisions **v4** (9,920). Max steady-state payload = **77,192 bytes/job**, about **603 KiB/frame / 17.7 MiB/s**, 7 value writes/job, plus open/close and about 28 KiB of ring snapshot copies. No flush on this normal path. Minimal mask exports at most every two seconds (~0.017/frame), versus ~8/frame with dirty receipts. Shapes measured with host sizeof; FW ring uses WDK 32-bit ULONG. v2 decision sizes in the older q6 note are obsolete.

   The HD ratio 5613/5244 = 1.0704 corresponds to ~2.09 ms/frame using the 167 s scoring window (31.85 ms vs 29.75 ms). Just **~261 us per extra export** explains that gap. Historical code comment records 0.76–1.13 ms for the old registry+flush path; it is not a measurement of this unflushed path. At 50–300 us/export (illustrative, not measured) current exports cost 0.4–2.4 ms/frame. Source supports the ranking, not a precise duration or a proven 7% cause.
2. **Typed skipped-kick decisions, next differential candidate.** ~160 additional SchedulerLock pairs and QPCs/frame. Receipt identity scan is bounded by 128 records for reason70 (32 for reason66); repeated same identity often matches early. At a hypothetical 0.2–2 us/poll this is 0.032–0.32 ms/frame. No firmware reads by RcSample at depth one: RcCurrent.RFence stays zero. At depth two, with an admitted live pair, RcSample adds **13 ReadU32 calls/poll**, each range validation + two memory barriers + shared word read, up to 2080/frame or 62,400/s. These are coherent firmware-memory transport reads, not FAULT_INFO device-register reads. Sampling continues even if the ring saturates. Their actual cost needs hardware measurement.
3. **Job timing, firmware/performance receipts, lower differential rank but removed from default.** Previously common to both masks: per-phase QPC/JobTimingLock, two three-word pstate reads/job, GPU performance reads, timestamp/tail/stats snapshot, up to two TA-ts1 word reads/poll until first change. rc95 depth-two-only adds up to four queue ReadU32 operations/poll until expected progress. Rough optional timing cost is O(10) locks/job plus up to 320 ts1 word loads/frame, O(640) rc95 loads/frame at depth two. Do not add these common costs to the EXP2103 difference; disabling them can improve both profiles. Required submit/complete metadata, completion timestamps and predictor updates remain.
4. **ManagerSlots and NotifierFlight, low rank.** At four renders/frame: at most ~12 managed-key comparisons per pass through the three-slot lookup, 1520 bytes of serial manager snapshot (not incremental), 1264 bytes/frame saved if SaveOnEvict enabled. Eight flight reservations/retirements zero ~768 CPU bytes/frame and publish eight 4-byte thresholds; bounded two-entry scans and existing coherency barrier. These are lifecycle obligations. No remaining independently justified removable manager/notifier cost was found without changing their ownership/publication behavior.
5. **Inert switches / fixed images: zero incremental runtime cost** apart from start-time registry queries and cached branch conditions. No registry switch lookup occurs in the healthy per-poll path.

## Implemented diagnostic contract

- `Wom1RcReceipt=1` explicitly enables typed decisions/progress and their immediate export. RcDepth2 and Depth2General never turn it on.
- `Wom1JobDiagnostics=1` explicitly enables optional job phases/subintervals, CPU/pstate/performance samples, FW timing/ts1 sampling, rc95 opportunity sampling and job-window counters/exports. Existing `Wom1GpuPerfReceipt=0` still suppresses perf within this mode; `Wom1ManagerReceipt=1` adds live/rebind flags within this mode. Both selectors alone no longer cause timing work. To reproduce the old complete diagnostic set, set JobDiagnostics=1 and RcReceipt=1 explicitly.
- With both off, the worker calls neither Rc sampling/skipped-decision helpers nor rc95 sampling nor routine export. Optional timing helpers return before QPC, locks or firmware reads; the builder has no BuildMark callback. Zero cost here means zero diagnostic sampling/export effects, not removal of branch instructions or functional completion metadata.
- Receipt-only mode exports only RcProgress/RcDecisions, not unrelated timing snapshots. Explicit short probes keep immediate exports and write-failure retry behavior.
- Original submission/complete identity, UMD GPU timestamps/completion records, poll prediction/learning, notifier publication and exact retire, live manager lifetime, kick admission, queue reads used for completion, and exceptional fault evidence remain. Explicit one-shot drills remain independent and keep pair identity/reports without routine receipts.

## Two hardware checkpoints for the integrating owner (not run here)

Prerequisites: review/merge, WDK /W4 /WX package build, exact manifests/package/profile/m1n1/Mu/recovery hashes and preregistration in the integration ledger. Same scene (100k temporal 0), resolutions, GPU power settings and recovery contract as EXP2103. No prelude opt-in.

WHY THIS HYPOTHESIS:
1. EXP2103 HD 5244 and supplied same-source EXP2105 HD 5613 isolate the additional switch set; General/caps are provably ineffective at depth one.
2. Base skipped-kick decisions dirty receipts every poll even when kicking is disabled; the dirty bit bypasses the two-second registry throttle.
3. Compiled-production RED→GREEN tests reproduce and remove that work with diagnostics off while keeping functional prebind/kick and completion identity.

1. **Depth-one diagnostic-off control.** Q17 candidate, EXP2103 functional mask, JobDepth2=0, RcReceipt=0, JobDiagnostics=0 (or absent), no ManagerReceipt/perf opt-in. Retain ManagerSlots/NotifierFlight/RcDepth2/General/sub-switch values exactly. Hypothesis: HD returns near **5600–5700** (possibly better with common instrumentation removed), recovering most of 369 points; FHD near **4400–4550** is a provisional range because EXP2105 FHD was not supplied. If HD stays near 5244, reject diagnostics as the dominant explanation and isolate ManagerSlots vs NotifierFlight next; do not assert success from missing receipts. Require correct rendering, normal exit, no fault/reset/removal, desktop healthy. No hot-loop receipts are expected; app score/elapsed time and existing error/event evidence provide this verdict.
2. **Same candidate, change only JobDepth2=1.** General/non-prelude classes become effective; RcReceipt and JobDiagnostics remain off. Expected relative HD gain if EXP2102/2103 benefit survives: roughly 2–5%, **~5750–5950** for a 5600–5700 depth-one result; FHD perhaps 0–3% (**~4490–4650** for a ~4490 baseline). These are falsifiable hypotheses, not promised scores or proof of individual switch costs. Correctness criteria are identical. No kicks/overlap proof can be claimed without a separate explicitly enabled short diagnostic probe; successful benchmark output alone only proves that workload completion.

Two runs test the aggregate diagnostic-cost ranking and depth-two benefit, not precise attribution between exports and sampling. Integrating owner collects evidence first, then exact package/devnode cleanup through the existing GPU-visible Code28 recovery; GPU-hidden only for emergency rollback. Stop on crash, reset, corruption or uncertain identity. Do not run any of these commands in this offline task.

## Review correction

Independent review found that a drill-only mode, newly reachable without automatic RcReceipt, kept its export dirty forever. Production sampler now dirties the drill only when a pinned terminal report or overflow changes (backend:2729); production drill exporter clears it after a successful drill-only registry write (backend:905), retaining it on failure. Both real-helper regressions were observed RED then GREEN. The follow-up review found no remaining actionable correctness issue. This does not replace the Windows package compile or hardware checkpoints.

## Verification

Implementation: `f6a5a79e15fc56360372302daf48e0bc489d1571` (`fix(agx): gate job diagnostics independently of depth two`). Affected suite: 104 tests OK; updated actual recovery fixtures: 5 tests OK with diagnostics off; final configuration/export refinements: 3 + 1 tests OK. Real code snippets are compiled with warning checks and ASan/UBSan; new C models also use pattern stack initialization. Protected invariants and git diff whitespace checks pass.

Required full command, from tests/: `python3 run_against_baseline.py`:

```text
ran 820 tests in 330 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

The six known failures are listed in the task REPORT.md, with logs preserved in the handoff. No baseline exemptions or experiment/change ledger edits. WDK `/W4 /WX` native package compilation and hardware measurement remain integration checkpoints; neither was attempted offline.
