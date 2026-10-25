# T14 offline recovery contract and plan

Scope: assigned worktree only; no hardware, guest, package, or other worktree
access. EXP1939 is saved evidence, not a new measurement. Experiment/change
ledgers are left to the integration owner as the task README requires.

Primary sources inspected:

- Asahi `gpu.rs:1006-1040,1059-1097,1422-1441`: classify pending work errors,
  clear pending records, wait for halt, then request resume.
- Asahi `workqueue.rs:257-263,850-940,945-975`: GPU doneptr counts completed
  work items; mark_error annotates commands, while signal retires by stamps.
  This does NOT establish that writing resume means DMA has stopped.
- Asahi `fw/workqueue.rs:81-101`, `fw/initdata.rs:36-44`, `regs.rs:374-493`:
  distinct done/read/write pointers, halt/resume layout, G13 FAULT_INFO.
- Available m1n1 object-store HEAD c6d10e04, `proxyclient/m1n1/agx/__init__.py:242-354`:
  clear pending records, halt/resume writes, no independent resume acknowledgment.
- Available Mu object-store HEAD f1ef718e, generated
  `Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgx.asl.inc:2-58`:
  APPL0002/coherent resources/G13 V13_5; no runtime recovery service.
  Exact pinned submodule commits are unavailable; these HEADs are not claimed
  to match EXP1939's hardware manifest.
- WDK 26100 `d3dkmddi.h:1062-1076,8242-8260,8333-8341`:
  DMA_PAGE_FAULTED identifies fault fence, VA (zero for non-VA errors), node,
  engine; ResetEngine returns LastAbortedFenceId. Official offline references:
  https://learn.microsoft.com/en-us/windows-hardware/drivers/display/tdr-changes-in-windows-8
  https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ne-d3dkmddi-_dxgk_interrupt_type
  https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_resetengine
  DMA_FAULTED is reserved for system use; use DMA_PAGE_FAULTED for these errors.
- KMD baseline `scheduler_windows.c:529-550,572-574,625-649`,
  `backend_platform_windows.c:2209-2291,3142-3164,3300-3310,5099-5197`,
  shared `apple_agx_g13_queue_runtime.c:576-636`: reset polls active work;
  recovery requires stamps AND done pointers; dropped work currently uses
  CompleteActiveFence, conflating aborted work with successful completion.

Ownership: firmware/m1n1 owns initial power and GPU execution; Mu exposes the
resources; KMD owns transport, DMA lifetime, queue bookkeeping, fault interrupt
translation and recovery; Windows owns device removal and TDR. No firmware,
ACPI, IRQ, MMIO mapping, caps, signer or recovery-profile changes are planned.
The saved EXP1939 manifest pins m1n1 0d3a75b3 and Mu J313_EFI-m2f2c.fd;
there is no new claim about assisted or standalone launch validation.

Plan:

1. Regression: killed render/compute/combined work with unchanged stamps but
   firmware done pointers reaching their exact published endpoints retires as
   an error, never success. Resume without queue progress still retains DMA;
   unreadable/invalid pointers and RTKit death still fail closed. Test the real
   queue runtime and reset functions, not a replacement implementation.
2. Permit error retirement on all participating GPU **done** pointers after a
   successful halt/resume callback. Keep success-stamp checks on normal work.
   Never write stamps or GPU pointers. This is the smallest source-supported
   way to avoid waiting for successful execution of work already marked killed.
3. Track scheduler aborted fences separately; engine reset reports the latest
   executed/aborted boundary, never the unexecuted queue tail. Preserve paging
   and render resubmission, preemption and the adapter-reset fence convention.
4. Preserve valid G13 fault VA/write/level in DMA_PAGE_FAULTED, tied to the
   same fence and context; missing/stale information remains unknown. No new
   MMIO reads and no diagnostic switch changes.
5. Targeted RED/GREEN, baseline runner, small commits and handoff report.

Critical limit: EXP1939's consumer also remained at 3 while write/expected was
4. Neither Asahi nor m1n1 proves memory release safe in that state. This change
cannot honestly promise that exact trace avoids 0x116. A resume-only shortcut,
forged stamp, or rebased GPU consumer would conceal unproven DMA ownership.

Smallest falsifiable checkpoint (integration owner only): same exact firmware
and workload, one hash-verified package. On a fault, faulting device removed,
no successful notification for its fence, aborted fence in reset, DWM/SSH alive,
and another app submission succeeds. Existing retirement receipts distinguish
unchanged stamps with consumed queues (new path) from unchanged consumers
(still unsafe). Collect receipts/dump before exact-package cleanup in the
established GPU-visible recovery; emergency GPU-hidden only per existing rules.
