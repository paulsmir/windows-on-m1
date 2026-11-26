# Platform parity plan: do what macOS does, in a portable form

Started 2026-11-26 on the owner's order. Maintained by the architect session. This is the working list; it is
updated when an item changes state. Evidence lives in `investigation/EXPERIMENTS.md`, verdicts in
`investigation/GPU_CURRENT_STATE.md`; this file only says what is owed, who owns it and what comes next.

## Owner's rules for this plan (2026-11-26)

1. If macOS (or Linux) manages something on this hardware and our stack does not, implement the management;
   a one-off static setting is an experiment, not the end state.
2. Everything is built so that it ports and extends easily. The next SoC after M1 (T8103) is M4 Pro.
   No SoC constant in logic: addresses, state codes, tables and quirks come from the Apple device tree or from
   one per-SoC table; a new SoC adds a table and at most one backend. Every design note has a PORTABILITY section.
3. Nothing found is forgotten: every finding becomes a row here.
4. Every result gets the question "is it still needed even without a measured gain?" and an explicit
   KEEP / DROP with the reason (correctness, parity with Apple, heat, stability, portability).
5. Standing method rules still apply: one variable per experiment, read-only first, a first-ever register read
   goes through a proxy probe, nothing becomes standing without a gate and a ruling.

Status words: OPEN (not started), DESIGN, BUILT (offline), BENCH (queued or running), DONE, PARKED (with reason).

## A. Platform services the OS normally provides (owning layer: m1n1 broker / Mu / drivers)

| # | Item | What macOS / Linux do | Our state | Status | Next step | Keep question |
|---|---|---|---|---|---|---|
| A1 | SoC fabric (SOC) and DRAM (DCS) performance state | macOS: PMP firmware raises SOC/DCS from the GPU's level in PMGRScratch word 7 and from GPU bandwidth; Linux: an OS driver (pmgr-misc class) | Dynamic service HV_SOC_PERFSTATE v1 (EXP2938): 8722 / 8756 / 8780, +3.45 %, no decay, SOC back at code 4 in idle. Static pin (EXP2936) +1.95 % with decay, not promotable | BENCH: GATE main PASS 2026-11-26 (EXP2943/EXP2944: 8464.3 -> 8727.0, +3.10 %, lines a-e PASS; cost: more time above 110 on the P cores, about +1 W, 1.0 % decay within the boot). Standing when the restart part EXP2945 passes | CS part on the first day with a Steam word; per-job timing and probe rows (GPU line procedure) to confirm the mechanism; repeat with SMC sensors; PMP backend decision per SoC (M4 Pro: dump its PMP image first) | KEEP: confirmed part of the cold gap; managed form is the end state |
| A2 | Power-domain run-time management | Linux genpd powers unused domains down; macOS per-driver | Nothing is powered down after boot | BUILT: census script + image (read-only) | Run census (proxy + desktop); classify domains with no Windows consumer; then a one-variable power-down lever; end state: per-device power management through ACPI power resources served by the broker | Heat and battery; likely KEEP |
| A3 | AGX power-on tunables | macOS writes ~50 registers after GFX power-on; upstream m1n1 applies 7 only on the Linux path | 38 of 44 differ; lever for the 7: applied, persistent, NO_EFFECT (EXP2927) | BENCH: full block arm A queued; arm B (with strobes) built | Run arm A, then decide arm B | 7 = KEEP candidate for correctness (upstream: "things subtly break"); decide default-on after arm A/B |
| A4 | GPU idle gating | macOS and Asahi keep the GFX domain on; the firmware gates inside (idle_off, PS AUTO_ENABLE) | CORRECTED 2026-11-26 (B6 audit): the domain staying on is not a gap. Ours: idle_off 20 ms against 2 ms, PS AUTO_ENABLE not set | DESIGN | Decode existing Wom1FwRx PowerOff counts + one PS read; then a managed idle-gating lever | Heat, battery |
| A5 | Temperature sensors and thermal policy | macOS: SMC sensors, CLPC keeps P cores cool under GPU load; hottest GPU sensor 98.5 C without throttling | EXP2937 (AppleSmc 0.1.0.16, read-only key path, KEEP, standing from 2026-11-26): under back-to-back GravityMark the P-core control value Tp4z plateaus at ~120 (raw about 103 after the +17 margin), held by the SoC itself; scores 8452 / 7437 / 6968; firmware temp_c never above 91, so the GPU throttle fits SOCHOT (ADT gpu-sochot-temp 111) from the hot CPU; no SMC GPU-die key exists (GPU_MTR sensor is the on-die PMGR/AGX sensor); idle system power 7.6 W | DESIGN | (1) per-run sensor logging + TCMz cool-down and stop gates on the bench; (2) a/b/x/z margin check under load; (3) ACPI thermal zone from per-machine _DSD rows (cpu-p _PSV 110 on z keys, cpu-e 108, battery 45; GPU firmware-owned) — power line + Mu; (4) direct read of the on-die GPU sensor (m1n1 or KMD) as a separate row if needed; (5) owner decision on a temporary P-core cap for long sessions | Required for production; the largest sustained-load loss (18 %) |
| A6 | Hypervisor overhead | not applicable to macOS | 1.1-1.4 % of CPU; C1 lever removes ~8.6 ms/s of tick time (EXP2929) | BENCH: l5, a2 queued; c2 dropped (superseded) | Run l5, a2; then a combined candidate through GATE | C1 KEEP candidate; standalone boot needs no proxy poll at all |
| A7 | Direct hardware vGIC CPU interface | Linux KVM on M1 uses it | Every ICC access traps; design ready, S0-S2 code built | PARKED: icc is the fourth exit class | Revisit after l5/a2 numbers | Portability: needed on every SoC; keep the design |
| A8 | Memory-cache (AMCC) DSID priority and timeout acknowledge | macOS deprioritises two DSIDs and acks a DSID-timeout error at GPU start | Unknown; first read attempt stalled the bus (EXP2931) | PARKED until daytime | `mccrd_probe` at the proxy with the owner present; MCCRD returns only for offsets shown safe | Correctness question (unacked error) |
| A9 | Upstream m1n1 fixes selected before the licence change (5 commits) and the fuse hand-over ('AGXF' window) | — | Listed, not applied | OPEN | Re-read each commit's subject against this plan; apply or drop with a reason | — |
| A10 | Audit: Linux-only setup in m1n1 that the Windows path skips | — | Done: LINUX-ONLY-SETUP-AUDIT.md; found A1 and A2 | DONE | — | — |
| A11 | Broker power-off of the GPU domain | macOS/Asahi: GFX PS register 0x23b7003f8 with AUTO_ENABLE (macOS 0x0f00200f -> 0x1f00200f) | Found 2026-11-26 (m1n1 line, source): /arm-io/sgx clock-gates are virtual (GFX-SGX 314, GFX-BUSY 315); pmgr_adt_power_disable writes nothing for them, so the broker's OFF never gates GFX at PMGR level; idle saving comes only from the firmware's internal gating. The instr-v6 "PS active" pre-check read the wrong register (fixed in instr-v6b e8b985ed by a tested resolver) | BUILT: PS AUTO lever (image 4331eb21) against instr-v6b | Bench after the perf-state runs; verdict heat-first (Wom1FwPower gated fraction, idle temperature); then a separate lever for a real OFF | Correctness and heat |

## B. GPU driver

| # | Item | Our state | Status | Next step | Keep question |
|---|---|---|---|---|---|
| B1 | Batched GPUVA mapping (one call per table range instead of per 16 KiB leaf) | KMD/client side committed behind a switch (22b142453); m1n1 side and MaxEntries pending (instr-v5) | BUILT (half) | instr-v5 run; m1n1 broker side; one GATE with the switch | Load time and eviction churn; KEEP |
| B2 | Out-of-memory behaviour | Pool reservation fails near 3.85 GB Available; three low or failed first runs on 2026-11-25/26 | OPEN | Design order after the Asahi/macOS audit; uses B1 | Production requirement |
| B3 | TDR reset path returns STATUS_DEVICE_HARDWARE_ERROR | Spec written (GPU-PRODUCTION-ITEM-TDR-RESET.md) | PARKED behind the performance work | Schedule after A1/A3 verdicts | Production requirement |
| B4 | macOS power calibration (Wom1PowerModel) | NO_EFFECT on sustained loss (EXP2900-2905); values match macOS | BUILT, default absent | Default-on inside the next promotion candidate through GATE | KEEP: parity |
| B5 | HwDataShared3 enable (Wom1Hws3Off) | NO_BEHAVIOUR_CHANGE twice | BUILT, default absent | none | KEEP as lever, no default change |
| B6 | Audit: Asahi and macOS 13.5 GPU bring-up, run-time and per-job behaviour against ours | Ordered 2026-11-26 | DESIGN | Table + top five read-only checks | — |
| B7 | Accepted package | 30.0.2883.0 (range resolve on by default) accepted 2026-11-25 | DONE | — | — |
| B8 | TA core mask | all-ones; Asahi uses the present-core mask from the ID registers on this 7-core part (B6 audit, CM-3) | OPEN | Check the existing GpuIdRegisters receipt; lever = mask computed from the ID registers | Correctness; portable by construction |
| B9 | Compute stats pointer | private block; Asahi uses the global Stats.comp from RuntimePointers, as our TA/3D already do | OPEN | Receipt dump after one GravityMark run; then switch-guarded change | Correctness |
| B10 | Firmware object lifecycle (DestroyContext flush, GrowTVBAck, FwCtl 0x84 on unmap) | missing or partial (B6 audit) | OPEN | Design; prerequisite for B2 and B3 | Production requirement |
| B11 | Per-job timestamps only on request | always on | OPEN | Design after B9 | Small cost; parity |
| B12 | UAT walker config word (SGX+0xd08000 w3/4) | ours 0x003680d8, kext constant 0x80d8 | OPEN | Read through v4c sgxrd in the macOS session (C1) and on our stack | Unknown effect; parity |
| B13 | Priority/pipe choice and JobMeta words | differs from macOS 15.7.8 kick shape; 13.5 protocol validated by Asahi | PARKED until the macOS session | One contract bundle after C1 | — |

## C. Evidence still owed

| # | Item | Status | Next step |
|---|---|---|---|
| C1 | Second macOS-under-hypervisor session (hv_control v4c): UAT walk, work-item bytes, PERFST of SOC/DCS next to word 7, scrwatch on word 7, HwData page-by-page, ADT dump, clean GravityMark score | Tools built; v4c rehearsal owed (daytime, owner present) | Ask the owner for a time |
| C2 | Linux (Asahi) reference on this machine: is macOS-level GPU speed reachable with our firmware version? | Deferred by the owner on 2026-11-11 | Owner's decision; raise again with today's findings |
| C3 | Guest network loss after EXP2918 | Cured by re-creating the Realtek USB Ethernet devnode; cause not established | Watch for recurrence; first step is the Device Manager rescan |
| C4 | Windows updates installing despite the pause policy; RTC 21.7 min behind on one boot | Noted | Bench identifies the updates; RTC goes to the power line |

## D. Portability rules applied to every item above

- m1n1 services (perf-state, power-domain, tunables): logic reads a per-SoC description — domain names, register
  bases, state code sets, vote tables, tunable lists — built from the ADT where the ADT has it and from one
  generated per-SoC table where it does not (source: firmware RE or macOS trace, cited in the table). T8103
  numbers never appear in the service code.
- Two backends are allowed behind one interface where Apple itself differs by SoC: an emulated policy (T8103:
  table read from the PMP image) and the real coprocessor (SoCs where it must run).
- KMD: firmware-version-dependent layouts stay behind the versioned module; switches are data.
- Every lever keeps its flag and its regression test; a flag-off build is byte-identical to the standing image.
- For M4 Pro the first step of each service is the same census/probe tooling run on that machine; the tools take
  the SoC from the ADT and must not assume T8103 node names.
