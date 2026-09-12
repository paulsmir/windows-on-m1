# Offline GPUVA semantic model — scoped PASS, migration conditional

Model source: tests/models/gpuva_semantic_model.{h,c}; executable scenarios in
gpuva_semantic_model_test.c; host runner tests/test_gpuva_semantic_model.py.
No production source/caps/DDIs, firmware, hardware, builder or agents changed.

## Executed evidence

Final run: investigation/evidence/AD04-gpuva-semantic-model/run-004/.
238 behavior checks; model plus unchanged shared UAT/regression/ledger suites:
6 unittest entries PASS, clang -Wall/-Wextra/-Werror and ASan/UBSan.
Manifest preserves compiler, command, input SHA256 and assumptions.
Earlier run001–003 evidence is retained. No Windows/AGX hardware was executed.

RED stages: missing model; actual byte-offset lookup mismatch (shared ResolvePage
requires page alignment; fixed only the model adapter); missing partial-update
composition; previously missing exact backing ownership check; missing distinct
paging bootstrap dependency. These are model construction defects, not GPU regressions.

The fixture uses actual AppleAgxUatCreateAddressSpace/Map/Unmap/ResolvePage and
their existing descriptor encoder, not a second table implementation. Root
relocation copies the real model table entries to distinct synthetic backing and
walks them through the shared code. Retired fixture storage is conservatively
retained; unbounded reclamation/progress is not proved.

## Scope and results

| Invariant | Observed within model |
|---|---|
| P/Q same numeric VA, different backing | Shared table walks give distinct expected addresses, including byte offsets |
| Independent process/root identity | Different root addresses/generations; moving P leaves Q unchanged |
| Root relocation | Same visible VA/translation; actual pre-relocation lease rejected |
| Slot reuse | Denied while render pending; completion alone insufficient; explicit invalidation needed |
| Generation safety | Wrong process/owner/root/map/slot and recycled allocation/process tokens rejected |
| Binding vs residency | Complete mapping cannot submit without external residency grant |
| Paging vs render fence | Typed domains reject interchange even with equal numeric sequence; duplicate completion rejected |
| Eviction/remap | Reservation survives; backing becomes nonresident/unmapped; replacement changes PA, not VA |
| Private isolation | User high/private VA denied; actual process upper-root walk is unmapped; firmware/table/foreign data backing rejected |
| Bootstrap | System paging root initializes directly through shared CPU table functions, without slot/render work; user bootstrap requires it |
| Coherent4K to16K | Four aligned contiguous equally protected valid4K entries produce one16K leaf; all-invalid group is explicit UNMAP |
| Partial updates | Reconstruct whole group; compatible update succeeds; unrepresentable update rejects without modifying prior state |

System paging process plus two users, one16KiB reservation per process and one
abstract reusable execution slot are modeled. CPU table storage/physical addresses
are synthetic. Exact three backing frames per modeled process constitute the
trusted input registration set; this is not a license to map arbitrary RAM.
Residency grants, publication/TLB acknowledgements and render completion are
explicit model events, NOT implementations of VidMm/broker/hardware mechanisms.
The PTE input is a normalized validity/write/address subset, not a claim that the
complete DXGK_PTE wire layout/protection/cache semantics have been implemented.

## Translation counterexample — do not hide behind green tests

Within one16KiB VA leaf, request:

    offset0x0000 -> backing0x20000000
    offset0x1000 -> backing0x21001000
    offset0x2000 -> backing0x20002000
    offset0x3000 -> backing0x20003000

One native16KiB leaf has one aligned physical base. The first mapping forces
base0x20000000; a shared UAT walk at offset0x1000 then returns0x20001000,
NOT0x21001000. No alternative base satisfies both. Both candidate backing
frames are in the same synthetic owner registration set, so this is not merely
an ownership rejection. Coarsening cannot fix it without changing semantics.

Similarly all14 mixed validity masks and14 mixed writable masks inside the
leaf cannot be represented by its one validity/protection value. Tests require
explicit rejection, no extra mapping or paging-success notification. Partial
updates are accepted only when the final group remains representable.

Thus arbitrary independent4KiB mappings ->16KiB leaves is mathematically FAIL.
The test harness is PASS because it detects this rather than overmapping,
silently changing permissions, selecting the first PFN, or inventing copies.

This is NOT yet proof that every legitimate WDDM configuration must request the
counterexample. Microsoft UpdatePageTable explicitly discusses16KiB GPU pages.
Exact admissible updates depend on the still-undecided table/segment contract.
The model neither assumes they are impossible nor assumes VidMm will avoid them.
Primary reference:
https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_updatepagetable

## First unresolved invariant / migration gate

Prove from pinned WDK/Microsoft semantics that the chosen GPU-MMU table geometry,
allocation placement and system-memory/eviction paths guarantee representable
16KiB groups, or provide a documented exact alternative. Rejecting a legal VidMm
mapping is safe failure, NOT a conformant implementation.

MIGRATION_VERDICT=CONDITIONAL. Do not enable production GpuMmu or implement DDIs
based only on the238 passing checks. If mandatory legal updates include the
counterexample and no exact representation exists, verdict becomes NO and the
development path returns to physical/patch-list relocation. No bounce-buffer,
global-root partition or private resident boolean is accepted as a substitute.

Minimal future production slice is DESIGN ONLY in
docs/superpowers/specs/2026-09-12-gpuva-minimal-migration-slice.md, gated on this
input-domain proof. Root-table placement, real invalidation and firmware context
selection still require their own integration evidence after that gate.
