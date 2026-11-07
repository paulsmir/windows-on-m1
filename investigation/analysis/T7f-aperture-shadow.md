# T7f — conditional KMD shadow backing for scattered system pages

2026-10-23; branch `codex/t7f-aperture-shadow-design`. Offline design, not a
production shadow implementation or a GravityMark fix. No guest, hardware,
builder, package, firmware, or other-worktree operation is part of this task.

**Decision:** a shadow can preserve bytes only during an exclusive GPU ownership
epoch, with copy-in before publication and copy-out before returning CPU/system
ownership. The current driver has no complete contract for those epochs, no
private shadow-address allocator, and no supported way to turn a late
UpdatePageTable allocation failure into MakeResident E_OUTOFMEMORY. Consequently
production publication is gated. Implement only offline regression/design
validation here. Existing shape diagnostics already detect the backing failure;
another shape-only receipt would not close the missing ownership contract.

## Evidence and scope

Repository-relative paths below refer to this branch; `.local/reference` is the
provided primary-source mirror. Read the T7b, T7d and T7e handoff reports and the
T7e/T7d/R133/T7c/R146/R145/M2-f3/EXP853/EXP850/T7/R155/R149 source notes. Older
notes are contract references, not another historical cause search. The T7b
proposal to disable SystemDirect was rejected by the handoff's protected
invariants and is not adopted here.
Shortened driver paths below are under `drivers/apple-agx/`; a bare C filename
uses its already named source directory.

The supplied EXP1915 receipt decode in
`/Volumes/pwdev/public_windows/.local/handoff/codex/t7b-g4-v3-decode/REPORT.md`
records allocation bytes `0x08100000`, ClassId 1, Win32Flags `0xf`, CpuVisible 1,
MetadataValid 1, system segment 0, four valid writable logical pages, and
LastStatus 0. Its first four IPAs are `0x91791d000`, `0x866c1c000`, `0x97b11b000`,
`0x91591a000`: neither aligned nor adjacent as a native quartet. The supplied
EXP1915–EXP1918U2 observation is removal around 22 seconds under pool pressure
(2683/2688 MiB); EXP1917 rejected aperture exclusion. No new live state was read.
All 392 bytes' decoded fields were independently matched to the supplied raw
`t7b-g4-v3-decode/kmd-receipts-EXP1915.txt:2`; the handoff's `receipt-check.log`
records the raw payload hash and relevant values.

**Correction to the task's eligibility premise:** writable General is proven;
"not CPU-mapped" is not proven by this receipt. The complete tracker in
`drivers/apple-agx/render-admission/include/render_admission.h:517` contains
allocation metadata and paging history but no live lock/map count. Flags `0xf`
permit both CPU and GPU access; they do not record current access. A writable
attachment proves a requested writable range, not a completed GPU write. No
eligibility predicate may depend on the particular 129 MiB size or these IPAs.

Both submodules are uninitialized in this worktree: m1n1 gitlink
`21c3648d32423def46086e258e0995c737dc9284`, Mu gitlink
`361a2134b2f742f331f8d75ae5f60ad4f7f413f1`. Current implementation, generated ACPI,
protected host ranges, and assisted/standalone launch binaries cannot be
reverified here. Checked-in broker ABI plus current KMD callers establish the
interface below; old source notes do not substitute for current firmware review.
This is an explicit gate on a future window partition or broker change.

## WINDOWS CONTRACT

### Backing, paging and fences

- `.local/reference/wdk26100/d3dkmddi.h:4693` defines UpdatePageTable's input
  PTEs, allocation identity and byte offset; `:4951` defines MapAperture's
  allocation/MDL/page offset, and `:4963` defines UnmapAperture's allocation and
  range. Those are backing/mapping operations, not CPU access notifications.
  `drivers/apple-agx/render-admission/src/memory_runtime_windows.c:1008` reads
  MDL PFNs and `:1025` records them in the software aperture. It never replaces
  the MDL's pages. `:1048` unmaps that software aperture.
- `drivers/apple-agx/render-admission/src/allocation_windows.c:548` sets
  AccessedPhysically only for class 0; General uses GPUVA. Therefore a General
  segment-0 UpdatePageTable must be handled even without MapApertureSegment.
  Do not require a preceding MapAperture notification for shadow correctness.
- `.local/reference/wdk26100/d3dkmddi.h:5084` requires PASSIVE_LEVEL on entry to
  BuildPagingBuffer. That does not mean the GPU is idle or an earlier queued
  Fill/Transfer has executed. Current UpdatePageTable waits for native jobs
  with the G3 lock released (`gpuva_g3_paging_windows.c:1511`), then applies
  leaves under a fast mutex. The mutex raises IRQL; do not insert pageable MDL
  mapping, large copies or waits while holding it.
- `gpuva_g3_paging_windows.c:993` resolves virtual copies to logical IPAs at
  build time and `:1020` snapshots aperture addresses. `paging_windows.c:340`
  executes the records in a worker, then `:357` records paging completion.
  A shadow copy at BuildPagingBuffer time could precede a queued fill into its
  source. Merely making it a CPU copy at PASSIVE does not fix ordering.
- `.local/reference/wdk26100/d3dkmddi.h:4741` defines TransferVirtual's
  allocation offset, size, source/destination page tables and direction;
  `:4892` defines physical Transfer endpoints. Both must consult the current
  authoritative backing. Updating only native leaves leaves the existing CPU
  transfer executor reading stale system bytes.
- `.local/reference/wdk26100/d3dukmdt.h:1475` defines MakeResident's allocation
  list, accepted count, paging fence and trim requirement. `:1484` requires
  synchronization before GPU use. It has no KMD-private shadow-reservation
  output or callback implemented by this driver.
- `gpuva_g3_paging_windows.c:1260` and `:1267` record actual invalid-status
  bugchecks and the operation-specific retry rule. The checked wrapper at
  `:1282` poisons the process on an illegal UpdatePageTable result and completes
  the DDI. Returning STATUS_NO_MEMORY there is not a recoverable budget failure;
  changing it to ALLOCATION_BUSY is also invalid. DMA-buffer exhaustion asks for
  command space, not memory/PFN replacement.

These status semantics are also recorded in R155 and T7e from the official
[BuildPagingBuffer documentation](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_buildpagingbuffer).
This offline task uses the pinned headers and recorded documentation analysis;
it did not fetch or verify current web content.

### CPU Lock and persistent Map

`.local/reference/wdk26100/d3dumddi.h:3480` defines D3DDDICB_LOCK: `pData` is an
output of the runtime callback, and `:4319` defines that UMD-to-runtime callback.
Lock2 also returns `pData` (`:3931`). Ordinary MapAperture/UnmapAperture fields
have no output CPU pointer. There is no supported pointer-redirection hook in
those structures for this driver's current aperture path.

Do not overgeneralize this into "Windows has no KMD-controlled CPU mappings":
`d3dkmddi.h:3707` defines AcquireSwizzlingRange with CPUTranslatedAddress;
`:2733` distinguishes a segment's CpuTranslatedAddress from CpuHostAperture.
Those are separate segment/swizzling architectures, not authority to rewrite
an existing system MDL or an already returned CPU VA. Neither is implemented
by this path; adopting one requires a separate WDK callback/lifetime review
and admission proof. MapApertureCpuVisible is not by itself a pointer override.

The current CPU path is concrete:

- `render-admission/umd/src/umd_win32_screen.c:975` reserves a transition,
  restores SystemDirect residency, calls LockCb at `:1003`, and publishes
  LockedBase/Mapped at `:1024`; UnlockCb precedes clearing the mapping at
  `:1066`. These fields belong to the UMD, not the KMD allocation tracker.
- `render-admission/umd/src/umd_gpuva_windows.c:1289` protects residency through
  pointer publication. It does not announce CPU ownership to the KMD.
- `drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp:2618` returns an existing native heap
  CPU pointer immediately; `:2639` makes UnmapHeap a no-op for native heaps.
  App-level Unmap cannot therefore be used as proof that all underlying CPU
  pointers/locks have gone away. DDI, winsys, BO and LockCb lifetimes must join.

A persistent Map may write or read between GPU uses without a fresh paging
operation. A one-time bounce, or copy-back only at eviction, is incorrect.
An unshared BO with **no CPU mapping now and no future unmediated CPU access**
is a possible candidate. General+GPU_WRITE alone is insufficient.

## AGX/ASAHI CONTRACT

- `.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/pgtable.rs:39`
  fixes UAT_PGBIT=14. `mmu.rs:1090` retains the GEM scatter/gather ownership;
  `:1221` rejects address/size/offset misalignment. Scattered *native* pages
  are valid; four unrelated 4 KiB pages within one native leaf are not.
- `.local/reference/mesa/src/asahi/lib/agx_bo.c:362` rounds BO size/alignment
  before calling the OS allocator at `:379`. That is not a Windows backing
  guarantee. No external implementation is copied by this design.
- `gpuva_g3_paging_windows.c:593` retains original IPA/allocation/offset per
  logical page. `:643` requires four adjacent pages, aligned base and identical
  permissions/segment; `:660` removes a native leaf otherwise. `:738` preserves
  logical pages. The existing path correctly refuses unrepresentable storage.
- `drivers/apple-agx/shared/src/apple_agx_gpuva_g3_graph.c:470` publishes one
  native backing IPA, not a vector of four PFNs; `:690` checks native access.
  A shadow needs explicit separate native provenance: replacing the logical
  PFNs with shadow PFNs would corrupt VidMm's backing identity and CPU paging.
- `drivers/apple-agx/shared/include/apple_agx_pool_window_abi.h:35` fixes the
  broker window unit at 16 KiB; `:37` and `:44` describe range mapping and
  no-unmap-while-GPU-backed semantics. Stage-2 aliasing cannot gather four
  scattered 4 KiB host pages into an AGX native leaf under that interface.

| Responsibility | Retained owner / proposed addition |
| --- | --- |
| Allocation identity, system pages, residency schedule, CPU Lock result | Windows/VidMm |
| CPU mapping transitions, API fences and residency references | UMD; proposed explicit CPU/GPU epoch handshake |
| Shadow bytes, native mapping provenance, ordered paging copies and lifetime | KMD; proposed allocation-owned shadow record |
| Native grants, UAT/TLB acknowledgment, stage-2 window | m1n1; no new ABI presumed |
| Reserved memory and ACPI exposure through ExitBootServices | Mu; unchanged |
| Initialization, hardware execution/interrupts, power and recovery | Existing firmware/KMD/broker owners; unchanged by this offline branch |

## TRANSLATION

### Trigger and eligibility

Prefer UpdatePageTable as the authoritative trigger. Merge partial updates
with retained logical pages, expand 64 KiB input PTEs, then classify each
16 KiB logical group. MapAperture may record an earlier candidate but cannot
publish a process GPU leaf or certify its allocation-relative coverage.

Direct publication stays unchanged when the native group is representable.
A shadow candidate requires all four pages valid, uniform permissions, system
backing, one live canonical allocation, consecutive allocation-relative offsets,
a native-aligned allocation offset and GPU VA group, and the full group inside
the allocation. Missing pages, mixed permissions/allocations, unsupported flags,
protected broker pages, unknown lifetimes and partial tails are separate refusals;
never turn them into a shadow by padding or widening access.

Initially restrict shadow epochs to ordinary private General allocations with
GPU access, no imported/shared/scanout/primary/CPU-only/paging-table/native-fence
role, no aliases outside the tracked owner, and a verified zero CPU-lease count.
Multiple GPU VAs for one canonical allocation+offset must use the **same**
shadow, with per-alias permissions and reference counts. Initially refuse
unaligned aliases that cannot preserve this native-page identity. Do not key
shadow storage solely by process VA or by a reused allocation pointer; include
an allocation lifetime ID and backing generation. No size/bind/trace whitelist.

Persistent CPU mappings are ineligible for shadow publication. They retain the
existing direct path when representable and the existing refusal when scattered;
the proposal does not claim to fix that workload class. Existing C75c direct
mapping and idle trim behavior remain default. A future UMD change must resolve
CPU access before GPU admission, not silently disable direct BOs globally.

### Data authority and ordered transaction

Per canonical native group keep original PFNs and pinned lifetime, backing
generation, shadow page/grant, alias set, CPU leases, queued/GPU references,
paging-record references and last copy/publication/retire sequence. Logical PTEs
continue to describe VidMm's original pages. A separate native-backing record
explains why the graph leaf resolves to a different IPA. GPU copy/QUERY/parser
provenance checks must explicitly validate this association, not weaken identity
checks generally.

| State | Authority | Allowed transition |
| --- | --- | --- |
| System | System pages; no shadow leaf | Reserve capacity and close CPU admission |
| Prepared | System pages; immutable queued snapshot, both lifetimes held | Execute prior fills/transfers, copy in |
| GPU | Shadow; CPU access prohibited | GPU jobs/aliases share the same bytes |
| Retiring | Shadow; no new jobs/CPU leases | Join jobs, revoke aliases/TLB, copy out |
| System | System pages contain latest bytes | Complete paging/CPU transition, release shadow |
| Uncertain | Retain both sets, no new access | Confirm reset/revoke before reclamation |

1. **Prepare before paging can fail for memory.** Reserve physical shadow pages,
   grant/alias metadata, and record storage ahead of the first eligible
   MakeResident/MapGpuVA transaction. Hold the reservation through all retries
   and accepted residency references. At build time capture generation-bound
   source PFNs and operations, with lifetime references; do not borrow
   `pPageTableEntries` or an MDL beyond its documented validity. A raw PFN list
   is not a pin. Source residency/pin lifetime across deferred execution is a
   gate that must be proved in the actual WDK path.
2. **Execute copy-in, then publish.** Extend the paging record executor with an
   ordered shadow operation. Join prior GPU users across all aliases, execute
   earlier Fill/Transfer records, copy each 4 KiB source slice into its native
   shadow page, apply the established CPU/GPU visibility barrier, register the
   shadow backing, and publish native leaves/TLB acknowledgment. Only then can
   the paging fence and a dependent GPU access succeed. No second copy-in when
   adding an alias to an already authoritative shadow. No new physical
   allocations or waits under the G3 fast mutex.
3. **Make all data movers authority-aware.** TransferVirtual, physical Transfer,
   FillVirtual/Fill, copy escapes and any CPU-access bridge must resolve the
   canonical allocation+offset+generation to the current authority. A transfer
   out reads shadow bytes; a transfer/fill into an active shadow updates it or
   retires the epoch first. Existing records with only pre-resolved system IPAs
   are insufficient. Serializing copy-in after earlier records but leaving
   later records unaware still loses data.
4. **Retire before losing the system destination.** Eviction/transfer-out,
   relevant UnmapAperture, final mapping invalidation, CPU-begin and backing
   replacement stop new access and join outstanding jobs/records. Revoke every
   native alias, wait for TLB acknowledgment, then copy authoritative shadow
   bytes back into the *old generation's still-held* system pages (or the
   explicit transfer destination). Copy-out must finish before releasing source
   lifetime or reporting a successful paging fence/CPU lease. An unmap may be
   only one alias or partial range: retain the common shadow while other aliases
   remain, and conservatively drain the whole affected native group before
   changing any constituent PFN. Never copy into DummyPage or newly reused PFNs.
5. **Retain on uncertainty.** Copy/publication/revoke failures cannot signal
   successful coherence or free GPU-reachable pages. Known prepublication
   failures roll back without exposing a leaf. An uncertain TLB/grant/reset
   retains pages and poisons the affected execution path until reset teardown
   proves it safe. Discarding contents is allowed only with an explicit discard
   contract, not inferred from invalid PTEs. A partial operation retry must not
   reinitialize an authoritative shadow or double-release reservations.

CPU_VIRTUAL updates for the CPU-only paging process retain immediate logical
mapping semantics and do not themselves activate a GPU shadow. GPU_PHYSICAL
shadow publication needs the ordered operation above. Converting the current
build-time graph publication to deferred shadow activation requires tests of
root selection, generation checks, replay/resubmission and queued submission;
it is not a small branch in AdmissionG3UpdateLeaf.

### CPU ownership protocol (proposed, not an existing WDK guarantee)

For the restricted private-allocation path, introduce an authenticated,
generation-checked UMD/KMD CPU lease before **every** underlying LockCb or
returned cached pointer. CPU_BEGIN closes GPU admission, waits for queued users,
drains active shadow bytes to original backing, and only then allows LockCb.
On LockCb failure roll back the lease; on success keep it through pointer
publication and all nested maps. CPU_END occurs only after the final underlying
UnlockCb succeeds and all exported pointer lifetimes end. Nested API unmaps
that retain a native cached pointer do not end that lease.

GPU_BEGIN requires zero CPU leases, closes CPU admission atomically, and retains
that state through submission/retirement. If CPU_BEGIN races with preparation,
one wins the allocation lock and the loser retries outside all graph/pool locks.
Do not wait while holding the lock needed by paging completion. The KMD must
validate owner/handle/lifetime; a UMD-provided counter is not sufficient to allow
stale or foreign physical access.

This is possible only if **all** CPU access routes are mediated. Shared opens,
foreign D3DKMT locks, presentation/GDI aliases, cached winsys pointers and
persistent maps currently prevent a KMD-only proof. Receipt-only lease counting
can establish workload coverage before any shadow is enabled. The existing
receipt cannot establish that GravityMark's failing allocation qualifies.

Redirecting LockCb to the shadow is deferred: no applicable ordinary aperture
output field was found, and changing an existing persistent pointer is unsafe.
A new swizzling/CPU-host-aperture architecture would be a separate alternative,
not an assumed callback that this design already owns.

### Capacity and recoverable failure

**Never take a seemingly free offset from segment 3.** VidMm owns its whole
advertised address space. `AdmissionG3PoolPlace` at
`gpuva_g3_paging_windows.c:450` records VidMm placements and can replace previous
owners. `pool_windows.c:524` reserves backing-page quantity, not an exclusive
hidden address range. A private shadow placed in an unowned demand-pool slot
can collide with the next legal VidMm allocation.

Two legitimate architectures require further integration:

1. A real hidden companion allocation with a VidMm-owned segment-3 placement,
   its own residency references and charged budget. It must stay native and
   CPU-invisible and must not recursively demand a shadow. UMD/runtime must
   include it in residency operations; KMD cannot manufacture VidMm placements.
2. A dedicated KMD slab pool using a disjoint broker-window range **excluded
   from the advertised segment-3 range from initialization onward**. Reserve
   aligned physical pages at an early fallible admission boundary. Reuse the
   existing native chunk allocator, not arbitrary scattered PFNs. This requires
   reviewed window partition/capacity/broker-budget contracts and accounting for
   memory unavailable to VidMm. Current m1n1/Mu source is unavailable here.

Recommendation for a KMD-owned design: evaluate (2) after the CPU-lease gate,
with hard global/per-owner limits and physical pre-reservation; keep (1) as the
alternative if correct budget/error propagation requires runtime participation.
Neither is an approved implementation. A dedicated pool cannot create RAM:
shadow bytes coexist with original locked system pages, native page-table and
grant costs. For the sample 129 MiB, a full-allocation shadow adds another
129 MiB before metadata. The 2683/2688 MiB observation leaves about 5 MiB in that
pool, far short of a same-pool full shadow. The receipt samples only four PFNs;
it does not establish the total number of scattered groups. Worst-case reserve
must cover all eligible groups, not just those sampled.

Retain T8 M2-f7c's `HighMemory && !LowMemory` growth rule at
`pool_windows.c:388`, and pressure-driven safe reclaim at `:398`. High-memory
headroom permits growth; it is not capacity promised to an already accepted
paging operation. Reclaim respects owner, alias, paging, execution and broker
references. Once a transaction is accepted, pressure cannot cancel its reserved
copy-out capacity or force a new allocation while retiring it.

**E_OUTOFMEMORY at MakeResident is a gate, not a result of this design.** A KMD
cannot return that HRESULT from UpdatePageTable and obtain a retry. The existing
supported early failure is CreateAllocation's STATUS_NO_MEMORY at
`allocation_windows.c:519`, before publication; it is not MakeResident. To get
recoverable failure at the requested residency boundary, integrate shadow
reservation into the UMD-owned residency request/hidden companion accounting
before calling the runtime, balance partial MakeResident successes and fences,
and propagate a recoverable result through the actual D3D12 owner. No inspected
source establishes that full error path today. If that cannot be closed, reserve
at allocation creation and document earlier OOM rather than claim the requested
MakeResident behavior. Never use process poisoning as ordinary capacity control.

Current caller behavior makes this gate concrete: the normal General BO
`residency_request` in `umd_gpuva_windows.c:343` zeroes flags and retries
E_OUTOFMEMORY after trimming, but `restore_residency` at `:1279` collapses a
final failure to a Boolean; `:1301` then sets DrawTerminal on restoration
failure. A new reservation OOM must retain its recoverable identity through
these callers. Separately, the nonnative D3D12 allocation path sets MustSucceed
at `agx_d3d12_ddi.cpp:2177`; the pinned WDK explicitly says this requests device
error on residency failure (`d3dukmdt.h:1468`). Do not generalize that flag to
the normal General path, or reuse that separate path for recoverable shadow OOM.

## Implementation plan — independently testable commits

The first deliverable is offline safety/contract replay plus this design.
Subsequent production commits are conditional on the unknowns below, and must
be reviewed and tested individually; no cumulative shadow enablement until all
gates close. Use native inline execution and the repository's RED/GREEN process.

1. **Offline contract replay (this branch).** Extend
   `tests/g4_aperture_native_leaf.c` / `test_g4_aperture_native_leaf.py` to cover
   misaligned contiguous groups and CPU-only logical paging as well as existing
   scattered replacement. Add a clearly test-only shadow epoch specification
   with byte comparisons, copy/fence order, alias authority, persistent CPU
   leases, budget refusal and uncertain revoke. It must be labeled as a model,
   never as production shadow coverage. Mutation checks prove sensitivity.
2. **CPU-lifetime evidence only, after interface review.** Change
   `umd_win32_screen.c`, `agx_d3d12_windows.cpp`, `agx_d3d12_ddi.cpp` and the
   private transport/receipt definitions to correlate canonical allocation,
   lifetime, cached pointer, underlying Lock/Unlock and GPU uses. KMD records
   known/unknown ownership explicitly. Regression: nested maps, native cached
   pointers, failed Lock/Unlock, trim/map and two-thread races; missing coverage
   never produces eligibility. Preserve SystemDirect and existing trim defaults.
3. **Private storage reservation, no publication.** Add a dedicated shadow
   allocator module and explicit address domain only after selecting architecture
   (1) or (2). Tests: VidMm/private disjointness, saturation, fragmentation,
   concurrent reservations, high/low pressure, no accounting leaks, broker
   capacity and page/grant rollback. Prove ordinary OOM at an allowed API/DDI
   boundary before paging; supply actual Windows compile/ABI checks.
4. **Allocation epoch and authority resolver.** Add generation-tagged state to
   KMD allocation ownership; integrate CPU_BEGIN/END and GPU_BEGIN/end.
   Tests: every pointer publication path, all aliases, stale/reused handles,
   persistent map, partial MakeResident and fence failure. Contract review must
   show no unmediated CPU writer in the eligible class. No native shadow yet.
5. **Ordered copy/publication and reverse transfer.** Extend paging records,
   `gpuva_g3_paging_windows.c`, `paging_windows.c`, `memory_runtime_windows.c`
   and graph provenance together. Replace the test-only model with actual KMD
   replay hooks: scattered input -> prior fill -> copy-in -> acknowledged shadow
   leaf -> real G4 range admission -> simulated GPU store -> eviction/Transfer
   -> copied-back bytes -> fence -> release. Cover record resubmission, partial
   native groups, 4/64 KiB input, aliases across tables, CPU map, replacement,
   cancellation, teardown and uncertain TLB acknowledgment. Keep logical PFNs
   original; test all mover paths against the same authority resolver.
6. **Enable only after gates and checkpoint.** Audit whole-branch tests,
   protected invariants, Windows compile and footprint. Add bounded diagnostics
   for eligibility/refusal, both backing IDs, copy bytes, epoch/fence and reserved
   usage. Hardware owner preregisters the narrow checkpoint below; no claimed
   GravityMark fix before real validation.

Verification command for each applicable commit: targeted tests first, then
`cd tests && python3 run_against_baseline.py` (must print BASELINE_OK). Never
change known_failures.txt to hide environment failures. Ledgers remain with the
integration owner under the explicit handoff rule.

## WHAT IS STILL UNKNOWN

1. Whether the actual GravityMark allocation had any CPU pointer/lock, including
   cached internal maps; the receipt lacks this field. Whether the restricted
   eligible class covers the workload at all.
2. A complete documented/implemented CPU-access mediation contract for the chosen
   allocation class. The private lease protocol above is a proposal, not proof
   that Windows sends the KMD all locks.
3. Ownership of deferred source pages and allocation/MDL lifetimes for every
   relevant paging sequence; a PFN snapshot alone is insufficient. Exact
   copy-in ordering against initialization, and cross-process alias retirement.
4. Disjoint native-capable storage with correct VidMm and physical-memory
   accounting, broker limits, and sufficient headroom under the reported load.
   Current firmware source/ACPI must be inspected before choosing a window split.
5. Recoverable MakeResident OOM through the actual frontend/owner, partial
   successes and fences. Existing late status translation is terminal, not this.
6. Cache attributes/visibility barriers and reset/cancel/revoke ordering for the
   new two-backing path on J313. Existing direct-buffer behavior cannot prove
   byte coherence of new copies. Full driver and firmware replays plus hardware
   validation remain required.

These are implementation blockers, not minor TODOs. No production shadow code
is committed. The tests on this branch exercise existing native safety and a proposed
single-group authority model; they do not close Windows scheduling, locking,
cache, broker or OOM integration.

## Offline verification

Fifteen targeted tests passed, including the actual native-leaf ASan/UBSan
replay and five explicitly labeled design-model tests. The latter exercise
byte preservation through copy-in/publication/GPU write/copy-out, alias reuse,
nested CPU leases, prepublication capacity refusal and uncertain revoke.
They do not provide the still-unimplemented real KMD shadow/G4 replay from
plan step 5. Two extracted-C mutations (native alignment and CPU-only isolation)
and three model mutations (copy-in, copy-out and CPU-lease exclusion) failed
their assertions; the unchanged six-test aperture module passed afterward.
This is invariant/model sensitivity, not RED/GREEN of a GravityMark fix.

The required unmodified baseline runner returned exit 0:

```text
ran 399 tests in 171 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

The existing real-broker builder replay explicitly skips because the m1n1
submodule is uninitialized. No known-failure list or ledgers changed. All four
protected source substrings remain present. Read-only design and test reviews
found no blocking issue; the error-propagation evidence above incorporates the
design review finding. Exact commands, known failure names and logs are in
the task handoff `REPORT.md`.

## Checkpoint

**No hardware run on this test/design branch.** The next useful evidence is the
CPU-lifetime join from plan step 2; receipt-only evidence must establish a real
eligible allocation before enabling shadow access.

After all offline gates close, Claude's smallest hardware checkpoint is one
private writable General BO with verified zero CPU leases, deliberately forced
to scattered system backing under a bounded test budget. CPU initializes it
before GPU ownership; verify copy-in, GPU checksum/store through two aligned
aliases, ordered transfer/eviction, then CPU-begin/readback of exact bytes. Repeat
with a partial update and retirement; a persistent map must refuse shadow
activation safely. Exhaust the private reservation and verify recoverable OOM
before publication, with the desktop alive and no device removal. Vary sizes by
native/Windows page geometry, not the trace's 129 MiB special case. Only then
run GravityMark beyond 22 seconds and compare direct-path throughput.

WHY THIS HYPOTHESIS: (1) EXP1915's writable logical quartet cannot become a native
leaf; (2) T7c/T7e have excluded the proposed small placement/page-size controls;
(3) current Lock/Map pointers and paging copies use original backing, so explicit
authority transitions distinguish a coherent shadow from a one-time copy.

Preregister the single variable, source/artifact hashes, copy/fence evidence and
stop criteria before any run. Claude owns the package, known-good artifact and
ordinary GPU-visible exact-package cleanup; the GPU-hidden image remains only
emergency recovery. KMD changes follow the existing reboot contract. This note
changes neither recovery nor assisted/standalone launch ownership.
