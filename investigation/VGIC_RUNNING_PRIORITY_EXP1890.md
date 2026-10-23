# vGIC stale ACTIVE LR (EXP1890 / EXP1889 / EXP1876G): root cause

Date: 2026-10-23. m1n1 fix: `aca9fd5d` (running-priority preemption) and
`db990f95` (queued IRQ keeps the programmed priority), on `m2/pool-window`
above `21c3648d`. Artifact: `.local/experiments/m1n1-db990f95-vgic-rp/m1n1-g2-abi6-iomfb-owner.macho`,
SHA-256 `1bb6a818139784ee1122beef56825404e5c213dbdd366496fd9e3a3fa7cf0904`,
same generated config as the EXP1890 m1n1 (`a01c4ca9…`).

## Sources inspected

- Hardware: `.local/experiments/EXP1890-rov-cr-uavonly/evidence/EXP1890/full-hang-snapshots.log`
  (two HV WATCHDOG snapshots 35 s apart), plus every earlier HV WATCHDOG
  snapshot under `.local/experiments` (50 logs) for ACTIVE LRs.
- Windows: ntoskrnl from the EXP1889 kernel dump `dump/MEMORY-gm.DMP`
  (base 0xfffff80147a00000, TimeDateStamp 0x142ac692, SizeOfImage 0x124a000,
  WER branch ge_release). The image was rebuilt from the bitmap dump (all 4682
  pages present) and disassembled with llvm-objdump. The live HAL GIC objects
  were read from the same dump. RVAs are listed below.
- m1n1: `src/hv_vgic.c` (`hv_vgic3_running_priority`, `update_vi`, `do_iar1`,
  `do_eoir1`, `set_igrpen1`, distributor ICENABLER), `src/hv_exc.c` (ICC trap
  fast path, `hv_exc_irq` maintenance and queue fallback, `hv_update_fiq`
  timer queue), `src/hv_vgic_diag.c`, `src/hv_nvme.c`, `src/hv_irq_routes.c`.
- History: commit `6a2293b8` (Aug 3) introduced `hv_vgic_diag_priority_deliverable`
  with running priority deliberately ignored. `update_vi` had never enforced it before either.

## Corrections to the EXP1890 reading

1. vINTID 64 is not an unrouted AIC IRQ. It is `HV_IRQ_SYNTHETIC_NVME_VINTID`, the
   emulated NVMe INTx, injected only on the boot CPU by `hv_nvme.c try_raise_intx`.
2. The stale LR did not block the timer. `hv_vgic_diag_priority_deliverable` ignored
   the running priority, and CPU0's `last_iar=18`/`last_eoi=18` timestamps advance
   between the snapshots (0x565914de0 -> 0x5d91cd6d8), so the vtimer kept being
   acknowledged and EOIed at EL0.
3. `iar=eoi=734917` are SGI counters (`hv_sgi_diag`), not counters for all interrupts.
4. CPUs 1-7 sit at the same kernel PC with DAIF masked and their timers disabled
   (`vctl=0x6`). That looks like the idle WFI. This is a hypothesis; the snapshot
   alone does not prove they are waiting for CPU0.

## Windows contract (ntoskrnl, measured from the dump)

- GIC HAL table (object VA 0xfffff7aa80001848): Accept at +0x58 -> RVA 0x4acda0
  (`mrs ICC_IAR1_EL1`); EndOfInterrupt at +0x60 -> RVA 0x4af3f0 (`msr ICC_EOIR1_EL1`).
  The Deactivate slot is NULL, so Windows uses EOImode 0 (VMCR.VEOIM=0 in every snapshot).
- PMR: the only runtime writer is RVA 0x4af1c0, PMR = 0xf0 - 16*IRQL, and no caller
  was found. Every snapshot of every CPU shows VPMR = 0xf0. **Windows does not mask
  by PMR; IRQL is lazy and software-only** (`[x18+0x38]`).
- Dispatcher (RVA 0x2b6714): the INTID is looked up in the controller line table,
  vector = entry+0x30, and IRQL = table[vector>>8 & 0xf] at RVA 0xe066b0
  `{0,255,2,255,255,255,255,255,8,...,15}`.
  - current IRQL < vector IRQL: raise, run the ISR, EOI (unconditional), then lower
    by a plain store (RVA 0x65ebe8).
  - current IRQL >= vector IRQL (RVA 0x2b699c): **defer**. The INTID stays ACTIVE
    with no EOI. The vector goes to PRCB+0x712[n] and the raw IAR to PCR+0x88[n],
    count at PRCB+0x710, maximum 15, and the 16th deferral is KeBugCheck(0xC8)
    (RVA 0x2b6a38).
  - Replay happens only in the KeLowerIrql path (RVA 0x2070c8 -> loop 0x207178). It is
    LIFO and stops at the first entry whose IRQL <= the new IRQL.
  - **The ISR exit path never replays** (RVA 0x2b68e4..0x2b6944). When it returns
    below DISPATCH and the deferral count is non-zero, it also **skips software
    interrupt (DPC/dispatch) delivery** (`cbnz` at 0x2b6934).
- On hardware this is safe. A deferred interrupt is ACTIVE, and its running priority
  blocks every equal- or lower-priority interrupt. So nothing can be deferred *inside*
  a hardware ISR, and deferred entries are drained by the KeLowerIrql that ends the raised
  section.
- Line table (EXP1889 boot): 64 -> 0x800 (IRQL 8), vtimer 18 -> 0xd01 (13), SGI0
  0xe01, SGI1 0xd02, SGI2 0xf05, xHCI 857 0xa00, 865 0xb00. PPIs 16/17/19 and
  vINTIDs 880/881/886/888 -> vector 0 (IRQL 0).

## m1n1 contract before the fix

`update_vi` and `do_iar1` signalled/acknowledged any Pending LR with priority < PMR,
whatever was ACTIVE. With Windows' PMR fixed at 0xf0, every device interrupt was
offered inside every ISR.

## Root cause

The NVMe INTx (LR priority 0x80, IRQL 8) was offered while the clock ISR ran on CPU0
(vtimer ACTIVE, IRQL 13). Windows acknowledged it, saw IRQL 13 >= 8, and deferred it
(ACTIVE, no EOI). The clock ISR then returned to EL0 by a plain IRQL store, without
replay and without DPC delivery. While the user thread only spins (GravityMark scene
generation, dxilconv in EXP1876G), nothing calls KeLowerIrql. So the entry is never
replayed, LR 64 stays ACTIVE, and NVMe `outstanding` stays set (no I/O completion
interrupts). DPCs and quantum-end dispatch on that CPU are suppressed, so the
user thread is never preempted. This matches EXP1890 exactly (CPU0 at EL0, PMR 0xf0,
LR0 = 64 ACTIVE, timer still delivered, whole guest hung, no bugcheck because the
stuck CPU is the one that would run the watchdog). On a secondary CPU, the same lost
DPC/clock processing gives the 0x101 of EXP1876G and EXP1889. It needs CPU-bound user
code: any syscall's KeLowerIrql replays the entry. This is probably also why EXP1877
(CPU-only load, little device interrupt traffic) stayed clean (hypothesis).

Earlier snapshots show the same chronic state: ACTIVE 64/886/888 on CPU0 for minutes
(EXP947, EXP959, EXP966, EXP872 at EL0, EXP910).

## Answers to the task questions

- *ACTIVE with no IAR outstanding / surviving EOIR*: neither. Windows did read IAR=64
  and, by design, never wrote EOIR (deferred and not replayed). `do_eoir1` (match by
  INTID among ACTIVE LRs), `hv_vgic_diag_eoi_lr`, the EISR maintenance clear and the
  drain paths are correct for EOImode 0. ICC_DIR is unused (no Deactivate in the
  HAL table).
- *0x40 queue priority* (`hv_exc.c` fallback): a real defect. Once preemption is
  enforced, it would make a queued line outrank its own IRQL and nest. Fixed in
  `db990f95`. It is dormant in EXP1890 (`iq=0`).
- *timer_queue drained only in the maintenance branch*: a latent stall, not this one
  (`tq=0` on every CPU). If all LRs are busy, the timer is queued, and
  `timer_irq_outstanding()` counts the queued entry, so `hv_update_fiq` never
  re-injects. Only a maintenance-type AIC event drains it. A drain at EOI was rejected
  earlier (`test_timer_eoi_does_not_add_the_rejected_level_state_drain`). Follow-up,
  not changed here.

## Fix

`hv_vgic_diag_priority_deliverable(prio, pmr, rp)` = `prio < pmr && prio < rp`.
Windows writes BPR1 = 3 and the vGIC reports five priority bits, so group priority
equals the implemented priority and a raw comparison is exact. Regression:
`tests/hv_vgic_diag_test.c` (`test_priority_is_masked_by_pmr_and_running_priority`
RED on 21c3648d at the EXP1890 case, GREEN after;
`test_recovery_wake_respects_running_priority`) and
`tests/test_vgic_irq_queue_contract.py::test_queued_hardware_irq_keeps_the_guest_programmed_priority`.
Host tests: 76 pass. The same 3 tests fail to compile with clang 22 both on HEAD and
on the fix (asc_tx_gate, hv_autonomous_manifest, hv_bootstrap_manifest).

## Residual risk (hypothesis, not fixed here)

Interrupts disabled in the GIC are still delivered:
- `hv_exc_irq` does not check the enable bit;
- ICENABLER does not retract a Pending LR;
- `do_iar1` does not check enable.

Windows maps an unconnected line to vector 0 = IRQL 0, which is deferred at any IRQL
and can never be replayed. A permanently ACTIVE LR results (candidate for EXP966's
886/888, which have vector 0 in the EXP1889 boot). With preemption now enforced, such
an LR would block every equal- or lower-priority interrupt on that CPU instead of
leaking. Watch for it in the discriminator. The follow-up is to withhold or retract
Pending LRs of disabled INTIDs at IAR and in `update_vi`, with NVMe `outstanding`
and the AIC mask released.

## Hardware discriminator (proposed; run by the GPU session)

WHY THIS HYPOTHESIS:
1. EXP1890: LR 64 ACTIVE at EL0 with PMR 0xf0 for 35 s, timer still taken.
2. ntoskrnl defers interrupts at IRQL <= current without EOI, replays them only in
   KeLowerIrql, and its ISR exit path neither replays nor delivers DPCs while one is
   pending. That is safe only under GIC running-priority preemption, which m1n1 did
   not enforce.
3. The failure needs CPU-bound user code: EXP1876G and EXP1889 had 0x101 with CPUs in
   user code, and EXP1877 (CPU load without device traffic) was clean.

- Single variable: m1n1 `21c3648d` -> `db990f95` (macho `1bb6a818…`). Same package,
  Mu, profile and launcher as EXP1890.
- Run: boot, the usual health checks (SSH, DWM, NVMe I/O, USB input), then GravityMark
  D3D11 1280x720 (scratchpad `gm-d3d11-1890.ps1` + `gm-run.ps1` via arm-task),
  which reproduced the defect 2/2.
- CONFIRMED if GravityMark passes scene generation into frames or results, with no
  0x101 or hang, and SSH stays alive.
- REJECTED if the hang or 0x101 recurs. Take two SIGINT HV WATCHDOG snapshots. An
  ACTIVE LR whose INTID has a non-zero vector rejects the fix. An ACTIVE LR of an
  unconnected INTID (vector 0) points to the residual risk above.
- Regression watch: boot to desktop and DWM, xHCI input, display, NVMe throughput.
- Recovery: the EXP's rec<N>h/recover.sh (`AGX_RECOVERY_ALLOW_AT_PROXY=1` after a
  reset). Previous m1n1 `.local/experiments/m1n1-21c3648d-rgba/m1n1-g2-abi6-iomfb-owner.macho`
  (`a66610f7…`).

## EXP1894 (db990f95): boot livelock, and the follow-up fix 062ea6bd

Evidence: `.local/experiments/EXP1894-vgic-rp-gm/evidence/EXP1894/full-boot-hang-snapshots.log`.
On CPUs 2, 4, 5, 6 and 7:
- LR0 = SGI0 ACTIVE (0x10) and LR1 = vtimer 18 PENDING (0x20).
- HCR_EL2.VI = 0 and DAIF is masked.
- The PCs sit on the IRQ entry path, not in a spin loop. With kernel base
  0xfffff8023b400000 they map to:
  - RVA 0x63f914, after `mrs MDSCR_EL1` in the exception prologue;
  - RVA 0x505540, after `mrs PMCCNTR_EL0` in interrupt trace logging;
  - RVA 0x4acda4, after `mrs ICC_IAR1_EL1`.

CPU0 spins at RVA 0x25a5b8 on a per-PRCB flag (waiting for the IPI targets). CPUs 1
and 3 are in the idle WFI (RVA 0x4811a8).

So the vCPUs kept taking IRQ exceptions while VI was clear. The only other source is
the hardware virtual CPU interface, enabled by ICH_VMCR.VENG1 = 1:
- it signals any Pending LR under VPMR;
- its ICH_AP1R is always empty because IAR and EOIR are emulated.

The emulated IAR now correctly refused the timer behind the ACTIVE SGI and returned
spurious, so Windows re-entered at once. That livelock left the IPI handler unable to
finish. Before aca9fd5d the emulated IAR accepted whatever the hardware signalled, so
the disagreement was invisible. The 2026-07 comment that the M1 interface "does not
signal on its own" is contradicted by this run.

Fix 062ea6bd keeps VMCR.VENG0/VENG1 clear, so the virtual IRQ comes only from
HCR_EL2.VI via `hv_vgic3_update_vi()` (PMR plus running priority), and update_vi no
longer gates on VENG1, which was always 1. Regression:
`test_hardware_cpu_interface_cannot_signal_behind_running_priority` is RED on db990f95
and GREEN on 062ea6bd.

Artifact: `.local/experiments/m1n1-062ea6bd-vgic-rp-vi/m1n1-g2-abi6-iomfb-owner.macho`,
SHA-256 `9e87ee7393c735d7ed5cbc1515d8a2eb2a9b293b006e764cfb1af23fff154cc7`
(config a01c4ca9).

WHAT IS STILL UNKNOWN:
1. Whether Apple's interface honours VENG1 = 0 (architecturally it must).
2. Whether some LR transition relied on the hardware signal because it does not call
   update_vi (every LR writer found does call it).
3. The residual GICD-disabled delivery risk above.

Discriminator: boot to SSH first. The failure criterion is a boot hang or a watchdog
snapshot with VI = 0 while a vCPU is on the IRQ entry path. Then run GravityMark as
planned for EXP1894.
