# EXP503 source-address discriminator

WHY THIS HYPOTHESIS: EXP502 has three natural CDD source-address failures with
C000000D after CPU-visible residency acceptance. The production outer guard and
scanout argument/state guard both return that status. No receipt contains the
actual arguments or state, so neither guard is yet a proven cause.

WINDOWS CONTRACT: FULL GRAPHICS WDDM3.0 source-address DDI uses the allocation
handle, source, segment, address, flags and ContextCount from pinned WDK26100.
Microsoft documents ContextCount0 for modeset and nonpageable code/data because
the DDI may also execute at DIRQL for MMIO flips. The actual EXP502 caller is
ADAPTER_DISPLAY::SetVidPnSourceActive in the topology-application path. A null
allocation is documented as possible; that alone does not prove the observed
failure, and production behavior is not changed by this discriminator.

AGX/ASAHI CONTRACT: no hardware protocol or platform state changes. Retained DCP
and scanout broker ownership, fixed native mode, accepted Asahi-derived AGX
firmware/UAT runtime, m1n1 native477 and Mu406 contracts remain accepted from
EXP475/477/478/494–502. No new hardware access or external source code is added.

TRANSLATION: use one zero-initialized 88-byte receipt inside the existing
nonpaged ADMISSION_CONTEXT. An interlocked claim permits only the first DDI
caller to write it. Store scalar input/state only; never dereference allocation
or undefined Context entries for diagnostics. Publish after the unchanged
production result. A subsequent existing PASSIVE display wrapper claims and
persists the immutable observation to device and service registry, then never
rewrites it. Existing passive Stop/Remove callbacks also flush before context
release as a fallback. No worker, allocation, waiting, lock acquisition, pageable helper,
registry write, MMIO or production decision is added to the source-address path.
The scanout observation reads only existing nonpaged runtime and IrqEnabled.
Context allocation/removal already owns receipt lifetime; no new runtime owner.

WHAT IS STILL UNKNOWN: actual first argument/state and whether the outer guard
or inner queue guard returns C000000D in the current internal dxgkrnl sequence.
EXP502 repeated topology attempts make the existing next passive wrapper a
natural persistence opportunity. If none follows, the receipt remains in memory
and absence on disk is inconclusive, never success. No paging/TA3D/fence/present
proof is implied by these diagnostics.

Sources inspected: production display.c, scanout_windows.c, receipts.c,
render_admission.h, lifecycle.c allocation and existing passive wrapper code;
EXP502 exact ledger/result/Event494/caller evidence; builder Windows Kits10
Include/10.0.26100.0/shared/d3dkmddi.h lines6363–6395; official Microsoft
DXGKDDI_SETVIDPNSOURCEADDRESS and DXGKARG_SETVIDPNSOURCEADDRESS documentation.
The prior blocked DXGKCB_LOGETWEVENT URL retrieval remains BLOCKED_BY_PLATFORM;
it was not retried and supplies no driver evidence.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? This is receipt-only observation
of unknown Windows ordering, not a deterministic bug fix. No artificial RED is
added. Existing render-admission/WDDM suites, nonpaged/lifetime review, pinned
native analysis, Universal validation and exact package/version/sign/hash gates
verify the candidate. Later causal fixes require real deterministic regression
tests where the observed defect is reproducible offline.

Smallest hardware checkpoint: one natural bind of exact30.0.503.0 under unchanged
full-owner artifacts; collect Wom1SourceAddressReceipt and bounded boot ETL plus
durable host log and existing health collector. Failure: absent/malformed receipt,
different earlier boundary, crash/reset/hang or production outcome changed.
Evidence precedes exact package cleanup and ordinary GPU-visible377/392 restore.
The same long-lived agent immediately proceeds to the evidenced next cause.
