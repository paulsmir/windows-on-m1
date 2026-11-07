# ANS-DIRECT — Windows owns the ANS NVMe runtime; m1n1 leaves the data path (2026-11-07)

Status: design, offline implementation in progress. No hardware run yet.

## Goal

The user's request (2026-11-07):
- Windows I/O must reach the SSD with no m1n1 (EL2) code in the data path.
- The driver writes exactly the sectors Windows asks for, like any ordinary storage driver.
- There is no per-command EL2 broker and no EL2 LBA window.

Reason: m1n1 can be wrong too, and the broker costs about 164 µs per command (ANS010-R7, hw avg). That caps throughput far below macOS.

## Sources inspected

- **Live/recorded state:**
  - ANS010-R7 broker perf: n=1641, 40 MB, hw avg 164 µs.
  - `config/j313-ans.json`: controller 0x27bcc0000+0x40000; ASC 0x277400000+0x4000; mailbox 0x277408000+0x4000; SART 0x27bc50000+0x10000; physical IRQ 590; guest GSIV 889.
  - `config/j313-guest-layout.json`: guest RAM 0x850000000..0xa00000000 is identity. The low window IPA 0x100000+0x3ff00000 is backed by PA 0x8a0100000 (`hv_autonomous_runtime.c` `hv_map_hw(low_mem_ipa, low_mem_pa, ...)`, `hv_vm.c hv_ipa_to_pa` comment, `hv_vgic.c` note that Windows uses it as ordinary RAM).
- **Asahi** `drivers/nvme/host/apple.c` (pinned copy `.local/ans-native/references/apple.c`, GPL: read for behaviour only, nothing copied):
  - `apple_nvme_reset_work`: after RTKit boot and BOOT_STATUS == 0xde71ce55:
    1. LINEAR_SQ_CTRL=EN;
    2. MAX_PEND_CMDS = depth | depth<<16;
    3. NVMMU_NUM_TCBS = depth-1;
    4. AQA/ASQ/ACQ;
    5. NVMMU ASQ/IOSQ TCB bases;
    6. CC enable;
    7. Identify;
    8. Create IOCQ (vector 0), Create IOSQ.
  - `apple_nvme_submit_cmd_t8103`: SQE in slot `tag`; TCB[tag] = {opcode 0, dma_flags TO/FROM/none by PRP1 and direction, command_id tag, length = rw.length, prp1, prp2}; linear doorbell = tag, written under the IRQ lock (comment documents missed completions otherwise).
  - `apple_nvme_handle_cqe`: phase check, NVMMU TCB_INVAL(tag) then advisory TCB_STAT read, CQ head doorbell.
  - `apple_nvme_disable`: the hand-off convention of "U-Boot and m1n1": an ANS controller is handed over **with firmware shut down, in an NVMe-disabled state, after a clean shutdown**. Disabling and re-enabling without a firmware reboot is documented as unreliable.
  - Limits: 4 MiB, 127 segments, `dma_set_mask 64`. No IOMMU on payload DMA. SART only guards RTKit shared buffers.
- **m1n1** (`m1n1_windows/src`, MIT, our tree):
  - `nvme.c nvme_init_common(native)`: `pmgr_reset(ANS/ANS2)`, `rtkit_boot_bounded`, BOOT_STATUS, linear SQ, MAX_PEND, NVMMU, disable, ASQ/ACQ/AQA, enable.
  - `asc.c`: mailbox = ASC base+0x8000. I2A CONTROL 0x114 bit17 EMPTY, RECV0/1 0x830/0x838. A2I CONTROL 0x110 bit16 FULL, SEND0/1 0x800/0x808.
  - `rtkit.c rtkit_recv_one_dispatch` strict post-boot set: syslog LOG and ioreport 0x8/0xc are echoed; MGMT IOP/AP power ACKs are recorded; a crashlog buffer request after boot means the IOP crashed.
  - `ans_queue.c`: SQE/CQE/TCB layouts, 64/16/128 bytes.
  - `hv_ans_service.c ans_service_handoff`: freeze/drain/flush the bridge → `stop` (RTKit sleep) → `retire` (RTKit/SART free) → hide.
  - `hv_irq_routes.c`: level IRQ passthrough with guest EOI.
- **Mu:**
  - `AcpiTables/J313AppleAns.asl.inc`: ANS0, `_HID APPL0003`, `_CCA 1`; one QWordMemory broker page plus `Interrupt 889 Level`.
  - `DSDT.asl` `J313_ANS_NATIVE_ACPI`.
  - The EBS gate validates the handoff receipt (proof flags 127).
- **Microsoft:**
  - StorPort `PORT_CONFIGURATION_INFORMATION` (`NumberOfAccessRanges`, `AccessRanges`, `BusInterruptVector`);
  - `StorPortGetDeviceBase`, `StorPortGetUncachedExtension`, `StorPortGetPhysicalAddress`, `StorPortGetScatterGatherList`;
  - `StorPortNotification(RequestTimerCall)`, HW_TIMER;
  - full-duplex InterruptLock rules (as already recorded in `windows/NATIVE_PATH.md`).
  - `_CCA=1`: Windows treats the device as cache coherent, so it needs no cache maintenance for DMA.

## Contract (who owns what)

| Phase | Owner | What |
| --- | --- | --- |
| Mu DXE → winload (pre-EBS) | m1n1 vNVMe bridge (unchanged) | bootmgr/winload I/O |
| EBS handoff | m1n1, once | freeze/drain/flush bridge → stop+retire (proven) → **cold boot to the Asahi hand-off state** → passthrough → receipt → never touch ANS again |
| Kernel runtime | AppleAns direct | NVMe registers, NVMMU/TCBs, admin + I/O queues, PRPs, IRQ, ASC mailbox runtime replies, shutdown |

### Cold boot to the hand-off state (EL2, once)

Asahi expects firmware running and NVMe disabled. Our handoff state:
- `pmgr_reset(ANS|ANS2)`;
- RTKit booted (`rtkit_boot_bounded`), BOOT_STATUS == OK;
- CC.EN=0 and CSTS.RDY=0, verified;
- NO linear-SQ/NVMMU/AQA programming, no queues.

This is exactly the proven prefix of `nvme_init_common(native)` up to "ANS booted". RTKit syslog/crashlog/ioreport buffers stay allocated in EL2 memory for the life of the guest; SART keeps their entries. The firmware writes logs there; nothing reads them.

### Passthrough

- **Stage-2:** controller 0x27bcc0000+0x40000 and mailbox 0x277408000+0x4000 are identity-mapped, device-nGnRE, guest RW.
  - The broker page hooks are removed.
  - The ASC CPU-control page (0x277400000) and SART stay **private**, so Windows cannot stop the IOP or rewrite SART.
- **IRQ:** route 590 → 889 level passthrough (`hv_irq_route_register`). EL2 no longer injects synthetic ANS IRQs.
- **ACPI direct variant:**
  - `_CRS` = QWordMemory controller, QWordMemory mailbox, Interrupt 889 Level;
  - same `_HID APPL0003`, `_UID 0`, `_CCA 1`;
  - so the persisted devnode/StartOverride contract (ANS010) is unchanged.
  - The driver selects broker vs direct mode from the resource shape: 1 range at the broker base, or 2 ranges at the expected controller and mailbox bases.

### DMA addresses

Windows logical addresses are guest IPAs (no IOMMU, `_CCA 1`). The driver converts each PRP/queue page IPA to PA with the firmware-published table:
- `[0x100000, 0x40000000) → +0x8a0000000`;
- everything else in guest RAM is identity.

The constants live in a header that a test checks against `config/j313-guest-layout.json`. A page that straddles a window edge is impossible (edges are 1 MiB aligned, pages are 4 KiB), but it is still rejected. The constants are checked before Initialize returns.

### Driver runtime (Asahi behaviour, written from scratch)

- **Queues:**
  - admin depth 2 (Asahi `APPLE_NVME_AQ_DEPTH`);
  - I/O depth 64 with tag = slot (t8103 linear SQ);
  - NVMMU_NUM_TCBS 63;
  - MAX_PEND 64|64<<16.
- **Memory:** one uncached extension, 16 KiB aligned arrays:
  - SQ 4 KiB, CQ 1 KiB, TCB 8 KiB per queue;
  - per-tag PRP-list pages;
  - an Identify page.
  - Every page's IPA is verified, then translated.
- **Submission:** fill the TCB and SQE, `dma_wmb`, doorbell = tag, all under InterruptLock (ISR serialization is the Asahi missed-IRQ rule).
- **Completion (ISR):** phase check, tag validation, TCB_INVAL + advisory TCB_STAT, head doorbell, then complete the SRB. The ISR also drains the ASC mailbox.
- **Mailbox:** an idle 50 ms HW timer drains the ASC mailbox (runtime responder set above). A crashlog request or an unknown system message latches the adapter faulted, and all I/O fails. No reboot of the IOP from Windows.
- **Transfers:** up to 512 KiB (129 PRP entries, one list page). Capped by Identify MDTS; Initialize fails if MDTS < 128 KiB.
- **Shutdown:** NVMe Flush, then CC.SHN=normal, wait SHST=complete, CC.EN=0.
- **Reset:**
  - CC-only re-init is the Asahi-labelled unreliable case. ResetBus therefore completes outstanding SRBs with BUS_RESET after a CC disable and re-enable, and treats a failed re-enable as terminal (adapter faulted).
  - No IOP reboot (owning PMGR/SART would violate the layer boundary).

## Differences vs Asahi / broker

- Asahi reboots RTKit itself. We inherit a freshly booted IOP from m1n1, so PMGR, SART and RTKit boot stay with firmware, like PC firmware initializing a controller.
- There is no EL2 LBA window. Safety now rests on Windows partitioning and the driver's capacity check, as on any PC.
- The broker's QD and size receipts disappear. The driver's telemetry (existing IOCTL) stays.

## What is still unknown (hardware decides)

1. Whether IOP mailbox traffic is rare enough for an ISR + 50 ms poll responder, or whether unanswered syslog stalls I/O. Measured by mailbox message counters in telemetry.
2. Whether the ~seconds between EBS and driver Initialize, with nobody servicing the mailbox, are harmless. The same gap already existed under the broker (EL2 only polled on commands).
3. Throughput with direct submission vs the broker (diskspd).

## Smallest falsifiable checkpoint (ANS-DIRECT-R1)

Installed native boot (ANS010 profile) with direct mode:
- **Pass:**
  - SSH answers;
  - APPL0003 runs in direct mode (telemetry mode=direct, 0 faults);
  - probe write/flush/readback;
  - diskspd seq 1 MiB QD32 read/write and 4 KiB random;
  - clean reboot;
  - recovery: probe hash, chkdsk clean.
- **Fail-safe outcomes:**
  - Initialize fails before CC enable → INACCESSIBLE_BOOT_DEVICE with no writes.
  - Hang → runner SIGINT snapshot (PCs, KiBugCheckData, devnodes, modules).
- **Recovery:** the unchanged `launch-normal2.sh` bridge profile. AppleAns Start=3 and StartOverride cleared by `ans-boot-mode.ps1 recovery`.
