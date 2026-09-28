# R148 pool residency implementation plan

> **For agentic workers:** Use the real-body regression and source-first contract below. The user has explicitly authorized offline execution, fixes, commits, and diagnostics in NEXT_TASK_R148.md; no further design approval is required.

**Goal:** Include all native batch pool BOs in the existing GPUVA residency and staging-copy transaction, without changing KMD admission.

**Architecture:** Mesa owns batch and pipeline pools separately from bo_list. The Windows batch adapter must enumerate both dynarrays into its canonical allocation reference set. Existing AgxWin32GpuvaSubmit owns MakeResident, paging-fence wait, upload, submission and retirement; it deduplicates through add_bo before entering that transaction.

**Spec:** `/Users/pavel/public_windows/.local/tandem/NEXT_TASK_R148.md`.
**Tech stack:** pinned Mesa C, Windows UMD callbacks, ARM64, Python host replay.

## Evidence and ownership

Inspected current winsys/agx_win32_gpuva_batch.c, agx_win32_gpuva.c,
agx_win32_asahi_bo.c; pinned Asahi agx_pipe.c::agx_flush_render,
agx_batch.c pool initialization/cleanup and asahi/lib/pool.h; Asahi Linux
mmu.rs::map_node; m1n1 hv_agx_gpuva_v5.c::relocate_root/lease; Mu T810X
MemoryInitPeiLib reserve/HOB and generated J313AppleAgxAbiAdmission.asl.inc;
KMD gpuva_g3_windows.c snapshot and QUERY; Microsoft Residency Overview,
MakeResidentCb, GPU Virtual Address and DestroyAllocation documentation.

All 18 saved EXP861 guest originals and 536 package source files are hash
verified in investigation/evidence/R148/inputs.json. No live machine query:
this task explicitly prohibits Air access. Last accepted hardware remains
EXP861 Code28 recovery, R143 reserve/firmware. First submit generation440 and
later QUERY generation623 remain independent observations.

UMD owns BO allocation, VA mapping, residency, staging copies and pool lifetime.
VidMm owns placement and page-table callbacks; KMD validates/publishes native
leaves and process ownership. m1n1 owns grants/root leases and GPU execution;
Mu owns RAM reservation and ACPI. Interrupts, DMA, power and recovery do not
change. Linux VM bindings persist under its BO ownership; Windows requires
explicit MakeResident for all referenced canonical BOs. A reserved/mapped VA
alone does not prove current residency. No external implementation is copied.

## Constraints and review focus

- No Air, linked driver/package, firmware/caps/signer/recovery changes.
- No size/address/bind whitelist from the observed VA.
- Count both dynarrays with overflow checks before allocating refs.
- Enumerate all BOs, including pool rollover and low-VA pipeline BOs.
- Deduplicate aliases by canonical allocation using add_bo.
- Invalid identity must fail before submission; existing rollback stays intact.
- No inference that the later QUERY or memory growth shares this cause.

## Steps

- [x] Add real AgxWin32AsahiBatchFinish regression exercising two pools, rollover,
  overlap with bo_list, MakeResident -> wait -> submit -> retire, and failure.
- [x] Observe RED from missing pool references on the current body.
- [x] Extend checked reference capacity by both pool counts; enumerate each
  `util_dynarray_foreach(&batch->pool.bos, struct agx_bo *, bo)` and pipeline
  equivalent through `add_bo`; preserve fail path.
- [x] Run GREEN plus affected GPUVA/attachment/process-buffer replays.
- [x] Verify persistent builder source hashes; transfer changed files only;
  compile changed native ARM64 TU with strict warnings/static analysis.
- [x] Full host suite once; compare exact failure/error identities with the
  recorded 15F/38E/2S baseline. Independent final review, read tandem review,
  commit implementation, append CHANGES.csv implemented row with full commit.
- [x] Finish saved ETL lifetime and QUERY analysis. If causation remains unknown,
  use the smallest bounded receipt, separately tested and committed.

## Later falsifiable checkpoint (proposed EXP862 only)

WHY THIS HYPOTHESIS: the saved first submit rejects the next render reference
after VDM; current producer stores scissor in batch pool; current adapter omits
both pool lists from MakeResident and upload. Real-body RED/GREEN distinguishes
this omission from paging-policy guesses.

Single semantic variable: complete batch-pool residency/upload membership.
Keep exact R143 firmware and existing first-failure receipts. A separately
hash-gated package/run must observe crossing the original submit reference or
capture a precise new refusal; no receipt alone is not success. Collect ETL,
receipts and lifetimes before exact package cleanup; accepted emergency hidden
then ordinary Code28 recovery remains available. This plan is not a run manifest.
