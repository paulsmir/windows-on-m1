# EXP1933 — contain omitted native leaves in the referenced allocation set

This behavior candidate supersedes the receipt-only next checkpoint in
EXP1933-fault-mapping-last.md. Keep that snapshot fix (83c2fe2b); the next run
must exercise admission behavior. Offline only; no hardware or package built.

## WHY THIS HYPOTHESIS

EXP1933's captured Explorer fault has four valid logical PTEs on scattered
system pages and NativePresent=0. The paging path acknowledges such logical
updates after retiring their native leaf. G4 validates declared envelope and
render addresses, but cannot infer every shader/descriptor dereference. Mesa
already provides the complete batch BO residency set; an allocation-level
native check of that set closes this coverage gap without T12's whole-process
omission veto. The later PID9624 fault has no retained mapping and is not
silently assigned Explorer's allocation identity.

## WINDOWS/AGX CONTRACT and source evidence

- `gpuva_g3_paging_windows.c:684-805`: four aligned contiguous 4-KiB pages
  produce one native 16-KiB leaf; incomplete/scattered groups retain logical
  pages for CPU operations while native publication is absent.
- Asahi `drivers/gpu/drm/asahi/pgtable.rs:39` defines 16-KiB UAT pages; WDK
  26100 `d3dkmddi.h:4693` supplies allocation/offset/process provenance in
  UpdatePageTable. No alternative PFNs or paging failure status are invented.
- Mesa Asahi `agx_state.c:2712` tracks descriptor textures with batch_reads;
  `agx_win32_gpuva_batch.c:937-968` and its render twin collect references,
  including descriptor resources, shader BOs, pool slabs and sparse backings.
- `agx_win32_gpuva.c:192-227` takes residency and waits its paging fence before
  invoking UMD Submit; `umd_gpuva_windows.c:577-680` sets CopyHeld plus
  SubmissionHolds on the exact set. These holds exclude driver trim/free/map.
- `gpuva_g3_windows.c:1330`: the new 64-byte software escape authenticates
  process, live context, device and runtime allocation handle under the G3
  mutex. It inspects full native coverage and matches each resident 4-KiB
  logical entry's allocation/offset/IPA, including write permissions.
- `umd_gpuva_windows.c:1619,1736`: inspect only CopyHeld resources, after
  uploads can replace canonical storage, before SubmitCommandCb. A failure
  returns through the existing unsubmitted rollback, with no completion signal
  or GPU job. Unrelated mapped allocations are never inspected.
- `umd_win32_screen.c:1036-1041`: imported slots expose a logical extent
  shorter than the canonical allocation. The query accepts that extent within
  Description.Size, then checks the full description, never a safe prefix.
  Description.Size is not universally the rounded VidMm allocation size;
  `render_allocation.c:135` preserves it and `allocation_windows.c:436` rounds
  the separately reported physical extent. No access beyond the description
  is promised by this guard.
- `investigation/analysis/R147-root-ordering.md`: paging selects the process
  root before pre-submit escapes; context SetRoot is guaranteed for execution,
  not this query. Existing Submit/BeginJob root equality guards remain.

The DMA command BO is excluded only because the KMD reads it as a CPU envelope;
its existing logical validation stays intact. Sparse resource anchors, tables
and backing BOs are checked at their canonical mappings; the existing sparse
alias map contract remains responsible for their reserved VA mappings.

Ownership: Mesa supplies reference sets; UMD owns holds and submission admission;
VidMm owns residency and ordered paging; KMD owns mapping proof; m1n1 owns the
broker/stage-2 and Mu firmware exposure. Firmware gitlinks are unavailable in
this offline worktree and unchanged. Saved EXP1933 evidence supplies the launch
contract. Initialization, interrupts, DMA, power and recovery are unchanged.
No external implementation is copied. C75c placement and trim remain intact.

## ABI, observable behavior and cost

`APPLE_AGX_G3_NATIVE_QUERY` uses magic 0x4e565041, version1, exactly64 bytes.
Allocation is a runtime allocation handle, GpuVa its canonical base,
AllocationBytes the UMD logical extent on input/full KMD description on output,
Write the requested native permission. Ready=1 proves coverage; Ready=0 returns
Reason and FailedVa. A valid query returns STATUS_SUCCESS even when not ready:
it is a result for UMD admission, not a failed paging or scheduled-submit DDI.
Malformed or unauthenticated queries return a normal escape error and fail closed.
No new switch. New UMD requires the matching KMD; old KMD refusal is fail-closed.

`reject-native-allocation` logs nine UINTs: allocation handle, VA lo/hi,
returned extent lo/hi, write, reason, failed VA lo/hi. Reasons below0x100 are
the graph walk reason (native missing/unpublished); 0x100 is logical
ownership/offset/permission mismatch; 0x101 is native-vs-logical IPA mismatch.
An escape failure may have reason0 and failedVA0: it is not proof of omission.
There is no cached positive result. One64-byte round trip per held GPU BO and
O(sum referenced bytes /4KiB) metadata inspection per submission are deliberate
containment costs; quantify their impact before accepting this for performance.

## Verification and checkpoint

`test_g4_native_allocation_guard` compiles the actual KMD handler and real
native graph walk, plus the actual UMD submit body, with ASan/UBSan. It covers
interior/tail omission, unrelated omissions, native permissions, owner/offset/
IPA mismatches, authenticated handles, pre-SetRoot query, active-job read-only
query, imported logical extent, output layout, query failure, revalidation,
upload-before-query and refusal-before-submit/fence. Original submit fails the
new assertion (it reaches submission); the guard passes. The review's imported
size counterexample and pre-SetRoot case each also went RED -> GREEN.

For Claude: rebuild KMD and all UMD ABIs together; retain 83c2fe2b. Same full-owner
m1n1 0d3a75b3, Mu, normal GPU-visible recovery and D3D12 heap staging. Set
Wom1GpuFaultInfoRead=1. Retain Wom1PoolHalfSize=1 specifically as EXP1933's
pressure control (segment1344MiB); it is not part of the fix. Do not combine a
pool-size change with this guard's first checkpoint. Record package hashes.

First require Code0 plus usable DWM/Explorer with pinned SSH. Then one ARM64
GravityMark D3D12 10K windowed1280x720/90s, trace synchronization enabled. On a
bad referenced allocation expect reject-native-allocation, then batch failure
without a submit callback for that job, no GPU MMU fault and no0x116. It may
remove that application's device: containment is not transparent residency
repair or benchmark success. DWM failure at boot, mismatched query errors, a
remaining GPU fault, hang or TDR rejects the candidate. Unrelated omission
counters can still rise. Require actual correct rendering/completion to claim
workload stability. Preserve trace/PID, receipts, dump and full log before
Claude's exact-package dump-first GPU-visible rollback.

## WHAT IS STILL UNKNOWN

Native WDK/package compilation; hardware query/submit overhead; actual first
refused allocation in Explorer/GravityMark; whether all faulting accesses stay
within the reference set; hardware PTE/TLB coherence versus the acknowledged
CPU graph; sparse-alias correctness beyond canonical backing; why the old later
job failed reset quiescence; later PID9624's mapping and executable identity.
Referenced incompatible backing still needs a later correctly owned placement
or staging repair. This change does not make scattered pages representable,
repair an arbitrary shader address, or independently guarantee benchmark success.
