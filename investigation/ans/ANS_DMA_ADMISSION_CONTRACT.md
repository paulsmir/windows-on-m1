# ANS DMA admission contract (t8103 / J313) — 2026-11-06

Status: offline contract and design. No hardware was accessed for this
document. Evidence classes follow AGENTS.md: OBSERVED (live J313 record),
SOURCE (current repository source), SPEC (official documentation),
RE (macOS 15.7.8 kernelcache, behaviour reference only, no code copied),
HYPOTHESIS (explicitly unproven).

## 1. Sources inspected

| Source | What it establishes |
| --- | --- |
| `m1n1_windows/src/nvme.c` (ANS m1n1 `ba1cfa1`) | ANS register map, linear-SQ + NVMMU/TCB programming, PRP = physical address, SART-only filtering of RTKit buffers |
| `m1n1_windows/src/ans_queue.h` | `struct apple_nvmmu_tcb` layout used by the broker |
| `m1n1_windows/src/sart.c`, `src/rtkit.c:265-300` | SARTv2/v3 entry layout, iBoot-protected entries, which buffers enter SART |
| `m1n1_windows/src/dart.c` | t8020/t8110 DART TTBR/PTE layout, `locked`/`keep_pts` import of inherited TTBRs, L1/L2 allocation through `memalign` (heap) or `pt-region-N` |
| `m1n1_windows/src/heapblock.c`, `src/dlmalloc/malloc_config.h` | every m1n1 C allocation (incl. DART tables) comes from one upward non-freeing heapblock with no ceiling |
| `m1n1_windows/proxyclient/m1n1/proxyutils.py:55-80` | Python heap = heapblock(0)+128 MiB; m1n1 C heap is assumed to stay below it |
| `m1n1_windows/proxyclient/m1n1/hv/__init__.py:2105-2165` | stage-2: RAM-LOW `[ram_base, ba.phys_base)` and RAM-HIGH `[phys_base, DRAM end)` mapped pass-through; only MCC TZ carveouts unmapped; `[ba.phys_base, phys_base)` (m1n1 image + both heaps) not mapped |
| `run_uefi.py:215-260` | assisted descriptor: GUEST_RAM `[phys_base, ba.phys_base+mem_size)`, DART_TABLES `flags=1 size=0` |
| `m1n1_windows/src/hv_ans_memory.c`, `hv_ans_platform.c` | current admission: stage-2 raw leaf (S2AP R/W), region-0 containment, reservation/private exclusion; policy rejects any region with flags |
| `m1n1_windows/src/hv_launch_golden_j313.c:55-77` | golden J313 geometry: GUEST_RAM `0x850000000+0x18f708000`, FB `0x85f000000+0xfa0000`, LOW_MEMORY `0x8a0100000+0x3ff00000` |
| `mu/Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c` | Mu reserves framebuffer, CPU-stack carveouts, RAM disk, low DRAM window; no generic ADT carveout import |
| `investigation/EXPERIMENTS.md` EXP464 | OBSERVED: GPU context0 TTBR0/1 = `0x9d71a8000/0x9d71a0000` — inside GUEST_RAM; shows tables of other IOMMUs do live in that window |
| Asahi Linux `AsahiLinux/linux` branch `asahi` @ `6ba0637d1ed9a2e3639dd3fc9eca6cd0483a6434` (fetched 2026-11-05, read-only, scratch copy outside repo): `drivers/nvme/host/apple.c` sha256 `aa43ad90…ed339`, `drivers/soc/apple/sart.c` `9de8b3fc…08ff`, `arch/arm64/boot/dts/apple/t8103.dtsi` `ba73aa7f…fce7` | t8103 `nvme@27bcc0000` (`apple,t8103-nvme-ans2`) has **no `iommus`**, only `apple,sart = <&sart>` (`sart: iommu@27bc50000`, `apple,t8103-sart`), IRQ AIC 590, mailbox `0x277408000` IRQs 583-586, power domains `ps_ans2` + `ps_apcie_st`; `dma_set_mask_and_coherent(64)` (apple.c:1537); SART only for RTKit shmem (apple.c:268-306); NVMMU constants identical to m1n1 native (TCB_STAT `0x28120`); TCB dma_flags FROM_DEVICE=bit0, TO_DEVICE=bit1 (apple.c:107-108); `apple,t8103-sart` → SARTv2 (sart.c:355-356). apple.c:85-103 documents the intended model: a higher exception level owns the NVMMU, shadow-copies PRPs and verifies they do not point at kernel text/data/page tables before programming the TCB — exactly the EL2 broker design here. |
| macOS 15.7.8 kernelcache: IONVMeFamily, AppleSART, AppleT8020DART, IODARTFamily | see §6 (RE notes, file `side-session-notes/.../ans-macos-re-2026-11-06.md`) |
| Microsoft StorPort docs (listed in `drivers/storage/appleans/windows/NATIVE_PATH.md`) | Windows owns SRB, SG list and guest DMA addresses; no IOMMU contract is required by StorPort |

## 2. What "DART_TABLES" actually protects

SOURCE: ANS has **no DART** on t8103 (Asahi t8103.dtsi: no `iommus` on the
NVMe node; m1n1 likewise). `nvme.c` writes physical addresses into
PRP1/PRP2/PRP lists and into the NVMMU TCB (`nvme.c:444-455, 707-714`); the
only address filter is `/arm-io/sart-ans`, and `rtkit.c:265` adds only RTKit
firmware shared-memory buffers to it. NVMe payload DMA is therefore an
unfiltered physical bus master once a TCB is published.

So the UNOBSERVED marker is not "the ANS IOMMU state is unknown". It is: the
set of *other devices'* DART page-table pages that a mis-authorized ANS write
could corrupt is not bounded. Two populations exist:

1. **m1n1-owned tables** — `dart_init()` L1 and `dart_get_l2()` L2 pages via
   `memalign` → dlmalloc → `sbrk` → `heapblock_alloc`. Includes DARTs m1n1
   opens late (xHCI `dart-usb1` in `hv_vgic.c:116`, DCP/disp in `dcp.c:911-932`).
   They live in `[heapblock base, heapblock top)` ⊂ `[ba.phys_base, phys_base)`,
   which stage-2 does not map — **provided heapblock never grows past the
   Python heap base / guest base**. Today that is a convention (128 MiB), not
   an enforced bound.
2. **Retained (iBoot-owned) tables** — TTBRs already valid at m1n1 entry
   (`dart.c:240-250`, `locked` or `keep_pts`) and ADT `pt-region-N`. Their PAs
   are not derivable from source; they must be measured.

Guest-owned tables (e.g. the AppleAgx KMD UAT at `0x9d71a0000`, EXP464) are
inside GUEST_RAM by design; DMA into them is the guest's own decision, exactly
like a CPU store, and is not a hypervisor isolation question.

## 3. The invariant that removes UNOBSERVED

**ANS DMA authority ⊆ guest CPU write authority.** A PA may be placed in a
PRP/TCB only if (a) the guest IPA's authoritative stage-2 leaf grants the
needed S2AP bit (already implemented: `ans_memory_translate`), (b) the PA lies
in GUEST_RAM and outside every published reservation (implemented), and (c)
**no page that the hypervisor or iBoot still relies on is guest-writable
inside GUEST_RAM**. (a)+(b) make DMA no stronger than a guest CPU store; (c) is
what DART_TABLES must prove, and it is a stage-2/Mu-memory-map property, not
an ANS property (AGENTS.md: fix in the owning layer).

DART_TABLES becomes `flags=0` only when all three hold:

- **A. m1n1 heap ceiling (offline, ANS005A).** `heapblock` gets a ceiling set
  once before guest entry (assisted: Python heap base; standalone: the
  autonomous layout's guest base). `heapblock_alloc_aligned` panics before
  returning a block that crosses it (also on overflow). DART_TABLES region :=
  `[heapblock_init base, ceiling)`, published with exact bounds. Every later
  allocation (xHCI DART L2, RTKit buffers, broker state) is inside it by
  construction, which is the "future owners" half the 2026-09 audit asked for.
  `run_uefi.py` must also assert `phys_base >= natural` (Python heap top),
  which it does not today. RED/GREEN: allocator crossing test + descriptor
  test (`tests/hv_ans_handoff_memory_test.py`) that the published region is
  exact, flags 0, disjoint from GUEST_RAM.
- **B. Retained-table census (hardware, read-only, ANSDART001).**
  `tools/ans_dart_census.py` at `Running proxy`, before hv_init: read PMGR PS
  state of each DART's clock gates; only for fully powered DARTs read
  TTBR[0..15][0..3] and the L1 page, list every L1/L2 page, classify vs
  GUEST_RAM. Unpowered DARTs are not read (gated DARTs lose TTBR state; a
  later owner is population 1 or the guest). t8110/unknown → INCOMPLETE.
  Verdicts: `NO_RETAINED_TABLE_IN_GUEST_RAM` → retained pages are outside the
  DMA window; `CONFLICT` → a retained table is guest-writable today (a
  pre-existing CPU isolation defect): fix in m1n1 stage-2 (unmap) and Mu
  memory map (EfiReservedMemoryType) first, DMA stays denied;
  `INCOMPLETE` → stays denied.
- **C. Encode the census.** The census receipt hash is published into the
  launch contract as the DART_TABLES evidence and every retained page (if any
  are inside RAM after the fix) is added to the ANS private exclusion list
  (`ANS_MEMORY_MAX_PRIVATE_RANGES` = 64 already exists). The policy keeps
  rejecting any `flags != 0` region; no flag is cleared by assertion.

What still cannot be closed offline: the retained TTBR values (B). Everything
else is deterministic source work.

## 4. ANS command-protocol contract (SOURCE: m1n1 `nvme.c`, `ans_queue.h`)

| Item | Value | Owner |
| --- | --- | --- |
| Controller MMIO | ADT `/arm-io/ans` reg 3 (`nvme.c:323`) | EL2 only |
| Boot status | `0x1300` == `0xde71ce55` after RTKit boot | EL2 |
| Linear SQ | `0x24908` bit0 enable; doorbells ASQ `0x2490c`, IOSQ `0x24910` (write = tag) | EL2 |
| Max pending | `0x1210` = `(64<<16)|64` native (`(63<<16)|63` legacy) | EL2 |
| PRP NULL check | `0x24008` bit11: legacy clears it; native path keeps it set | EL2 |
| NVMMU | `0x28100` TCB count-1 (63); ASQ TCB base `0x28108`; IOSQ TCB base `0x28110`; invalidate `0x28118` (write tag); status `0x28120` (native; legacy reads `0x29120`) | EL2 |
| TCB | 128 B: opcode, dma_flags (bit0 from-device, bit1 to-device), command id, length, prp1 @+24, prp2 @+32, AES IV @+56 (`ans_queue.h:24-29`, Asahi apple.c:104-120); every command, even non-DMA, needs a TCB; admin opcode 0xD8 buffers go through SART, not PRPs (apple.c:85-91) — the broker admin allowlist already denies it (`hv_ans_service.c:135-158`: only 0x06 Identify, 0x05/0x01 create IO CQ/SQ, 0x0a WCE get) | EL2 builds from validated PRPs |
| Queues | admin + one IO queue, depth 64, CQ doorbells `0x1004`/`0x100c` | EL2 |
| SART | SARTv2 on t8103 (Asahi sart.c:355; m1n1 picks by ADT `sart-version`, recorded by ANSDART001): cfg `0x00+4i` flags[31:24] size[23:0]<<12, paddr `0x40+4i`; 16 entries; entries with flags at init are iBoot-protected and never freed (`sart.c:149-170`) | EL2 (RTKit buffers only) |
| DMA lifetime | a PA is pinned from TCB publication until completion **and** TCB invalidate status success; stage-2 changes to pinned IPAs are refused (`hv_ans_mapping_begin`) | EL2 |

Per-command sequence (broker, EL2): validate SRB-derived request → translate
every PRP page through §3 → copy PRP list into EL2 shadow page → write SQE and
TCB → `dma_wmb` → linear doorbell (tag) → completion via IRQ/poll → TCB
invalidate + status check → unpin → response to Windows. A failed invalidate
or stop with outstanding pins quarantines the guest (`hv_ans_platform.c:85`).

## 5. Layer ownership

| Concern | Owner | Notes |
| --- | --- | --- |
| ANS power, RTKit, SART, controller enable/disable, admin/IO queues, TCB, doorbells, IRQ | m1n1 EL2 broker | Windows never touches ANS MMIO under the hypervisor |
| DMA admission (stage-2 leaf, reservations, census exclusions, pins) | m1n1 EL2 | this document |
| Retained-table reservation in the UEFI memory map | Mu (`MemoryInitPeiLib`) | needed only if the census finds a conflict |
| Handoff from the synchronous recovery NVMe bridge | m1n1 + Mu EBS gate | ANS001 (already sealed) |
| SRB/SG, SCSI translation, ordering, completion | AppleAns StorPort miniport | `drivers/storage/appleans/windows` |
| Recovery | GPU-visible recovery profile + existing synchronous bridge | unchanged |

Without the hypervisor (future): the same miniport would own the §4 sequence
directly (no broker); §3(a)/(b) disappear because Windows owns all RAM, and
§3(c) reduces to Mu reporting every retained-table page as reserved — the
census verdict is exactly the input that Mu change needs.

## 6. macOS RE (behaviour reference)

See the separate RE notes; summary is merged in `investigation/ANS_CURRENT_STATE.md`.

## 7. Smallest falsifiable hardware checkpoint

ANSDART001 (preregistered in `investigation/EXPERIMENTS.md`): read-only census
at `Running proxy`, no image change, no guest, no ANS access. Recovery: none
needed beyond continuing the normal launch (no state is written); if the proxy
faults, power-cycle to the existing recovery profile.
