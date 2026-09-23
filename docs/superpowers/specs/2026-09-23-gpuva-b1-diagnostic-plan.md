# B1 diagnostic KMD and firmware-slot gate — source audit, 2026-09-23

Status: **offline design in progress; no B1 package or hardware run**. R27 forbids further physical-mode DWM experiments. EXP762 ended before UMD at KMD memory start. This plan does not preregister a run or claim a B1 verdict.

## WINDOWS CONTRACT:

The B1 build stays at the known WDDM 3.0 physical admission model with GpuMmu capability bits zero. It uses a diagnostic KMD path, not fabricated VidMm process DDIs. KMD owns two explicit test allocations, identified as an app root and paging root, and records their lifetime independent of normal WDDM resource handles. The normal WDDM scheduler cannot complete a fence before firmware completion and the v5 job retirement/TLB acknowledgement. DWM/client graphics are disabled for this test once the diagnostic checkpoint begins. Pinned WDK 26100 `d3dkmddi.h` remains the Windows ABI authority; EXP736/EXP761 are the prior Code0 full-owner physical admission controls, and EXP762 is the memory-start failure control.

## AGX/ASAHI CONTRACT:

Asahi `drivers/gpu/drm/asahi/mmu.rs` uses distinct process VM roots, slot binding and ASID invalidation; G13 user TTBR1 is zero. m1n1 `hv_agx_gpuva_v5.c`, `hv_agx_retained_platform.c` and `hv_agx_power_mmio.c` implement the v5 wire, ownership, lease, job refs, slot TTBR0/TTBR1 and TLB acknowledgement. Active legacy context 63 reserves that slot, so B1 chooses slot 1. Context 0 and retained TTBR1 prefix must compare byte-for-byte before/after every path. Current KMD memory/start/render path instead builds a 64-MiB contiguous local object and complete UAT for context 63; `backend_platform_windows.c` hardcodes context 63 into runtime initialization, submission identity and compute work. Existing TA/3D completion in EXP730/736 proves the old context-63 path only.

## TRANSLATION:

1. Make a diagnostic-only KMD v5 MMIO client with sequence/epoch/receipt checks and no PA returned to UMD. Allocate zeroed 16-KiB-aligned root/table/backing pages with `AdmissionPhysicalAllocate`, retaining guest IPA and host PA translations. Register two roots and their disjoint backing grants. Use broker parent/leaf update operations only; a prepopulated root is rejected by m1n1 correction `fd4a3b7b` (RED then GREEN 7 real-C broker tests).
2. Map the same lower VA in each root to different owned 16-KiB pages. Read back via CPU and record page hashes/host PA inequality. In host tests, reject nonzero table creation, cross-owner backing, stale token, unretired job, failed TLB/rollback, slot63 use and context0 mutation. Do not interpret an MMIO echo as firmware execution.
3. Before a firmware submission, determine the exact set of native pages reached by the existing TA/3D work graph. Register and map those pages for the selected process in its own root. Set **every** work-item VM slot and backend submission identity to slot1, retain the returned lease token across the queue, and require actual TA and 3D completion receipts before `JOB_END` and `RELEASE`. The second process then leases slot1 after TLB ack and executes its own TA/3D work against its distinct VA backing. If a required firmware address falls outside the authorized graph, stop before submission.
4. Capture context0 TTBR1 and retained prefix bytes at entry, after each root publication/job/retirement, and on failure cleanup. Require unchanged bytes, slot1 TTBR1=0, distinct slot1 TTBR0 values across the two leases, matching job completion/fence and TLB ack. Abort graphics at the first mismatch; collect evidence, uninstall exact package and boot GPU-visible Code28 recovery.

## WHAT IS STILL UNKNOWN:

- The exact minimal TA/3D work graph reachable from the existing context-63 template. Copying its prepopulated page tables into a v5 root would bypass validation and is forbidden. The currently fixed 512 backing grants per broker may be too small if the entire 64-MiB local object is mapped; count the *reachable* pages offline before adjusting capacity.
- Whether m1n1's v5 guest-IPA-to-host-PA translation accepts each test-owned KMD allocation and whether the firmware can fetch every job object with user TTBR1=0. These are hardware questions after the offline graph is complete.
- Whether KMD's 64-MiB contiguous local allocation consistently succeeds on the current guest. EXP762 returned `STATUS_INSUFFICIENT_RESOURCES` during MemoryRuntimeStart; its internal substage was not retained. B1 must fail closed and record the substage if admission fails. No repeat physical-mode DWM experiment is justified by that uncertainty.

## Smallest falsifiable hardware checkpoint and recovery

One hash-pinned WDDM3.0/GpuMmu0 diagnostic package plus a new hash-pinned m1n1 v5 image; one run, no DWM stimulus. Preregister one experiment with `WHY THIS HYPOTHESIS`, the four sections above, exact root/m1n1/Mu commits and diff hashes, build/launch commands, package manifest and recovery artifacts. Check bounded SSH, USB L41/L43 and launcher before any physical request. Success requires two distinct owned backings at one VA, sequential slot1 leases, two real TA/3D firmware completions, TLB retirement acknowledgements and byte-identical context0/TTBR1. Failure includes any earlier KMD/firmware/ownership discrepancy. Collect ETW, broker/KMD receipts and raw logs before exact package cleanup; return to one inert Code28 APPL0002 with autologon still enabled.
