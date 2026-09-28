# R146 — QUERY53 remains ambiguous; root selection leads

Task: main `.local/tandem/NEXT_TASK_R146.md`; base EXP859 ledger commit
`3e6744a21cdb5367f3ede01c13cc96fc6056f32a`. Verdict:
**STOP_CAUSE_NOT_UNIQUELY_DETERMINED**. No production fix, guard relaxation,
package, builder connection or Air access. This takes the task's explicit
stop-and-report branch. Evidence/reproducer: `investigation/evidence/R146-query53/`.

## Exact evidence and limits

`state.json` and UTF-16 `devnode.reg` independently contain the same 16 bytes:
`0100000010000000350000000d0000c0`, decoded as four little-endian ULONGs
`{version=1, bytes=16, predicate=53, status=0xC000000D}`. There are no VA, length,
allocation, process/context, root or generation fields. The first failure is
adapter-wide, not identified as DWM. Guards1–52 passed for this request, except
operation-specific conditions which are inapplicable to QUERY. Guards54–61
were never reached; local backing/provenance therefore is not established by
the receipt. `receipts.c:2008` confirms the entire encoding.

Saved `Wom1G3UnpublishedGroups` has segment0=139562 and segment2=0 (all other
counters zero). This weakens a general incomplete/misaligned local-leaf cause;
it does not prove the queried allocation's mapping exists. Invalid/unmapped
groups with no valid first subpage do not necessarily increment this counter.
The overwriteable `Wom1G3FlushInput` snapshot has branch5 (no active translation
slot), process0xffff8205fb45d010, root offset0x3e74c000, resolved original
root0x91e74c000 and graph root0x9d8b38000. Those two IPAs can differ because of
the original-to-shadow translation; this is **not** proof of a stale root.
Neither this last flush nor the global counters are joined to the first QUERY.

EXP858's 93 ETL copy escapes locate the first QUERY callsite; they do not encode
93 identical HRESULTs or guard IDs. EXP859 establishes only its first captured
guard53. Do not silently promote either observation into a universal failure.

## Owning paths and eliminated generic explanations

1. `gpuva_g3_windows.c:846`: CreateProcess creates a graph on BootstrapIpa.
   `gpuva_g3_paging_windows.c:884` dispatches UPDATE_PAGE_TABLE under the G3
   mutex. ResolveTable/BrokerTable select the process-owned shadow table;
   UpdateParent installs links on that table, without selecting it as root.
   `AdmissionG3UpdateLeaf:308–498` stores allocation/offset provenance in
   ResidentPtes for **both** page profiles, projects complete groups of four
   logical4K entries into native16K leaves and publishes segment2 through
   GraphTryLeafBacking(LocalBacking). System-only MappingAcquire is an extra
   system-frame lifetime operation, not the only route to leaf publication.
2. Publication and broker acknowledgement precede replacement of ResidentPtes,
   success return and mutex release. QUERY takes the same mutex. There is no
   deferred worker in this local publication path that could publish a leaf
   only after the paging fence. Missing/future Windows updates remain unknown.
3. `gpuva_g3_paging_windows.c:794–882`: FLUSH_TLB accepts an owned root without
   binding it: branch5 when no active slot; branch6 when another root is active.
   `gpuva_g3_windows.c:1052–1095`: SetRootPageTable resolves and binds the VidMm
   root and records it on the context. QUERY does not check that this callback
   has occurred; it walks Graph.RootIpa (`:619`).
4. `apple_agx_gpuva_g3_graph.c:518–539`: read QUERY walks root edge, middle edge,
   and nonzero native leaf backing. FALSE means **no write requirement**; no
   read-bit exists here. ReadOnly=0 cannot cause a missing read bit. Read-only
   complete local groups also pass the real replay. Mixed subpage permissions
   can prevent native publication, a different condition.
5. `umd_win32_screen.c:509–524`, `render_allocation.c:7–38` and
   `allocation_windows.c:385–406`: canonical description rounds byte width to
   16-byte pitch; physical allocation size rounds up to64K. QUERY uses the
   stored description size. `agx_win32_gpuva.c:22–60` maps the64K-rounded byte
   extent; `umd_gpuva_windows.c:64–100` retains that same successful VA as
   CanonicalGpuVa, used by transfer_slot QUERY at`:243–251`. Thus ordinary
   pitch/4K/16K/64K rounding does not produce a general over-query. Stale or
   wrong per-request identity cannot be excluded without the absent fields.

Microsoft documents VidMm ownership of process VA/page tables in the
[GpuMmu model](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpummu-model),
allocation-relative provenance and4K inputs in
[UpdatePageTable](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_updatepagetable),
and per-context root notification in
[SetRootPageTable](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_setrootpagetable).
The cited SetRoot documentation does not establish a callback-before-private-
QUERY ordering guarantee. The actual EXP859 ordering remains unmeasured.

Primary hardware references inspected: main `.local/reference/asahi-linux-asahi/`
`drivers/gpu/drm/asahi/mmu.rs:486–525` requires native-aligned mapping;
`m1n1_windows/src/hv_agx_gpuva_v5.c:7–57` retains slot/owner/root/epoch validation;
Mu T810X `MemoryInitPeiLib.c:477–490` reserves the RAM and emits its reserved HOB;
`J313AppleAgxAbiAdmission.asl.inc` reconstructs64-bit resources. Saved EXP859
reserve receipt identifies the unchanged R1431GiB IPA=PA0x8e0000000 contract.
No new live ADT/register/IRQ assumption is needed for this saved-evidence audit.
No external source is copied. VidMm owns residency and root notifications;
UMD owns requested VA and paging waits; KMD owns validation/copy authorization;
m1n1 owns native translation/grants; Mu owns reservation/ACPI. Interrupt, DMA,
power and recovery ownership are unchanged. EXP859 ordinary Code28 remains the
last accepted recovery; assisted/standalone launch contracts are not modified.

## The replay assumption and controlled counterexamples

R145 calls `sys_process` from `tests/g3_r145_copy_cases.c:24`.
`tests/g3_system_lifetime_cases.c:21–38` creates tables **and explicitly calls
GraphBindRoot**, bypassing the OS SetRoot notification. R145 then publishes an
entire64K allocation in one update (`g3_r145_copy_cases.c:39–51`). That fixture
cannot expose pre-root-selection QUERY or partial/missing range publication.

`replay.py` uses the existing real KMD bodies, graph, wire and m1n1 broker.
Only test setup changes: omit that artificial root bind, publish the same local
range with actual0x41 PTE flags, and deliver real DDIs. In both16/64 profiles:

- QUERY fails53 while the populated VidMm root is unselected.
- FLUSH_TLB succeeds with branch5/slot0; QUERY still fails53.
- Real SetRootPageTable with the identical published tables makes QUERY succeed.
- Complete read-only local groups also succeed.
- With the correct root selected, a different requested VA or unpublished
  allocation tail independently yields the identical53/C000000D receipt.

These are counterexamples proving diagnostic ambiguity, **not** RED→GREEN of
an EXP859 fix. Production code was never changed. Initial harness runs found
two fixture mistakes: registry flush-count reset was omitted, and the no-slot
flush was expected as branch6 instead of earlier branch5. Both were corrected
in the audit fixture; neither is a production defect. Final ASan/UBSan16/64
replays also complete the existing R145 lifetime/provenance cases.

## Ranked causes and proposed EXP860

1. **Bootstrap/parked root at pre-submit QUERY.** Strongest: exact KMD lifetime,
   R145's artificial root selection, successful paging flush without binding,
   and deterministic replay of the current boundary. Missing fact: root identity
   at the actual failed request. No semantic fix justified yet.
2. **Missing parent/leaf at the requested VA within a selected root.** The guard
   permits this identical result. No local unpublished-group counts makes a
   generic local representation defect less likely, but does not exclude absent
   ancestry, unmapping or incomplete publication of this range.
3. **Requested allocation/VA/range differs from its published mapping.** Generic
   rounding is consistent in current source; per-request stale identity or tail
   remains possible because the receipt includes none of those inputs.

Minimal extra field to test candidate1: one `RootIsBootstrap` bit sampled as
`p->Graph.RootIpa == p->BootstrapIpa` under the **same QUERY lock**. True selects
the bootstrap-root state; it alone does not prove an alternative root fully
covers the allocation. For a decisive bounded snapshot, retain query VA/length,
Graph.RootIpa/BootstrapIpa/context root, first missing VA and walk level,
process/mapping generations, plus the four ResidentPtes (flags/segment/IPA/
allocation/offset) when the leaf shadow exists. Explicitly mark absent ancestry;
do not substitute zero for a successfully observed PTE. This separates root,
ancestry, representation, tail and identity without changing admission.

**Proposed EXP860 single variable:** extend the first QUERY53 diagnostic snapshot,
same admission predicates/return codes/package features and exact R143 firmware.
No package or preregistration is performed in R146. Before any later run, replay
snapshot contents for each counterexample and compile affected TUs under pinned
WDK `/W4 /WX /analyze`. Snapshot under lock, persist after unlock/reference
release, bounded first claim. Checkpoint: first53 snapshot names root and missing
walk component; absent snapshot is inconclusive, never success. Evidence first,
then exact package cleanup under immutable hidden profile if Code0, then ordinary
Code28, per EXP859. A later package/run requires its own authorization and hashes.

WHY THIS HYPOTHESIS: root selection is required by the exact walker; the real-body
replay fails before SetRoot and succeeds after it; saved local unpublished counts
are zero. This supports a diagnostic discriminator, not a speculative root bind
inside Escape or bypass of the graph safety check.

## Verification

The decoder verifies all10 guest-original hashes, agreement of the two receipt
copies, and all535 packaged source hashes against the unchanged current source.
Full suite remains1154 tests,15 failures/38 errors/2 skips, with exactly the same
53 failure/error identities as the recorded baseline. ARM64 recompilation is
inapplicable: no production TU changed. No fix or hardware-validation claim.
Exact log/source/evidence hashes and commands are in `summary.json`.
Tandem OPEN-item dispositions are in `R146-review.md`.
