# EXP858 QUERY contract audit and first-failure receipt

Execution: inline, following the user's single-pass/no-hardware instruction.
Goal: compare every QUERY predicate with real producer/WDK and replay values,
then preserve the first predicate ID/NTSTATUS without changing acceptance.

Sources inspected: UMD umd_gpuva_windows.c and umd_win32_screen.c; KMD
callbacks.c, allocation_windows.c, gpuva_g3_windows.c, gpuva_g3_paging_windows.c;
shared graph/transport/ABI; g3_r145_copy_cases.c and replay shim; Microsoft
DXGKARG_ESCAPE, D3DDDICB_ESCAPE, D3DDDI_ESCAPEFLAGS, Acquire/GetHandleData,
DXGKARGCB_GETHANDLEDATA and UPDATEPAGETABLE docs; pinned WDK26100 declarations.
Existing Asahi/native-page, m1n1 grant and Mu reserved-memory contract inspected
in EXP858-next-boundary.md remains unchanged. Saved EXP858 is the machine-state
reference; no live measurement is authorized. EXP857 reached parser rejection;
EXP858 reaches first local-copy preflight then rollback.

No universally failing predicate was demonstrated. Runtime/KMD handle namespaces
are intentionally different and bridged by AcquireHandleData, whereas replay
constructs the bridge and resident provenance. Do not change that ownership.
UMD owns handles/staging; VidMm translates escape handles and publishes paging;
KMD owns predicate validation and diagnostic capture. Firmware, DMA execution,
interrupts, power and recovery are untouched.

- [x] Add RED checks to real KMD replay: realistic non-pointer D3DKMT handle,
  QUERY rejection records exact predicate/status; later rejection cannot replace
  first; success/no QUERY does not create receipt; references balanced/no stores.
- [x] Annotate existing guards with stable IDs, common exit after locks/references
  are released. Record first QUERY failure in adapter memory and one16-byte
  registry value Wom1G3CopyQueryFailure at PASSIVE_LEVEL, using existing receipt IO.
  No request ABI change, no permissive checks, no changed return status.
- [x] GREEN both16/64 profiles; regression mutation of reachable QUERY failure groups; exercise
  actual receipt writer with only registry operations shimmed. Compile changed
  ARM64 KMD translation units in persistent builder; no link/sign/package.
- [x] Full suite once; compare exact failure/error identities to saved unchanged
  baseline15F/38E/2S; inspect diff/review; commit and append implemented CSV row.

Smallest future checkpoint (not run here): receipt identifies failed predicate
on one first QUERY. Recovery remains immutable hidden exact cleanup after Code0
then ordinary Code28. No hardware is justified while the new ID remains unknown.


Execution record: one implementation change, based on5b5f291. RED observed the
missing receipt assertion; GREEN exercises real handler/writer16/64. Whole ARM64
TUs pass after correcting unsigned FIELD_OFFSET comparison. Full suite1154,
exact baseline15F/38E/2S, no new identities. Added review-requested test cases then
reran affected replay tests. The guard dictionary lists conditions which cannot
fail for a well-formed QUERY (rather than manufacturing impossible states).
Fresh code review: no production findings. Concurrent Windows primitive behavior
is not simulated by the sequential replay. Scope remains diagnostic; no semantic
root cause was established and no package/hardware operation was performed.
