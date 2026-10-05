# B1 diagnostic KMD and firmware-slot gate — source audit, 2026-09-23

Status: **offline candidate compiled; no B1 hardware run**. R27 forbids further physical-mode DWM experiments. EXP762 ended before UMD at KMD memory start. This plan does not preregister a run or claim a B1 verdict.

## WINDOWS CONTRACT:

The B1 build stays at the known WDDM 3.0 physical admission model with GpuMmu capability bits zero. It uses a diagnostic KMD path, not fabricated VidMm process DDIs. KMD owns two explicit test roots, identified as an app root and paging root, and records their lifetime independent of normal WDDM resource handles. The normal WDDM scheduler cannot complete a fence before firmware completion and the v5 job retirement/TLB acknowledgement. DWM/client graphics are disabled for this test once the diagnostic checkpoint begins. Pinned WDK 26100 `d3dkmddi.h` remains the Windows ABI authority; EXP736/EXP761 are the prior Code0 full-owner physical admission controls, and EXP762 is the memory-start failure control.

## AGX/ASAHI CONTRACT:

Asahi `drivers/gpu/drm/asahi/mmu.rs` uses distinct process VM roots, slot binding and ASID invalidation; G13 user TTBR1 is zero. m1n1 `hv_agx_gpuva_v5.c`, `hv_agx_retained_platform.c` and `hv_agx_power_mmio.c` implement the v5 wire, ownership, lease, job refs, slot TTBR0/TTBR1 and TLB acknowledgement. Active legacy context 63 reserves that slot, so B1 chooses slot 1. Context 0 and retained TTBR1 prefix must compare byte-for-byte before/after every path. The retained physical baseline still allocates a 64-MiB local object and context63 UAT. The B1-only profile patches seven TA/3D slot fields to slot1 and initializes the backend submission identity for slot1; context63 remains reserved for the legacy owner and is not used by the B1 job. Existing TA/3D completion in EXP730/736 proves the old context63 path only.

## TRANSLATION:

1. Make a diagnostic-only KMD v5 MMIO client with sequence/epoch/receipt checks and no PA returned to UMD. Allocate zeroed 16-KiB-aligned root/table/backing pages with `AdmissionPhysicalAllocate`, retaining guest IPA and host PA translations. Register two roots, explicitly shared grants for the immutable GPU graph, and disjoint owned output grants. Use broker parent/leaf update operations only; a prepopulated root is rejected by m1n1 correction `fd4a3b7b` (RED then GREEN 7 real-C broker tests).
2. Map the same lower VA in each root to different owned 16-KiB pages. Read back via CPU and record page hashes/host PA inequality. In host tests, reject nonzero table creation, cross-owner backing, stale token, unretired job, failed TLB/rollback, slot63 use and context0 mutation. Do not interpret an MMIO echo as firmware execution.
3. `AppleAgxRenderSharedMemoryBindRelocationObjects` moves generated objects 0..35 into kernel half/context0. Only objects 36..74 are user-side; the validated planner enumerates 284 packed native leaves, 17 shader alias leaves and one output leaf per root. Broker v5 builds both roots with shared grants for the packed pages and a different owned output page at identical VA `0x15001d0000`. The B1-only KMD profile sets seven source-identified TA/3D work/microsequence slot fields and backend identity to slot1. It retains the lease through actual TA **and** 3D completion, then calls `JOB_END` and `RELEASE` before the second process leases slot1. An uncertain broker receipt or firmware job preserves all owned storage for recovery.
4. Capture context0 TTBR1 and retained prefix bytes at entry, after each root publication/job/retirement, and on failure cleanup. Require unchanged bytes, slot1 TTBR1=0, distinct slot1 TTBR0 values across the two leases, matching job completion/fence and TLB ack. Abort graphics at the first mismatch; collect evidence, uninstall exact package and boot GPU-visible Code28 recovery.

## WHAT IS STILL UNKNOWN:

- Whether the G13 firmware switches hardware to slot1 for the patched TA/3D work while retaining access to all kernel-half objects through context0. The 302-leaf lower graph and context split are established offline; actual firmware completion and output isolation are the hardware discriminator. Copying prepopulated context63 tables is forbidden.
- Whether m1n1's v5 guest-IPA-to-host-PA translation accepts each test-owned KMD allocation and whether the firmware can fetch every job object with user TTBR1=0. These are hardware questions after the offline graph is complete.
- Whether KMD's 64-MiB contiguous local allocation consistently succeeds on the current guest. EXP762 returned `STATUS_INSUFFICIENT_RESOURCES` during MemoryRuntimeStart; its internal substage was not retained. B1 must fail closed and record the substage if admission fails. No repeat physical-mode DWM experiment is justified by that uncertainty.

## Offline work-graph finding (2026-09-23)

The captured EXP208 template materializes to 6,094,848 bytes in the 8-MiB backend tail. Its work objects name context63 in seven exact fields: InitBM `16+0x4`, 3D `18+0xc`, TA `19+0xc`, 3D microsequence `15+0x4c/+0x284`, TA microsequence `17+0x34/+0x22c`. `BufferManagerInfo+0x48` and `Unknown Buffer+0x1f0` also contain integer 63 and are **not** context fields. The new `AppleAgxRenderTemplateSelectVmSlot` validates and patches only the seven Asahi/m1n1 schema-backed fields atomically; RED no-op test fails and host/ARM64 KMD builds pass. The helper is linked but not invoked by production.

The source-backed split is exact at object36: objects0..35 are rebound into `Initdata.RenderSharedMemory` kernel-half allocations and objects36..74 remain lower-VA GPU data. The B1 planner enumerates 284 packed lower leaves, 17 shader aliases and one per-process output leaf, not the whole 6.1-MiB template. m1n1 commit `e2aab060` permits an explicit shared grant for the lower graph with matching allocation generation; private duplicates remain rejected. Capacity1024 covers both 285-grant roots. The KMD orchestration host test exercises both roots through actual m1n1 C broker/wire, checks distinct output PTEs, sequential slot1 leases, busy release before job retirement, TLB acknowledgements and rollback on injected PTE failure. The B1 KMD profile compiles under pinned WDK26100 but has not run on hardware.

## Smallest falsifiable hardware checkpoint and recovery

One hash-pinned WDDM3.0/GpuMmu0 diagnostic package plus a new hash-pinned m1n1 v5 image; one run, no DWM stimulus. Install the package on the full-owner guest while `B1Armed` is absent: `StartDevice` returns before memory/interrupt/GPU access and records stage0. Verify exact package and the disarmed receipt, write `B1Armed=1` to the APPL0002 hardware key, perform an **orderly reboot**, then test during the fresh adapter start. This separates the EXP762 post-install memory-fragmentation question from the B1 firmware result. Preregister one experiment with `WHY THIS HYPOTHESIS`, the four sections above, exact root/m1n1/Mu commits and diff hashes, build/launch commands, package manifest and recovery artifacts. Check bounded SSH, USB L41/L43 and launcher before any physical request. Success requires two distinct owned backings at one VA, sequential slot1 leases, two real TA/3D firmware completions, TLB retirement acknowledgements and byte-identical context0/TTBR1. Failure includes any earlier KMD/firmware/ownership discrepancy. Collect ETW, broker/KMD receipts and raw logs before exact package cleanup; return to one inert Code28 APPL0002 with autologon still enabled.

## EXP768 cold-arrival and KD update — 2026-09-24

EXP763–767 ledger records no reason to omit the KD observer; no
`kd_wait_bugcheck.py` or `bcdedit /debug` command was preregistered in those
experiments. EXP768 preregisters debug-on serial vUART and a bounded
`tools/kd/kd_wait_bugcheck.py` log alongside the cold-arrival staged path from
EXP767. The script attaches by a brief break-in/version/continue handshake
before waiting, so its successful attachment and timing are recorded as an
observation. Collect KD, guest and launcher evidence before rollback. The
normal GPU-visible Code28 guest remains the cleanup destination; the
GPU-hidden image is used only for the documented staged-arrival exception and
exact package removal.

EXP768 outcome supersedes KD's presumed observational neutrality: the full-owner
boot stopped before DriverEntry/AddDevice receipts while serial debugging and
the attached observer were enabled. The cleanup package and KD environment
changed together, so this boot cannot attribute the stall to either one. For
the next B1 cleanup discriminator, restore the EXP767 debug-off/no-observer
environment and change only the cleanup package. Qualify serial KD separately
on the ordinary GPU-visible package-free guest before using it in another GPU
experiment. `kd_proclist.py` is not a playbook diagnostic: its fixed EPROCESS
offsets failed on EXP768. `kd_stack.py` also has fixed structure offsets, so
neither is a safe substitute without build-matched PDB-derived layouts.

## EXP768 cleanup contract and discriminator

Sources inspected: EXP767 `state.json` and ledger verdict; Asahi
`drivers/gpu/drm/asahi/{mmu,pgtable}.rs` (slot users, three 16-KiB table
levels); current m1n1 `hv_agx_gpuva_v5.c`, `hv_agx_retained_root.c` and
`hv_agx_retained_platform.c`; Mu J313 `J313AppleAgxAbiAdmission.asl.inc`
(one APPL0002, synthetic interrupt, four resource ranges); KMD
`gpuva_b1_windows.c`, shared B1 roots and memory owners; pinned WDK26100
`d3dkmddi.h` SHA256 `c13cecb0ce73e7bbdb6bec8586d05eea31932a8c532bec49b3dae4a03054770e`
and Microsoft GpuMmu/DDI documentation. EXP767 proves two user-root jobs and
lease/job/TLB retirement, but its `CleanupStatus1` does not name the cleanup
owner. Asahi's `VmBind` reference drop differs from the explicit m1n1 v5
JOB_END/RELEASE order; Mu exposes only resources; Windows B1 is still a
GpuMmu0 diagnostic path and does not hand these test roots to VidMm.

At Stage7 `LeaseToken=0` and `JobInFlight=FALSE` follow the two successful
release receipts. No successful broker call sets `state->Uncertain`; both
roots were created without `root->Uncertain`. Source therefore makes root1
(the second job's owner) the first remaining teardown owner, followed by
root0 and 12 test pages; the exact failing operation is not deducible from
EXP767. No deterministic order/count defect was demonstrated by the host
normal-path test, so no guessed cleanup behavior change is made. Durable
steps 0–15 record precheck, each root's remaining mapped/grant/parent/table
counts and broker status, each page release and the first failure. A new
read-only retained-root wire query hashes every context0 table page in m1n1
(including inherited TTBR1 root), once before job0 and once after cleanup;
KMD records both hash and page count and fails closed on mismatch. The hash
excludes firmware data pages, which legitimately change during jobs.

Smallest checkpoint: exactly one EXP768 cold-arrival staged run with otherwise
EXP767 B1 semantics. Success is Stage7, two outputs/completions, all cleanup
steps successful, zero outstanding test pages, matching nonzero context0
hash/page count, and no new stop code. A root/page failure names the next
owner; a hash mismatch stops G3 hardware promotion. Collect registry/KD/host
logs first, disarm and shut down, remove the exact package in GPU-hidden
recovery, and restore ordinary GPU-visible Code28. No G3 caps are bundled.
