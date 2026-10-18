# R133 — EXP852 leaf publication correction

Spec: main-repository `.local/tandem/NEXT_TASK_R133.md`. Execute inline.
No Air, package, firmware/caps changes. EXPERIMENTS.md and GPU_CURRENT_STATE.md
are owned by the concurrent thread and are not edited here.

## Source-first contract and plan

Inspected EXP852 kernel dump with matching private KMD PDB on the builder;
receipt v3, saved launch contract and full.log; current G3 paging/graph/client;
Asahi mmu.rs map_node alignment/SG contract; m1n1 register_backing,
translate_guest and retained_backing_allowed; Mu MemoryInitPeiLib reservation;
pinned WDK26100 DXGK_PTE and official BuildPagingBuffer documentation.

VidMm owns residency and supplies 4-KiB PTEs. KMD owns logical shadow,
16-KiB aggregation, mapping/grant lifetime and DDI return status. m1n1 owns
protected-region exclusion, grant validation, UAT writes and TLB ordering.
Mu owns R64 reservation/ACPI; initialization, runtime interrupts, DMA execution,
power and ordinary EXP377/392 recovery remain with their existing owners.
Asahi needs aligned 16-KiB runs; Windows can also supply pinned CPU-only pages
inside a region excluded by the retained broker. An OS mapping is not a grant.

- [x] Reproduce the dump's GPU_PHYSICAL update (start 0x430/count 0x400,
  failing group 0x630, segment0 PFNs 0x851420..423), using the real translator,
  graph and broker with the saved protected firmware range. Observe RED.
- [x] Classify only an explicit pre-publication system grant refusal as
  unpublished; invalidate an old leaf before acknowledging a replacement.
  Keep logical residency, preserve other groups, and never suppress uncertain
  publication/revoke or invariant failures. Test retry and retirement.
- [x] Verify external BuildPagingBuffer status and preserve original failure
  diagnostics before rollback can overwrite them. Real faults remain fail-closed.
- [ ] Run the affected 33-test profile and full host suite once, record exact
  failures, review, commit explicit paths, then append CHANGES.csv with EXP852
  and status=implemented in a separate bookkeeping commit.

Smallest falsifiable checkpoint here: protected group remains logically mapped
but has no UAT leaf/grant; neighbours do publish, invalidation retires references,
and a genuine sync failure cannot complete paging or start a job. No hardware
checkpoint is authorized. Later hardware must retain the exact recovery pair
and use evidence-first cleanup; this change makes no DWM claim.

Ruling: LastStatus=0 is not proof of no broker refusal: UpdateLeaf rolls back
previous publications through calls that reset LastStatus. Use the saved
contract and real replay to discriminate, and retain the original refusal.

## Tandem dispositions

The OPEN historical dispositions below remain applicable to this narrower
publication correction; R57 now also drives external DDI status verification.

- REVIEW R113: ACCEPT — preserve direct cold full-owner transition; no launch here.
- REVIEW R111: ACCEPT — preserve the measured R64 CPU memory-type fix; no new uncached OS-RAM alias.
- REVIEW R110: ACCEPT — preserve the completed 64-bit dynamic ACPI range fix.
- REVIEW R109: REJECT — header already records the firmware hypothesis rejected; no sysmem implication.
- REVIEW R108: DEFER — historical firmware issues are outside the proven Submit boundary.
- REVIEW R107: DEFER — historical 0x101 attribution is outside this offline mapping design.
- REVIEW R106: ACCEPT — keep ordered transition and package gates for any later run.
- REVIEW R105: ACCEPT — use the recorded EXP836 matrix result; do not rerun it here.
- REVIEW R104: REJECT — already rejected by EXP821, not evidence for this mapping failure.
- REVIEW R103: DEFER — AllocateCb boundary is crossed; no live harness in this task.
- REVIEW R102: DEFER — AllocateCb failure is not the current boundary.
- REVIEW R100: REJECT — preserve its recorded rejection; no re-investigation.
- REVIEW R99: DEFER — old CreateDevice failure is superseded by the current boundary.
- REVIEW R98: ACCEPT — retain full-size output cases in future scatter-output tests.
- REVIEW R97: ACCEPT — one current-source pass, no EXP208 archaeology.
- REVIEW R96: ACCEPT — preserve the native TA/3D builder and change only its backing contract.
- REVIEW R95: ACCEPT — include ownership, generation, paging and failure-attribution gates.
- REVIEW R94: ACCEPT — keep per-process GPU objects distinct from firmware objects.
- REVIEW R91: DEFER — prior screen observation is unchanged; no presentation claim here.
- REVIEW R90: DEFER — panel/color observations are outside this address-translation boundary.
- REVIEW R88: ACCEPT — submission, completion and actual presentation remain separate checkpoints.
- REVIEW R86: ACCEPT — bounded direct grants first; versioned range grants before capacity claims.
- REVIEW R85: ACCEPT — future package must bind source, ABI, profile and artifact hashes.
- REVIEW R74: ACCEPT — read the context contract; retain aperture DMA placement.
- REVIEW R71: REJECT — the assertion that a cap guarantees all mappings contiguous is not established; retain the supported 64-KiB path as a separately tested candidate.
- REVIEW R69: ACCEPT — no live bind or Air access here.
- REVIEW R65: ACCEPT — preserve AGX4 v2, root/rights/lease/fence ordering; compute remains separate.
- REVIEW R64: ACCEPT — both OPEN entries: preserve the implemented R64 carveout and current G4 ABI; neither grants new hardware permission in this task.
- REVIEW R63: ACCEPT — keep firmware-owned reserve; no new contiguous allocation at StartDevice.
- REVIEW R57: ACCEPT — preserve full-span bounds and meaningful paging failure status in new tests.
- REVIEW R55: DEFER — repeat StartDevice is a separate recovery defect, not a mapping experiment.
- REVIEW R54: DEFER — no series is started; later hardware requires its own authorization and ledger.
- REVIEW R49: ACCEPT — preserve pinned WDK caps/table ABI; no opportunistic cap change.
- REVIEW R48: ACCEPT — no ports opened; future control-plane/launcher exclusion remains required.
- REVIEW R47: ACCEPT — preserve build-profile/ABI checks; no firmware build here.
- REVIEW R45: ACCEPT — test real broker dispatch and leaf/grant/table/root teardown order.
- REVIEW R40: DEFER — timer/watchdog attribution is outside the synchronous Parse rejection.
- REVIEW R37: DEFER — historical disarmed-start failure does not justify changing sysmem or launching Air.
- REVIEW R54: DEFER — No hardware series in this task.
- REVIEW R49: DEFER — Admission caps remain unchanged; Code0 already proven.
- REVIEW R48: DEFER — No ports opened.
- REVIEW R47: DEFER — No firmware or package build.
- REVIEW R45: ACCEPT — Reverse teardown is exercised with the real broker.
- REVIEW R40: DEFER — EL2 timing is outside this synchronous paging boundary.
- REVIEW R37: DEFER — No live install or bind.


## Measured cause and limits

CDB loaded package852's private KMD PDB without mismatch. The kernel bitmap
MEMORY.DMP is the 2026-09-27 13:40:49.659Z crash, uptime 42.945 s,
0x10E arguments B / fffff880ab7546b8 / ffffffffc0000483 / ffffa5098abdc000.
Stack: CompleteBuildPagingBufferIteration -> UpdatePageTable ->
MapScratchAreaVaRange -> MemoryTransferUsingGpuVaWorker -> TransferToSystem
-> EvictResource. This is paging-process eviction, not a DWM submission.

The saved UpdatePageTable has level0, GPU_PHYSICAL, table offset C000,
StartIndex=430, count=400, FirstPteVirtualAddress=2430000, hAllocation non-null,
hProcess=ffffa50989ffa960 (ProcessId1). The receipt's Index630 is the first
refused four-PTE group, not the call's StartIndex. In the input buffer at
ffffe784a3a63300, four PTEs have Flags1 and PFNs 851420,851421,851422,851423.
The preceding four PFNs are 8ef41c..8ef41f. These are fully aligned contiguous
16-KiB groups; neither missing neighbours nor scatter is the explanation.

Saved contract.bin contains firmware kind2 [8510b4000,852e3c000), enclosing
851420000..851423fff. full.log independently records guest image start and
length 1d88000. Current retained_backing_allowed rejects every overlapping
non-GUEST_RAM/non-LOW_MEMORY region; translate_guest returns zero and
register_backing returns OWNERSHIP before insertion. This exact refusal is
source/contract-backed and reproduced through the real broker; the original
broker response itself was not saved at the instant of refusal.

In the dump the failed group's old ResidentPtes are zero; no leaf/backing/frame
for that IPA remains in the paging graph after rollback. Created=1,
Uncertain=0, JobInFlight=0, LeaseToken=0. MappingGeneration=80ff is finite.
These observations exclude the obvious local graph precondition alternatives.
An allocation failure cannot be uniquely excluded from the post-return dump,
but the protected-region contract independently guarantees that this candidate
cannot be granted. It is incorrect to claim a uniquely observed R132 local
check solely from LastStatus=0.

The real replay preserves the captured start/count, PTE group, preceding group,
GPU_PHYSICAL mode, local table offset and saved firmware exclusion. Earlier
prefix PFNs are explicitly modelled allowed RAM, not falsely described as
captured. RED matches the entire first-failure tuple: C0000483, LeafGraph7,
Index630, GraphLastStatus0, Uncertain0. The first 128 groups publish; the first
protected group refuses registration; 128 compensating successful calls erase
the original LastStatus before receipt serialization. This falsifies the task's
parenthetical inference that LastStatus0 proves no failing broker call.

## Correction

A typed out-result from the exact grant call identifies only acknowledged
system OWNERSHIP/CAPACITY refusal before UAT mutation. The pager then removes
any previous native leaf, retains the new logical residency, counts the group
as unpublished, and continues. Local grant failures, stale identity, changed
translation of an already granted frame, malformed descriptors, allocation
failure and ambiguous stores/TLB/revocation still fail closed. No broker check,
protected range, firmware, ABI, caps or recovery policy was relaxed.

The first broker error now survives both graph grant cleanup and pager batch
rollback. A separate real-broker RED corrupted an existing descriptor: grant
succeeded, UPDATE_LEAF returned OWNERSHIP, successful REVOKE reset LastStatus
from4 to0. GREEN retains4 and the original descriptor; no grant/reference leaks.

## BuildPagingBuffer contract — unresolved fatal recovery

Microsoft lists SUCCESS, GRAPHICS_ALLOCATION_BUSY and
GRAPHICS_INSUFFICIENT_DMA_BUFFER; Busy requires actual GPU allocation use and
space failure requires actual DMA-buffer exhaustion:
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_buildpagingbuffer
The documented bugcheck subtype confirms invalid error return:
https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/bug-check-0x10e---video-memory-management-internal
The saved dxgmms2 CompleteBuildPagingBufferIteration+314 compares a negative
status with C01E0001; mismatch branches to the B bugcheck. No arbitrary fatal
NTSTATUS is a supported replacement for C0000483 at this boundary.

Ruling: do not misreport an irreversible graph failure as temporary busy or
DMA-space shortage, or return success for an unperformed update. The measured
EXP852 refusal now succeeds honestly as a logical-only mapping; the generic
fatal return/recovery requirement is NOT implemented. Existing genuine-fault
paths can still return an invalid DDI status. Correct general recovery needs a
real ordered paging-execution fault record/fence or context-recovery design,
including CPU_VIRTUAL bootstrap, ownership retention and no false completion.
An asynchronous scope question was sent before considering that expansion.

Independent review accepted the grant classification and required explicit
fatal-status limitations plus the internal first-error regression, now fixed.

## Verification and delivery

Affected profile: 34 unique tests PASS (the final invocation accidentally
repeated test_agx_retained_backing: 35 executions, all PASS). Command:
`CC=/tmp/agx-clang-wrapper python3 -m unittest tests.test_gpuva_g3_contract tests.test_g4_submit_virtual_replay tests.test_g3_vidmm_replay tests.test_gpuva_broker_v5_contract tests.test_agx_retained_backing tests.test_agx_retained_backing tests.test_gpuva_g3_paging_bootstrap tests.test_change_ledger`.
Both R132 and R133 replay use ASan/UBSan. Includes incomplete/scattered groups,
protected replacements, stale identity, capacity refusal/retry/retirement,
aliases, real BeginJob lifetime, translator, table conflicts and sync/TLB faults.

Full command: `CC=/tmp/agx-clang-wrapper PATH=/tmp/agx-cc:/opt/homebrew/bin:$PATH python3 -m unittest discover -s tests`.
1122 tests in 103.203 s: 15 failures, 41 errors, 2 skipped. All 56 error/failure
names occur in the R132 baseline list. Full suite is NOT green. Exact names:
main-repository `.local/experiments/R133-offline/full-suite-failures.md`;
full log, RED/GREEN logs and CDB transcripts are in that same directory.

Independent final review found no additional Critical/Important defect within
this correction. It explicitly did not approve the unresolved generic fatal
DDI contract. No source whitelist or hardware-specific address was added to
production code. No external implementation code was copied.

All source-file hashes for the driver/tests are in `source-manifest.json`,
SHA256 939ec8b95e8c5276cd007c71cdebebec0a897162caa8a8c056e3d289ef0ca9bc. Evidence/log index `verification.json`, SHA256 184b02a70f548dd95fa307b7ef603eb563a76707d9f0a2287875d3faccdd3be5.
Dump SHA256 dde45931f203446dc087f7f0d0891f0c38b5c3a35a1907d439ccfc0a9b885876;
matching PDB SHA256 6c2ef715fc269add2199aa8285d16b1cc8b5fac4dee95239dcedc7b479136039.
No Air access, package build, installation or hardware validation of this fix.
EXPERIMENTS.md and GPU_CURRENT_STATE.md were untouched; concurrent evidence
commit 81bfb7bf remains intact.
