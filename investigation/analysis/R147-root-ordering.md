# R147 — paging selects the process root before COPY QUERY

## Authorized offline plan

Base EXP860 ledger 4a34b79e; no package build or Air access. Saved v2 receipt
selects bootstrap 0x9d8b28000, no SetRoot calls, missing native level1/index0
at VA0x20000 for64KiB. This proves selected-root identity, not absence of
mapping in the populated VidMm root or the scheduling reason for no callback.

WHY CONTINUE COMPARISON: EXP860 now resolves the missing root discriminator;
only current paging/root ownership and one real-body regression are needed.

Inspected: current gpuva_g3_{paging_windows,windows}.c, gpuva_g3_private.h,
shared apple_agx_gpuva_g3_graph.c; real G3 replay and R145 copy tests;
Asahi mmu.rs:486–525; m1n1 hv_agx_gpuva_v5.c checks and relocate_root;
Mu T810X MemoryInitPeiLib reserve/HOB and J313AppleAgxAbiAdmission.asl.inc.
Existing R143 reserve remains IPA=PA0x8e0000000/1GiB. No new live-machine
assumption, ADT/register/IRQ change, external code copying or firmware change.

VidMm owns allocation/residency and process page-table updates. UMD owns VA
mapping and paging-fence wait. KMD owns graph publication, root translation,
copy authorization and provenance. Broker owns process/table grants, inactive
root relocation and leased hardware translation slots; Mu owns reservation
and ACPI exposure. Runtime IRQ/DMA/power/recovery ownership is unchanged.
Asahi owns VM mappings directly; Windows supplies logical paging descriptors.
KMD translates those into process-owned native shadow tables and broker roots.

Implement successful WDDM root-level (level2, native0) update selection via
GraphBindRoot, under existing mutex and inactive-job/lease checks, excluding
NotifyEviction. This uses the validated target address and process handle,
not hAllocation or a guessed VA. Preserve private-tree attachment and the v2
receipt. SetRoot retains identical relocation semantics and context state.
No root binding or guard bypass is added inside Escape.

Steps: real no-fixture-bind replay RED; paging root fix; GREEN16/64 including
absent VA/unpublished/foreign process, relocation and stale generation;
affected tests, changed-TU ARM64 W4/WX/analyze, full suite exact baseline
comparison; review, implementation commit, CHANGES implemented row and ledger
commit. Preserve nested dirty trees and accepted EXP860 Code28 recovery.

Smallest later hardware checkpoint (proposed EXP861, not authorization): same
R143 firmware and exact separately built package, single variable paging-time
root selection. First COPY QUERY must cross53 and reach upload or a new precise
failure. Keep v2, collect ETL/dumps first; exact experiment cleanup via accepted
hiddenCode45 then ordinaryCode28. No receipt alone is not proof of success.

## Microsoft contract and choice

[UpdatePageTable](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_updatepagetable)
identifies the target by PageTableAddress and its address mode, the process by
the KMD handle returned from CreateProcess, and its level by PageTableLevel.
hAllocation instead identifies the allocation being mapped; it can be NULL for
page tables. The three-level advertised contract maps WDDM level2 to native0.
The existing resolver validates the full local reserve, and BrokerTable obtains
the process-owned shadow for that exact original table IPA. No table search by
contents, allocation size, queried VA or test-specific identity is used.

[SetRootPageTable](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_setrootpagetable)
notifies a context of its root and root movement/resizing, with that context
idle during update. It does not document that a private Escape must wait for
SetRoot, or guarantee SetRoot before CPU staging. EXP860 proves zero observed
callbacks at QUERY; no scheduled submission is a plausible explanation, not a
scheduling theorem established by the receipt or this documentation.
[GPU virtual address](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-virtual-address)
explicitly guarantees SetRoot before a graphics context is set for execution;
it also describes implicit page-table allocations without UMD/KMD allocation
handles, initialization by UpdatePageTable, and SetRoot notification on root
relocation. This establishes the execution boundary, not a private CPU-copy
Escape ordering. Existing context-root equality guards remain for execution.

[GetRootPageTableSize](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_getrootpagetablesize)
returns minimum size and is called only for two-level page tables. This driver
advertises three levels with a fixed eight-entry root, so GetRootPageTableSize
cannot discover its address. [GpuMmu](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpummu-model)
assigns table allocation/residency/update ownership to VidMm.
[Update flags](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_updatepagetableflags)
distinguish initial residency from impending eviction. Eviction of an old root
must not select it. [FlushTlb](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_flushtlb)
also names the process and root explicitly, but is not needed to infer a root
already identified by the root-level update itself.

Selected implementation: after successful publication and mirror, bind the
root-level target through existing GraphBindRoot, excluding NotifyEviction,
then attach existing private storage. The same mutex serializes paging, copy
and SetRoot. The broker refuses a foreign/unregistered root or occupied process
slot; KMD already refuses in-flight/leased paging. Bind failure poisons the
process rather than allowing copies through a stale root. Root movement advances
MappingGeneration exactly through the existing SetRoot mechanism, invalidating
old copy tickets. A later SetRoot to the same root is idempotent; another root
uses the same existing relocation behavior. No new Windows callback or UMD
ordering protocol is introduced. The receipt, all61 COPY guards, permissions,
allocation-offset provenance, leaf publication and whole-range checks remain.

## Brief dump assessment

Read the exception and module streams of both saved full user dumps. DWM thread
1420: read AV C0000005 at dwmcore.dll+0x11c020, address0x68793250. Explorer
thread8684: read AV at d2d1.dll+0x8388, address0x88f062c0. These do not establish
that COPY refusal caused the AVs: a returned invalid-parameter status does not
identify the writer of either bad pointer. Consequences of the copy failure
remain unproven; no separate crash investigation was pursued.

## Proposed EXP861 (single variable; no package or run authorized here)

WHY THIS HYPOTHESIS:
1. EXP860 first QUERY selects bootstrap with process/context SetRoot counts0/0;
   it cannot see the separately populated process root.
2. The old real paging body reproduces QUERY rejection without fixture root
   binding, and identical published mappings pass with paging-time selection.
3. Official UpdatePageTable fields identify the root and owner before the
   independently delivered context notification; no weaker guard is required.

Single variable: paging-time process root selection. Retain exact R143 firmware,
all caps, signer/recovery and v2 diagnostics. Separately build/hash/preregister
one package before any run. Expected checkpoint: first COPY QUERY passes its
real range/provenance checks and staging upload is observed, or the immutable
receipt identifies the next guard. A missing receipt or Code0 alone is
inconclusive; GPU submission/completion and DWM pixels are separate checkpoints.
Use EXP860 evidence-first exact-package hiddenCode45 cleanup and ordinaryCode28
recovery. Hashes, manifest and full commands must be filled in at actual package
preregistration; this proposal must never be used as an install/run manifest.

## Final offline verification

Real-body RED: both16/64 first no-SetRoot QUERY returns C000000D. GREEN uses
ASan/UBSan with real KMD paging/escape/SetRoot bodies and graph/wire/broker.
It exercises no-SetRoot QUERY/upload, invalid-root update, paging relocation,
stale copy ticket, old-root eviction, and both idempotent and relocated SetRoot.
Existing R145 foreign-process/handle, allocation provenance and partial-copy
checks continue; v2 absent VA, unpublished leaf/tail and immutable receipt cases
pass. The diagnostic bootstrap counterexample now explicitly parks its root;
it is not presented as the production no-SetRoot ordering test.

Broader replay27 and receipt2 tests pass. Full1157 tests/154.503s retains exactly
baseline15 failures/38 errors/2 skips, with no added/removed failure identities.
The full53 existing failure/error names are recorded in summary.json. No claim
that the entire suite is green. Pinned WDK26100/MSVC14.44 ARM64 paging TU passes
/W4 /WX /analyze, after536 persistent input hashes verified and only1 changed
file transferred. No SYS/DLL link, package, signer, firmware or Air operation.

Evidence and final source-input hash: `investigation/evidence/R147-root-ordering/`.
Independent review accepted the change and separately reran both profiles.
All39 current OPEN tandem dispositions are in R147-review.md; canonical review
hash is unchanged. Hardware validation and DWM execution remain unproven.
