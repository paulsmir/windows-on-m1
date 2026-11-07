# RC24 shadow contract and implementation record

Offline work on codex/rc24, base 6514ebca. Spec:
`/Volumes/pwdev/public_windows/.local/handoff/codex/rc24/TASK.md`.

## Plan

1. Inspect current allocation, paging, native publication, pool ownership and
   teardown; compare Asahi GEM/UAT, pinned WDK, and the explicitly permitted
   main m1n1 checkout. Record the cache and admission contracts before edits.
2. Reproduce the confirmed containing-runtime teardown defect using the actual
   Windows stop/free functions and actual slab allocator. Fix retention for
   live private leases and failed window revocation. Verify normal teardown,
   pending workers, missing broker, and failure paths under ASan/UBSan.
3. Check all six shadow gates, including ordered CPU paging, physical copies,
   allocation aliases and page-in capacity. Enable only if those owners and
   the actual broker contract are established; no fabricated retry status,
   permanently locked duplicate, or relaxed native-coverage check.
4. Run real paging/lifecycle regressions, KMD header checks and the prescribed
   full baseline. Obtain an independent review; commit small changes without
   attribution and write the handoff report with exact evidence and limitations.

Global constraints: offline only; no guest, hardware, package or other worktree;
no changes to EXPERIMENTS.md, CHANGES.csv, known_failures.txt or protected
invariants. Implementation and regression tests precede commits. Host tests
cannot certify WDK compilation, cache coherence or hardware performance.

## Current findings

The supplied fallback m1n1 checkout is c6d10e04afdad5314e8ac1e67bc3919b094ab000.
It contains retained-root firmware code but no v5 GPUVA broker or pool-window
implementation. The pinned 21c3648d32423def46086e258e0995c737dc9284 object is
absent there. This is a concrete source-version mismatch, not evidence that
private-page authorization is impossible. An offline matching snapshot was
requested while independent KMD analysis continued.

The cache decision must preserve one CPU cache type for each physical page.
Existing private slots come from the same WC MDLs as normal pool slots.
Changing this shared allocator to MmCached would also change the proven pool
contract, whose window is mapped PAGE_WRITECOMBINE. A future private WC path
must map through its owning MDL with the matching type, never through the
generic segment-0 MmCached mapper. UAT shared attribute 2 must be distinguished
from firmware-private cached attribute 0. Further evidence and gate verdicts
will be recorded after the lifecycle and ordering audit.

The original stop path called SlabDrain, which refuses outstanding private
leases, but still freed their containing runtime. It also discarded the runtime
on failed UNMAP_ALL, although the slab pages remained live. The implemented fix
retains the complete owner, with no claim that this alone implements shadow
teardown.

## Final scope decision

**The requested end-to-end shadow bridge is not delivered.** The implemented
change is a conservative pool-owner lifetime correction. Wom1SysmemShadow=1
still refuses eligible creates. Do not run this revision as a shadow-backed
half-pool performance candidate. This is an explicit incomplete-task result
under TASK.md's instruction to identify what cannot be done correctly; it is
not a claim that the requested architecture is impossible.

This pass went beyond the initial cache gate: it inspected the permitted
hypervisor source, the alternative WDDM backing interfaces, all CPU copy paths,
allocation aliases, process destruction and private-pool teardown. The missing
matching broker source is an external verification blocker. The admission,
authority and eviction state machine is also unresolved; obtaining that source
alone would not make the remaining implementation complete.

## Cache and authorization evidence

Coordinates below use D = drivers/apple-agx/render-admission,
S = drivers/apple-agx/shared,
A = .local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi,
W = .local/reference/wdk26100/d3dkmddi.h,
H = /Volumes/pwdev/public_windows/m1n1_windows.

| Source | Observed contract / decision |
| --- | --- |
| D/src/pool_windows.c:279, :888, :986 | Slabs and normal pool CPU views are WC. Private slots share those slabs. Do not change the common allocator to cached and leave WC aliases. |
| D/src/gpuva_g3_paging_windows.c:1230 | Generic system physical endpoints are mapped MmCached. Raw private WC PFNs must not be sent through this path as ordinary segment-0 endpoints. |
| A/gem.rs:160, :165, :188, :195 | GEM kernel objects use cached CPU mappings; user objects choose WC unless WRITEBACK is requested. A CPU cache choice is separate from GPU protection. |
| A/pgtable.rs:81, :155, :163, :232 | UAT attribute 0 is cached, 2 uncached/shared; GPU shared protections use 2. Cached noncoherent classification concerns firmware-accessible cached mappings. |
| A/mmu.rs:643, :777, :795 | Firmware cached mappings require remap/flush before unmap. Merely selecting attribute 0 and adding a CPU barrier is not a coherence proof. |
| A/mmu.rs:1200, :1221; A/pgtable.rs:39 | Asahi maps owned DMA SG ranges with 16-KiB alignment. Windows scattered 4-KiB pages do not satisfy that contract by being adjacent in a CPU VA. |
| H/proxyclient/m1n1/hw/uat.py:20 | The source calls attribute 2 Shared, outer writeback/inner NC; attribute 0 is for GFX-ASC cached access. Do not equate Windows MmCached with UAT attribute 0. |
| H/src/hv_vm.c:27, :29 | The inspected stage-2 default is MEMATTR_UNCHANGED. It does not repair conflicting guest CPU cache aliases. This source is older than the current pool-window implementation. |
| H/src/dart.c:53, :519, :535 | DART page addressing uses a 14-bit shift and one physical page base. These fields are not a mechanism for gathering four arbitrary 4-KiB originals into a native UAT page or repairing CPU cache aliases. |
| H/src/hv_agx_retained_platform.c:47; H/src/hv_agx_retained_backing.c:6 | The older retained-root route checks a complete aligned guest-RAM leaf, exclusions and translation. This is not the driver's current v5 backing-register protocol. |
| S/include/apple_agx_gpuva_broker_v5.h:9, :22; S/src/apple_agx_gpuva_g3_graph.c:505, :528 | Current driver uses v5 ABI version 6 and per-generation backing registration. The old retained-root source cannot establish authorization/revocation of these private MDLs. |

Selected future cache contract: retain WC for the existing raw private slots;
use a mapping tied to the owning MDL with the same CPU attribute, bounded to
that lease, through deferred copy completion. Keep GPU shared/coherent UAT
protection (attribute 2), not firmware-private cached protection. Preserve
ordering barriers before GPU publication and after ordered copy-out. A separate
cached-only private allocator is an alternative, but must never share WC slabs
or their window aliases. Neither alternative is implemented or hardware-proven
here. DART is a separate translation domain; its presence does not prove this
broker uses it for raw guest pages. No external implementation was copied.

## All six gates and alternatives examined

1. **Admission remains incomplete.** D/src/allocation_windows.c:453 refuses
   the request; :536 reserves native pool backing at fallible CreateAllocation.
   D/src/render_pool_slab.c:24 retains min(Reserved, ReserveCap) plus private
   slots. Preallocating one shadow for every live BO adds locked RAM for its
   entire lifetime. Releasing it on eviction and reacquiring it in UpdatePageTable
   moves allocation failure to a non-retryable placement boundary; the current
   status contract at gpuva_g3_paging_windows.c:1381 rejects that approach.
   INSUFFICIENT_DMA_BUFFER is record-capacity continuation, not a RAM retry.
   An internal cap alone does not constrain the set of simultaneously resident
   aperture BOs VidMm may ask the driver to map. A supported capacity/residency
   contract must precede admission, rather than substituting device removal
   when a hidden pool fills.

   WDDM 3.0 does have backing interfaces: W:929/:3856 declares
   SetAllocationBackingStore/ShareBackingStoreWithKmd. The pointer is supplied
   to the KMD; the declaration does not authorize replacing the originals with
   a contiguous MDL, establish its page-hold lifetime, or provide a native
   residency OOM negotiation. No driver implementation of this callback exists.
   W:10013/:10107 declares physical memory objects and AllocateAdl with
   RequireContiguous. D/src/physical_memory_windows.c:96/:117 already uses a
   contiguous noncached object/ADL. That is a usable allocation mechanism,
   but allocation can fail and the API does not by itself charge a hidden
   shadow as VidMm placement or make it pageable. These are avenues for a
   complete design, not absent APIs or a demonstrated ready-made solution.

   W:4755 declares NotifyResidency; the existing operation matrix
   investigation/G3_PAGING_OPERATION_MATRIX.md:22 identifies its physical-
   addressing gate. The requested General BOs are VA-only. W:3857/:4867 puts
   NotifyEviction/NotifyAlloc under WDDM3_2; the normal build selects WDDM3_0
   (D/AppleAgxRenderAdmission.vcxproj:24). Changing the interface/caps is not
   a transparent fix and still needs allocation, ordering and lifetime proof.

2. **Cache policy is selected above; production broker/cache integration is
   incomplete.** Raw leases exist, but no production caller acquires them or
   maps their MDL for shadow copies. The matching v5 broker source/launch
   contract is absent. No authorization check was removed.

3. **Ordered content authority remains incomplete.**
   D/src/gpuva_g3_paging_windows.c:1107/:1134/:1178 resolves and stores original
   endpoints while building records. :1278/:1283 maps those stored addresses
   during execution. D/src/paging_windows.c:340 executes each record before
   completing the fence. Building a copy-in at UpdatePageTable can read before
   a queued fill/transfer. Redirecting only native PTEs leaves virtual paging,
   physical paging (memory_runtime_windows.c:1061/:1081), and copy escapes
   (gpuva_g3_windows.c:1668/:1709) consuming different authorities.
   D/include/render_paging.h:35 has no shadow owner/generation/reference token.
   A replacement needs records holding the canonical generation and both
   backings, execution-ordered activation, copy-out before source release,
   and error cleanup for unsubmitted/cancelled records. None is claimed here.

4. **Shared generations remain incomplete.** OpenAllocation resolves the
   canonical object through AcquireHandleData at allocation_windows.c:919;
   it must not create one shadow per device-specific open. The native query
   authenticates canonical allocation, offset, rights and NativeIpa==GuestIpa
   at gpuva_g3_windows.c:1443/:1455. The graph's physical-frame grant generation
   (S/src/apple_agx_gpuva_g3_graph.c:187/:528) is not a canonical content
   generation. A future owner must cover aliases/placed-resource ranges,
   partial updates, write permissions and migration; unchanged neighbors
   cannot be refreshed from stale originals after GPU writes.

5. **Containing-runtime retention is fixed; full teardown remains incomplete.**
   D/src/pool_windows.c:639 retains the complete runtime when workers/private
   leases remain, before touching mappings. :654 frees it only after acknowledged
   window revocation and slab drain. On refusal/absent broker its slab list,
   callbacks and metadata remain allocated. Private grants are not covered by
   window UNMAP_ALL. This deliberately follows the existing terminal-leak
   policy; it is not recoverable quarantine or an implementation of retry,
   repeat Stop/Remove, reset, process-exit and parent-adapter lifetime.
   DestroyAllocation at allocation_windows.c:690 forgets/unreserves then frees;
   process teardown at gpuva_g3_windows.c:2396 first retires the graph. Neither
   currently owns the required canonical shadow copy/alias references.
   Thus every shadow teardown path cannot truthfully be called complete.

6. **Enablement remains refused.** The strict read-once request and
   CreateAllocation refusal from RC23 remain intact. Silently treating Shadow
   as ordinary Spill would recreate the known incomplete-native-coverage path.

VidMm owns original backing, placement and residency; the KMD owns private
storage, CPU mappings/copies, native publication and lifetime. UMD owns residency
and submission holds. m1n1 owns stage-2, broker authorization and revocation;
Mu owns boot reserves/ACPI. Initialization, IRQ, power and recovery ownership
are unchanged. No live ADT/registers or current generated ACPI were read, and
no new assisted/standalone launch contract is certified. The narrow runtime
fix changes no hardware or Windows DDI semantics.

## Verification and next checkpoint

The original real Windows stop/free replay failed in three scenarios: private
lease, broker refusal and absent broker. The fixed replay passes those plus
pending worker, drained worker, normal stop and null runtime. It links the real
slab allocator under ASan/UBSan. The private case is a future-integration
invariant: production has no private-lease caller. The failed-window path
exists in current production. Saved owner cleanup in the fixture is test-only;
production deliberately leaks uncertain owners rather than reclaiming them.

Targeted run: 36 tests pass, including original-address real paging
resolver/encoder/executor, allocation/open lifecycle, native coverage, UAT,
private ranges and KMD headers. No new shadow activation test or completed
activation implementation is claimed. Full prescribed baseline:

    ran 529 tests in 228 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
    BASELINE_OK

Independent review reran the stop/private/paging-identity tests (3 pass), found
no concrete regression in the narrow change, and confirmed the end-to-end task
gap. Logs and complete dispositions are in the RC24 handoff. No WDK build or
hardware validation was run.

Next causal prerequisite is the matching offline broker source plus a supported
bounded residency/admission design, not another flag trial. After the completed
bridge and pinned WDK build, the requested candidate is **HEAD plus that fix**:
Wom1GpuFaultInfoRead=1, Wom1SysmemShadow=1, Wom1PoolHalfSize=1. This revision alone
is expected to refuse the allocation, not improve performance.

Smallest future hardware checkpoint: one measured scattered GPU-only allocation,
queued fill, two aliases, GPU writes, ordered eviction/copy-out, exact readback
and remap, capacity refusal at a supported boundary, and acknowledged teardown.
Capture original/native identities, canonical generations, copy fences, grants,
and physical retained bytes. Only then run GravityMark D3D12 **2k then 10k**.
Expected signals: no reject-native-allocation, more available physical RAM,
far shorter paging waits, higher scene presents/min, correct content, DWM alive.
These are acceptance targets, not predictions supported by the lifetime fix.

WHY THIS HYPOTHESIS: saved RC22 accounting shows low physical headroom; UAT
requires contiguous native leaves; real paging consumes originals at execution
time. A reclaimable, ordered bridge addresses all three; an always-locked
duplicate or an early snapshot does not. Integration must record exact artifact
hashes and preserve known-good recovery, then collect evidence before ordinary
exact-package GPU-visible cleanup. No hardware run was preregistered here.
