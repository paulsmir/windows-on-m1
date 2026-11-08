<!-- 2026-11-08: compiled from m1n1 exp/m1n1-brokerlock-slc source + EXPERIMENTS.md; key claims spot-checked (5 kHz tick hv_tick_policy.c:6, ICH_HCR TC|TALL1, bhl + nvme poll on every slow exit hv_exc.c). EXP2251 (CPPC memory-backed counters) result pending at time of writing. -->
# m1n1 HV inventory: what it does today, what it costs, what should leave it (draft, 2026-11-08)

Sources: m1n1 `.local/m1n1-wt-brokerlock-slc/src` (paths below are relative to it), ANS line `.local/m1n1-wt-ans-direct-slc`, `run_uefi.py`, `investigation/EXPERIMENTS.md` and `GPU_CURRENT_STATE.md` in `integration-ad04-windows-compiler`. **[I]** marks an inference that has not been measured.

## 0. Structural facts that set every cost

- **Global lock.** Slow exits take the global `bhl` (`hv_exc.c:1816-1832`). Before releasing it, `hv_exc_exit` calls `hv_nvme_poll_irq()` + `hv_update_fiq()`. After releasing it, the exit runs one step of the **scanout service state machine** (`hv_exc.c:1834-1846`). Unlocked fast paths cover WFI (`:1864`), ICC/PMU/timer sysregs (`hv_handle_msr_unlocked`, `:781`), SMC/PSCI (`:1934`), HVC (`:1938`) and AIC IRQs (`:2039`).
- **CPU0 EL2 tick runs at 5 kHz.** Secondaries tick at 100 Hz (`hv_tick_policy.c:4-6`). On CPU0 every FIQ takes the slow path under `bhl` (`hv_exc.c:2140-2215`). `hv_tick` (`hv.c:1351-1376`) runs the watchdog pet, diag, PC sampler, uartproxy, vUART poll, fb-stream and CPPC tick. Lowering it to 1000/100 Hz made Windows progress in bursts (EXP-20260814-029/030, ledger ~l.14055-14100), so today something depends on that polling cadence. [I] The likely dependents are NVMe/vUART/scanout polling.
- **Stage-2 is identity pass-through for /arm-io** (`hv_autonomous_runtime.c:198,323-328`). The hooks below are the only trapped MMIO. Stage-2 does not isolate devices. REC-EXP2205 notes the iBoot DCP DART tables are guest-writable.

## 1. Inventory

Categories: ARCH = mandatory architecture, GLUE = platform glue, BROKER = device broker, DIAG = diagnostic, LEGACY = legacy or debug-only.

| # | Responsibility (source) | Cat. | Hot-path trigger | Measured cost | Recommendation |
|---|---|---|---|---|---|
| 1 | vGICv3 distributor/redistributor/ITS MMIO emulation (`hv_vgic.c:2450-2466`) | ARCH | Per GICD/GICR access. Mostly boot and enable/priority changes | not measured | **Keep** |
| 2 | ICC sysreg emulation. `ICH_HCR_EL2.TC\|TALL1` set, so every IAR1/EOIR1/PMR/RPR/SGI1R traps (`hv_exc.c:870-990`) | ARCH | ≥2 traps per guest interrupt; 1 per IPI | not measured. EXP1085 gives ~1740 guest IRQ/s on CPU0 | **Keep in HV, but reduce traps.** [I] Clearing TALL1 so IAR/EOI hit the hardware ICV path depends on the running-priority fixes (EXP1890-1906) and on the KVM-on-M1 quirk list. S-M, risk high (previous vGIC hangs) |
| 3 | AIC→vINTID routing, LR maintenance, level masking (`hv_exc.c:2039-2135`, `hv_irq_routes.c`) | ARCH | Per device IRQ (no `bhl`) | not measured | **Keep** (M1 has no GIC) |
| 4 | Guest timers. FIQ→vIRQ 17/18 inject/latch (`hv_exc.c:663-760`); CNTV/CNTP→EL02 maps (`:796-801`); CNTVOFF/time stealing (`:1841`) | ARCH | Per timer expiry, plus a check on every exit | not measured (`TIMERRATE` is a trace only) | **Keep** |
| 5 | EL2 tick. CPU0 at 5 kHz under `bhl`, secondaries at 100 Hz (`hv_tick_policy.c`, `hv.c:1351`) | GLUE/DIAG | 5000 serialized exits/s on CPU0 | Indirect: 1 kHz secondaries = +7000 EL2 entries/s and stutter (ledger l.13224) | **Keep for now. Lower to ≤1 kHz once #9/#13/#16 no longer need polling.** M, risk medium (EXP-029 regression) |
| 6 | PSCI CPU_ON/OFF/SUSPEND/RESET/OFF/FEATURES (`hv_psci.c:1686-1760`); WFI idle (`hv_exc.c:1864`) | ARCH | Boot, CPU hotplug, idle | not measured | **Keep** (no EL3) |
| 7 | PMUv3 → Apple PMC redirection (`hv_exc.c:1172-1590`). Also in Python (`proxyclient/m1n1/hv/__init__.py:657-940`), which is LEGACY for C launches | ARCH | Per PMU sysreg access by Windows | not measured | **Keep in C. Remove the Python duplicate from the production path.** S |
| 8 | Apple impdef sysreg pass/maps (GXF, SPRR, AMX, HID, PMCR) (`hv_exc.c:793-861`) | ARCH | Rare | not measured | **Keep** |
| 9 | Emulated NVMe + PCI ECAM + virtual INTx. Backed by ANS (`hv_pci.c:199`, `hv_nvme.c:555`). `hv_nvme_poll_irq` runs on every slow exit (`hv_exc.c:1837`) | BROKER | Per NVMe doorbell/completion, plus a poll on every exit | Broker ANS path ~164 µs/command (ANS010-R7, `ANS_DIRECT_PLAN.md:12,17`). Native read ~850 MB/s (ANS006-R10) | **Remove from production. Already done by ANS-DIRECT (ANS011 profile)**; keep as the `VNVME_RECOVERY` profile only. Default `--storage-mode` is still VNVME_RECOVERY (`run_uefi.py:27`). Remaining work: S (default flip + skip the poll when no vNVMe) |
| 10 | ANS boot/handoff, broker and write window (ANS tree `hv_ans_*.c`, 1275 lines) | BROKER→boot | Boot only in ANS-DIRECT | see #9 | **Boot-only handoff (done).** Broker only in recovery/debug |
| 11 | AGX GPUVA v5 broker: UAT leaf publish, backings, leases, JOB_BEGIN/END, sync_tables + TLBI ASIDE1OS (`hv_agx_gpuva_v5.c`, dispatch `hv_agx_power_mmio.c:~897`) | BROKER | Per GPU job (LEASE/JOB_*), per 16 KiB page map/unmap, per process switch | EXP1085: REG_BACKING 13.7 µs, UPD_LEAF 9.5 µs, REVOKE 39.2 µs, 376 ms/s broker time in a drag. EXP1127: UPD_LEAF 4.1-4.5 µs, JOB_BEGIN 3.3-3.6 µs, 59-76 ms/s. EXP2163: LEASE 160-272 µs per switch, 2 per frame. EXP2169: 30-48k calls/10 s. EXP2233: up to ~38k publications/s, each with TLBI | **Move to the native KMD** (Asahi-style in-kernel UAT + TLBI). The HV keeps only a one-time IPA→PA contract (`hv_guest_ipa_pa.c`, HVC 0x4d31) or identity RAM. L, risk high. Depends on V1 retained slots (EXP2165-2168) and EXP2232/2233 (runtime VTTBR write is UNDEFINED; TGE path) |
| 12 | Retained GPU context-table page hook 0x9fffb8000 (`hv_agx_power_mmio.c:973`, `hv_agx_retained_platform.c:536-551`); retained root/tables/backing; hwdata/fwio/rr windows | BROKER | Per KMD access to the TTBAT/context page and the profile/fw-io windows | not measured | **Move with #11** (the KMD owns TTBAT like Asahi). L |
| 13 | Scanout broker + service: DCP swap/poll (`hv_agx_scanout_broker.c`, `hv_agx_scanout_service.c`). One step on every exception exit (`hv_exc.c:1845`) | BROKER | Per DWM flip (MMIO), plus a step on every exit | EXP2169: 45-69k steps/10 s, hold max 254-920 µs. It blocked V5 for 117-244 µs until the broker-lock split (EXP2170/2180). EXP2236: ~19k v5/10 s during boot | **Move to a native DCP/IOMFB display path in the KMD**, or as an interim run it event-driven instead of per-exit. Interim M, native L |
| 14 | AGX power broker (PMGR power on/off) (`hv_agx_power_broker.c`, `:1014`) | BROKER/GLUE | Rare (receipt seq constant during runs, EXP2169) | not measured | **Move to firmware/ACPI power resources** (`_PS0/_PS3`) or the KMD. M |
| 15 | Pool window / local reserve (`hv_agx_pool_window.c`, `hv_agx_local_reserve.c`) | BROKER | Per pool-window command / local receipt read | Local-reserve fast path halved broker time (EXP1127) | **Retire with #11.** If guest RAM stays non-identity, keep only as a boot-time carve-out (Mu memory map). M |
| 16 | xHCI1 page hook: DMA-pointer (CRCR/DCBAAP/ERSTBA/ERDP) translation + DART prep (`hv_vgic.c:36-221`, `hv_xhci_handoff.c`) | BROKER/GLUE | Every access to the first 16 KiB of xHCI1. [I] This likely includes doorbells and ERDP, i.e. per USB transfer/interrupt | not measured | **Move to firmware**: Mu programs the xHCI DART (identity over guest RAM) before EBS, then the hook is dropped. M, risk medium (USB = input + SSH). Measure trap rate first |
| 17 | CPPC page: Desired Perf writes → cluster P-state + DRAMCFG (`hv_cppc.c:100-205`, map `:257-262`) | GLUE | Per FxPPM write. Counter reads were trapping | EXP2250: 42k-299k counter reads/10 s (4-30k traps/s). The brief's EXP2251 fix (memory-backed counters, `hv_cppc.c:207-225,257`) is in the source; I did not find the EXP2251 result in the ledger | **Keep short-term** (cheap now). **Long-term: a Windows PEP / processor-perf driver writing PMGR directly.** M |
| 18 | Apple input SPI/MMIO pass-through setup (`hv_apple_input.c:313`) | GLUE | Boot only | none | **Keep** (boot-only) or move the mapping to the Mu memory map. S |
| 19 | vUART + emulated PL011 for kd (`hv_vuart.c:158`, `hv_pl011.c`); `hv_vuart_poll` in the tick | DIAG | Per UART access, plus 5 kHz poll | not measured | **Diagnostic profile only** |
| 20 | Framebuffer USB stream (`hv_fb_stream.c`, `hv.c:1374`) | DIAG | 5 kHz tick | not measured | **Diagnostic profile only** (`--display-mode virtual`) |
| 21 | Watchdog/diag/PC sampler/snapshots; `hv_wdt_breadcrumb` on every exit (`hv_wdt.c`, `hv_diag.c`, `hv_exc.c:459`) | DIAG | Every exit (breadcrumbs), tick | not measured | **Keep breadcrumbs (recovery value). Gate samplers off in production.** S |
| 22 | BROKERLOCK accounting (`hv_agx_power_mmio.c:155-320`) | DIAG | Per broker call | n/a | Remove with #11 |
| 23 | AIC trace hook (`hv_aic.c:90`), virtio (`hv_virtio.c:307`, proxy-only), `hv_smmu.c` (not in Makefile), vTPM | LEGACY | Only if armed | n/a | **Remove from the production build** |
| 24 | Python proxy HV (assisted launch: tracers, `map_hook`, time stealing) | LEGACY/boot | Every exit not handled in C goes over USB | not measured | **Production = C runtime only** (autonomous). Python for bring-up only |

## 2. Prioritized plan (top 5)

1. **GPUVA v5 + retained tables into the KMD (#11, #12, #15).** This is the largest measured HV cost on the GPU path:
   - 59-376 ms/s of broker time (EXP1085/1127);
   - 160-272 µs LEASE twice per frame (EXP2163);
   - ~38k publications/s, each with a TLBI (EXP2233);
   - the lock contention in EXP2169.
   This mirrors ANS-DIRECT (164 µs/command removed). First step: a design doc and an offline translation (WDDM paging → in-KMD UAT), then one hardware discriminator. Prerequisite: settle the IPA==PA vs IPA→PA-table contract.
2. **Scanout service off the exception-exit path (#13).** Interim: run it from a dedicated event or tick instead of on every exit (45-69k steps/10 s, hold up to 920 µs, EXP2169). Final: native DCP in the KMD.
3. **ANS-DIRECT as default; vNVMe out of production (#9).** Then `hv_nvme_poll_irq` on every exit (`hv_exc.c:1837`) can be compiled out. This also unblocks item 4.
4. **Lower the CPU0 5 kHz `bhl` tick (#5)** once nothing polls (no vNVMe, no fb-stream/vUART in production, scanout event-driven). It needs one A/B against the EXP-029 burst-progress symptom.
5. **Measure, then fix, the trap rates nobody has counted yet:** ICC traps per interrupt (#2) and xHCI hook traps (#16). Add receipt-only per-ESR/per-hook counters in a 10 s window, in the style of EXP2250. Then either move the xHCI DART setup to Mu (#16) or narrow the ICC trapping (#2). The counting comes first, so decisions are not made on guesses.

## 3. Must stay in the HV no matter what

- **vGIC (#1-#3).** Windows ARM64 requires a GICv3. M1 has only AIC plus the ICH virtual CPU interface, so the distributor, redistributor and AIC→LR routing have to be emulated at EL2.
- **PSCI (#6).** Windows brings up CPUs and resets through SMC/HVC PSCI, and M1 has no EL3. CPU_ON spin-up, SYSTEM_RESET/OFF and idle need an EL2 owner.
- **Timer virtualization (#4).** M1 timers are FIQs, not GIC PPIs. Delivering them as vINTID 17/18, the EL02 redirection and CNTVOFF all require EL2.
- **Apple sysreg/PMU shims (#7, #8).** Windows touches PMUv3/GXF/SPRR state that M1 implements differently. Only EL2 can trap and redirect it.
- **Minimal stage-2.** It protects HV code and data and enforces the IPA layout that ACPI describes. [I] Device isolation is not provided today and is not required for function.
- **Boot handoff.** Firmware (ANS, AGX power-on until #14 moves) and DART/DCP state inherited from iBoot.
