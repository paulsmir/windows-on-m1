# EXP942 copy-transfer failure observation plan

Goal: retain the first rejected UPLOAD/DOWNLOAD predicate, exact range, process
and mapping generations before releasing the G3 lock and allocation reference.
This is diagnostic instrumentation, not a proposed graphics fix.

Execution: implement inline in the existing integration worktree. Continue the
user-authorized investigation without a new approval round or subagents.

WHY THIS HYPOTHESIS:
1. EXP941 Explorer5188 logged reject-copy-slot step3 on a 1MiB UPLOAD at
   GPUVA0x1db0000, before the later desktop-capture/DWM crash. The boolean UMD
   return loses the exact rejection. A refused copy aborts ResourceCopyRegion.
2. KMD captures detailed predicates only for QUERY. Its saved predicate57 cannot
   be attributed to that UPLOAD: QUERY and data transfer are separate calls and
   paging may change between them. Reusing this receipt as the cause is invalid.
3. Physical photo and same-surface DCP snapshots show nonzero corrupted content;
   successful private clear/copy and windowed Present do not validate composition.
   Distinguishing a paging-busy refusal from an invalid page/owner/range is closer
   to a measured failure than guessing DCP stride or changing another capability.

WINDOWS CONTRACT: FULL GRAPHICS. The existing software-entry buffered Escape
keeps its exact flags, owner authentication, status and output behavior. No
WDDM capability, request ABI, admission, fence or residency semantics change.
Registry publication remains PASSIVE_LEVEL, after mutex and allocation-handle
release. Reference: Microsoft DXGKDDI_ESCAPE / D3DDDI_ESCAPEFLAGS documentation
used by EXP940; pinned WDK26100 types remain unchanged.

AGX/ASAHI CONTRACT: existing logical PTE validation and local-RAM copy retain
their allocation ownership, paging-quiescence and active-job guards. No UAT,
DART, RTKit, queue, power, interrupt, cache or firmware mutation is added.

TRANSLATION: a separate immutable diagnostic record describes the failed
admitted data-transfer call. Include operation, predicate, NTSTATUS, PID,
allocation/context, GPUVA/offset/length, failed page (only for page predicates),
and request/current generations. Capture under the existing lock; publish once
after releasing it. Existing QUERY receipt remains independent.

WHAT IS STILL UNKNOWN: which existing guard rejects the first actual transfer
on the failing native desktop workload. A hardware run must answer that exact
question; it must not treat observation as a repair or erase an earlier failure.

Sources inspected: umd/src/umd_gpuva_windows.c transfer_slot/copy_escape;
src/gpuva_g3_windows.c AdmissionGpuvaG3CopyEscape and QUERY capture;
src/receipts.c first-cause publication; include/render_admission.h;
shared/include/apple_agx_g3_copy_abi.h; real KMD tests/g3_vidmm_replay.py and
tests/g3_r145_copy_cases.c. Existing graph/m1n1 owner mappings and primary
Asahi/m1n1 render/display implementations remain unchanged.

Live baseline after941 rollback: boot124616 Start10:19:27.2044595Z, normal377/392,
oneCode28/CPU8/packages0/services0/signerfalse/filesfalse/RegFlush0/cleanNTFS.
Initialization/power/DMA owners remain the last validated launch contract.

Review focus: no copied bytes on rejected ranges; first failure cannot be
overwritten; QUERY failures must not consume transfer record; publication must
occur after reference/mutex release; request and current generations are distinct.

- [x] Add a separate versioned transfer-failure record and adapter claim.
- [x] Capture only failed UPLOAD/DOWNLOAD calls at the existing locked rejection
      boundary; keep all predicates and return values unchanged.
- [x] Publish the immutable record outside the lock through the existing receipt
      mechanism; expose it under Wom1G3CopyTransferFailure.
- [x] Extend the real-copy replay shim to observe this record and verify an
      invalid-page upload still rejects without modifying memory, captures its
      exact range, and retains its first record after another rejection.
- [ ] Run software-copy and QUERY-receipt replays for16/64 profiles, then pinned
      WDK/native build/sign/hash gates. Commit implementation and CHANGES row.
- [ ] Preregister exact942 package/manifest and one short discriminator only
      after the above gates. Expected checkpoint is a truthful failure receipt,
      not a claim of fixed pixels. Ordinary GPU-visible rollback remains377/392.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? The hardware investigation
currently has no record for a rejected data transfer and could falsely blame
an unrelated QUERY. Replay checks the actual rejected-copy boundary, its exact
range and unchanged memory/status, not a manufactured driver-behavior failure.

Offline result: current16/64 copy replay plus historical QUERY decoder3tests PASS; old actual copy handler (0f8d237c body with current-contract harness) fails the new first-transfer-receipt assertion. Initial historical-harness mode failed an unrelated old-query expectation and is not counted as RED; corrected scoped replay reaches the intended assertion. Existing historic QUERY checks remain unchanged and pass.
