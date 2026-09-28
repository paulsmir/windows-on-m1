# R144 — full-local G3 paging bounds

Task: `/Users/pavel/public_windows/.local/tandem/NEXT_TASK_R144.md`.
Baseline root57c680e49fb1c8481a967c798c54af8f3c1c6086; source/diff snapshots
and all offline logs: main `.local/experiments/EXP857-r144-paging-bounds/`.
No Air access, launch, package installation, or recovery image writes.

## Source-first contract and implementation plan

Inspected in order: EXP856 attempt1 serial, full dump (SHA256
5e6d31e75b77c675f851e11e113736ee78d10dee398d58943dc12ddd8e753c88), saved
Wom1G3PagingFailure; current Asahi `drivers/gpu/drm/asahi/mmu.rs` user/kernel
VA ranges and `pgtable.rs` page allocator/native table geometry; current m1n1
`hv_agx_retained_platform.c:translate_guest`, `hv_agx_retained_backing.c`,
`hv_agx_gpuva_v5.c`; Mu `AgxLocalReserveValidation.h` and generated
`J313AppleAgxAbiAdmission.asl.inc`; KMD `memory_runtime_windows.c`,
`gpuva_g3_windows.c`, `gpuva_g3_paging_windows.c`, shared G3 graph/translation.
Microsoft primary specifications:
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_buildpagingbuffer_updatepagetable
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgkarg_buildpagingbuffer
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_buildpagingbuffer

Observed contract: EXP856 proves firmware identity reserve0x8e0000000+1GiB;
KMD reports1000MiB local, private16/backend8, DCP W0 remains56MiB.
Asahi allocates UAT tables independently of display; m1n1 validates each16KiB
leaf against RAM/launch exclusions with a40-bit PA bound, not56/64MiB;
Mu excludes1GiB from OS RAM. Windows supplies segment-relative offsets or CPU
virtual table addresses. KMD incorrectly uses ScanoutView for paging tables,
local leaf backing and virtual paging copies. DCP limits are irrelevant there.
No external implementation code is copied.

Ownership: firmware initializes/protects backing and Mu exposes ACPI; VidMm
owns placement; KMD owns address validation, CPU table updates and graph/grant
lifetime; m1n1 owns stage2/grants/UAT publication. IRQ, DMA ownership, power and
recovery contracts unchanged. Assisted reference EXP856 firmware reached KMD;
accepted recovery remains immutable EXP377/385 hidden then EXP377/392 ordinary.
No standalone contract or hardware success is inferred.

- [x] Decode exact dump UpdatePageTable/PTE plus receipt with package856 PDB.
- [x] Add real G3 VidMm replay of1GiB/1000MiB/56MiB, exact offset and neighboring
  high offsets, CPU/GPU modes, high child and leaf, last table, private/backend
  rejection, virtual fill/transfer and flush; observe RED before code changes.
- [x] Replace scanout view with local view in G3 table/leaf/paging consumers;
  retain display/output scanout gates and every alignment/span check.
- [x] GREEN both16/64 profiles; run G3/G4/reserve suites and full host suite,
  compare exact failure names with baseline; compile changed ARM64 sources.
- [x] Fresh final review, read tandem REVIEW, commit explicit implementation
  paths, then append CHANGES row with that40-character commit and bookkeeping.

Smallest later hardware checkpoint (not authorized here): rebuilt exact KMD
package with unchanged EXP856 m1n1/Mu passes the receipted high table update,
then Code0/Start12 and600s pinned SSH. Failure: same/new paging receipt or
bugcheck. Collect dump first; immutable hidden exact cleanup then ordinary
Code28. No arm/install/launch command is executed in R144.

Ruling: execute the already ordered repair inline without another plan approval;
retain final independent review. Fix the common G3 local-view ownership error,
not BuildPagingBuffer error-code masking; hiding rejected work as success or
retry would not implement the requested page-table update.

## Tandem dispositions

REVIEW R113: ACCEPT — armed package only to cold full-owner; no Air operation in R144.

REVIEW R111: DEFER — DC ZVA mapping is outside the confirmed table-address boundary.

REVIEW R110: DEFER — retain the recorded resolved/rejected status; no historical re-analysis.

REVIEW R109: REJECT — retained recorded hardware rejection; not reopened by EXP856.

REVIEW R108: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R107: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R106: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R105: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R104: REJECT — retained recorded hardware rejection; not reopened by EXP856.

REVIEW R103: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R102: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R100: REJECT — retained recorded hardware rejection; not reopened by EXP856.

REVIEW R99: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R98: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R97: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R96: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R95: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R94: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R91: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R90: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R88: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R86: DEFER — capacity8192 is a separate gate; the current rejection precedes broker access.

REVIEW R85: ACCEPT — preserve ports, profile/provenance and offline-only scope; no hardware series here.

REVIEW R74: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R71: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R69: ACCEPT — preserve ports, profile/provenance and offline-only scope; no hardware series here.

REVIEW R65: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R64: ACCEPT — preserve R143 firmware-owned reserve and full local segment contract.

REVIEW R63: ACCEPT — preserve R143 firmware-owned reserve and full local segment contract.

REVIEW R57: ACCEPT — preserve full-table span check; do not disguise failed paging as success or buffer retry.

REVIEW R55: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R54: ACCEPT — preserve ports, profile/provenance and offline-only scope; no hardware series here.

REVIEW R49: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R48: ACCEPT — preserve ports, profile/provenance and offline-only scope; no hardware series here.

REVIEW R47: ACCEPT — preserve ports, profile/provenance and offline-only scope; no hardware series here.

REVIEW R45: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R40: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R37: DEFER — prior phase concern outside EXP856 table-address rejection; existing verdict is unchanged.

REVIEW R64: ACCEPT — preserve R143 firmware-owned reserve and full local segment contract.


## Result

CDB loads package856 private symbols and decodes bugcheck Arg2 as the actual
DXGKARG_BUILDPAGINGBUFFER: UPDATE_PAGE_TABLE, level0, segment2,
offset0x3e7d4000, count16, StartIndex0, Repeat|InitialUpdate, single zero PTE,
FirstPteVirtualAddress0. Process TableShadows is NULL. Derived table IPA is
0x91e7d4000; ChildIpa/TableIpa/BrokerTableIpa in the failure receipt are zero
because the resolver rejects before assigning them. There is no child-address
fault in this event. Branch1 returns C0000141 because offset >56MiB-16KiB;
VidMm promotes that invalid DDI error to0x10E/B. Native table shadows may live
outside the local segment; their existing owned-root fallback is retained.

Real replay RED reproduces the same branch/status before edits. GREEN passes
both16/64 profiles with ASan/UBSan, including neighboring high addresses,
last local table, CPU/GPU update modes, high child/leaf, local payload end,
private/backend refusal, high-root flush resolution and CPU paging fill/copy.
Restoring each old resolver, outer table check, leaf updater or paging executor
independently makes the regression fail at its corresponding boundary.
The replay's local view models OS backing; ScanoutView runs its production body.
The existing production memory Start/Stop/view replay also passes.

Consumer audit: six G3 view uses switched to LocalView (process identity,
resolver, leaf backing, paging execution, owned-root flush fallback and table
CPU writes). Remaining uses in display.c, scanout_windows.c,
backend_platform_windows.c:AdmissionCompletedOutputPlatformValid and
G3OutputMatchesLocal/G4SubmitVirtualEnvelope are output/scanout admission;
retain56MiB. High-primary relocation remains an independent future gate.
No broker, shared graph, m1n1, Mu, caps, signer or UMD change is required here.

Verification:
- G3 VidMm26 PASS; G4 suite31 PASS; reserve3 PASS; production views1 PASS.
- Additional G3 contracts19:17 PASS,2 existing host compiler errors
  (`test_portable_cpu_view_validator`, `test_gpuva_system_context_object_contract`).
- Full baseline1146 -> final1147: identical15 failures/38 errors/2 skips;
  all53 exact names preserved in evidence JSON, no new failures or errors.
- ARM64 changed-TU compile:534 source hashes verified; pinned WDK26100,
  MSVC14.44.35207, /W4 /WX /analyze; both C files compile without diagnostics.
  First compile attempt lacked inherited WDK km INCLUDE; attempt2 supplies it.
  This is object compilation only, not a linked/signed package.
- Independent read-only reviewer: no actionable findings. Tandem review hash
  unchanged753bdfe638d6171954875a5fc9a58c09761999ffcab89b9d216f566c7e1300c5.
- Five immutable firmware/recovery artifact hashes and both pre-existing nested
  diffs verified unchanged. No Air access or hardware result.

For EXP857, rebuild KMD SYS/PDB and regenerate/sign its catalog/package with
final source provenance. Existing UMD may be retained only after normal package
provenance verification; this task does not produce package857. Reuse exact
EXP856 R143 m1n1/Mu (hashes below), not old64MiB firmware. No firmware rebuild:
m1n1 5fe13d19c5ca4d21327c44934613ae0f5109b8de096590db69f455e439e61a01;
Mu FD e54c009847e64a4b2b327f54385eedb94f5a9e5fd3b459fd6101b07af4c023fc.
Next hardware checkpoint needs separate authorization; no stable-Code0/DWM
claim follows from these offline results.
