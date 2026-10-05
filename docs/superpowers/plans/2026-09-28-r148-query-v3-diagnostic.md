# R148 QUERY v3 bounded provenance diagnostic

Scope approved by parent: extend the first QUERY failure only; no admission,
status, mapping, UMD, firmware, packaging, or hardware behavior change.

Evidence and inspected source/specification contract are recorded in
`investigation/analysis/R148-submit.md`. EXP861 QUERY at root0x9d1d50000,
generation623 and VA0x20000/65536 is separate from G4 generation440. The v2
leaf-absent result cannot tell absent ResidentPtes lookup from four invalid
entries, and omits the runtime allocation identity needed for correlation.

1. RED: add v3/168 decoder and real-body 16/64 profile assertions, preserving
   v1/16 and v2/144 cases. Assert immutable first capture, early unknown request,
   invalid copied handle, all-invalid group, absent shadow, partial group.
2. Append three u64 identities/size to the unchanged v2 prefix; add flags64
   (ResidentGroupAvailable) and128 (CanonicalAllocationAvailable). Keep only
   the existing adapter snapshot, with the acquisition reference and mutex
   held while reading resolved canonical identity/size after guards34–41.
3. GREEN targeted real-body replay and decoder. Parent owns ARM64 compile,
   independent review, full suite, commit, and CHANGES entry.

Ownership: UMD selects canonical handles/residency; VidMm schedules paging;
KMD owns guarded current provenance and first-failure capture; m1n1 owns native
UAT publication; Mu exposes the reserve. No ownership boundary changes.

Smallest future checkpoint: a single durable v3 first QUERY failure separates
available all-invalid resident provenance from absent provenance and identifies
its runtime handle/resolved allocation. Existing EXP861/R143 recovery contract
remains. No hardware is authorized by this plan. No claim about never-mapped
versus evicted history is possible from this snapshot.
