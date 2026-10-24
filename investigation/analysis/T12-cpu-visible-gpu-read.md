# T12: CPU-visible GPU reads and bounded pool reclaim

Offline implementation plan and design. Scope: matched current KMD sources,
no hardware, guest, package, firmware or other-worktree changes. The supplied
EXP1923 receipts are the machine observation; no new live state is inferred.

## WINDOWS CONTRACT

The pool descriptor already reports CpuVisible=1, PopulatedFromSystemMemory=1,
Aperture=0 and a CpuTranslatedAddress (render-admission/src/memory_windows.c:21,
:188; topology in shared/src/apple_agx_physical_topology.c:61-85). T7c returned
CpuVisible=1 with only segment 3 in the supported sets; EXP1917 rejected that
shape. The recorded Microsoft GPU-upload-heap specification analysis in
R142-render-backing.md:66-69 states that WDDM 3.0 requires system-memory fallback
for CPU-visible allocations. This explains why removing the aperture conflicts
with this profile, even though the pool descriptor itself is CPU visible.
The newer VRAM-only lockable-surface architecture is not an already implemented
contract in this driver; do not advertise it or change interface/caps here.
That result does not prove a missing segment flag. The exact Windows
validation branch has not been captured. Pinned WDK d3dkmddi.h defines the
segment descriptor and allocation sets, but does not promise that this shape
is accepted by this GpuMmu profile. Do not repeat that rejected experiment or
claim that changing one undocumented flag fixes it.

UpdatePageTable is not a placement negotiation. The existing checked wrapper
in gpuva_g3_paging_windows.c:1330 documents the permitted paging results and
EXP1320's bugcheck when ALLOCATION_BUSY was returned for UpdatePageTable.
INSUFFICIENT_DMA_BUFFER requests more command space, not different PFNs.
There is no inspected documented recoverable result to relocate this backing.
Use the existing submission refusal contract, without changing paging status.

Primary ABI: .local/reference/wdk26100/d3dkmddi.h (DXGK_SEGMENTFLAGS,
DXGK_SEGMENTDESCRIPTOR4, DXGK_ALLOCATIONINFO, UPDATEPAGETABLE), and the cached
Microsoft contract analysis in T7e/T7f. This task is offline; current online
Learn content and Windows acceptance are not newly verified.

## AGX/ASAHI CONTRACT

Asahi pgtable.rs:39 fixes UAT_PGBIT=14. Four independent Windows 4 KiB PFNs
cannot form a single native leaf unless aligned, adjacent and permission
compatible. Mesa agx_bo.c rounds size/alignment, not Windows physical backing.
AdmissionG3UpdateLeaf checks this, retires the native leaf when unrepresentable,
and preserves logical PTEs for CPU paging. EXP1923 proves envelope validation
alone missed an indirect shader read into such an omitted upload-heap leaf.

Windows owns residency/system PFNs; KMD owns representability, publication,
GPU admission and pool lifetime; UMD owns CPU pointers and API synchronization.
m1n1 owns broker grants, UAT and stage-2; Mu owns firmware reserve/ACPI. Neither
platform owner changes here. Their submodules are uninitialized in this
worktree; current dirty source and launch artifacts cannot be revalidated here.
No external implementation is copied.

## TRANSLATION

Selected option (b), explicitly a **fail-closed GPU admission fix**, not a new
residency allocator. Maintain 2048 omission bits per leaf-table shadow, updated
only after a successful leaf transaction. A bit means at least one valid
logical GPU-process PTE has no native leaf. Partial updates can temporarily set
it, repair/unmap can clear it, and CPU-only paging never sets it. Reset and
subtree retirement maintain the same invariant. Before the common BeginJob
acquires a broker lease, under the paging lock, reject any process with a set
bit. This covers indirect shader reads, beyond the command/attachment parser.
It deliberately refuses even if the next shader would not touch that mapping.
No unsupported paging status, permanently poisoned partial update, new CPU
copy, or changed SystemDirect/placement/capability is introduced.

Options evaluated:

- (a) Keep as research, not a code candidate: segment 3 is already CPU visible;
  EXP1917 rejected the proposed allocation shape. No documented accepted
  alternative was identified in the pinned ABI/recorded Microsoft analysis.
- (b) Selected for the safety invariant. No valid UpdatePageTable retry can
  relocate; refuse GPU execution through the existing job-failure path instead.
  This can produce device removal under pressure. It does not make an omitted
  allocation resident or guarantee benchmark completion.
- (c) Separate CPU staging plus CpuVisible=0 canonical segment-3 allocation
  can support an upload heap only with copy-in before every consuming execute,
  after CPU writes, including persistently mapped pointers. Readback needs
  copy-out before CPU/fence completion. Existing private staging is a starting
  point, not proof that all persistent pointers and cross-context fences are
  covered. Three 128 MiB heaps cost up to 384 MiB copied per execute, plus
  duplicate backing and copy escapes; this forfeits C75c's zero-copy behavior.
  No equivalent-performance implementation is established. Do not silently
  disable the protected direct-buffer path to claim a production fix.
- (d) T7f shadow requires authoritative ownership epochs, aliases, reservation
  before paging, ordered copies and CPU mapping synchronization. Persistent
  CPU mappings defeat a paging-time-only bounce. Larger scope than (b).

Busy reclaim: remember the executed paging sequence at each Busy result,
including whether any sequence has executed. Keep the granule committed and
on the candidate list but issue no further release until that sequence
advances. Recommit resets the marker. Pressure bypasses idle aging only, not
this backoff or owner/paging/broker lifetime checks. Investigate existing graph
revocation before alleging a leaked broker grant.

## Implementation plan

Execute inline under the user's end-to-end authorization.

1. Extend actual leaf-update and BeginJob replays: omitted indirect resource
   rejects before broker lease; contiguous replacement/unmap reopens admission;
   partial update, CPU-only, broker refusal, rollback and table retirement.
   Observe RED, add omission bitmap and final gate, observe GREEN.
2. Extend the actual pool state machine with repeated immediate/aged Busy
   reclaim, same/older/new execution sequences and recommit controls. Observe
   RED, implement per-granule sequence backoff, observe GREEN.
3. Run affected modules and KMD CRT-header guard, required baseline until
   BASELINE_OK, inspect protected invariants and final diff. Small commits;
   README overrides global ledger workflow: integration owns CHANGES/EXPERIMENTS.
4. Write the handoff REPORT with exact evidence, test output and limitations.

## WHAT IS STILL UNKNOWN

This guard does not solve continued execution with scattered CPU-visible
backing. Windows allocation validation, hardware refusal/recovery behavior,
performance and WDK compilation remain unverified. A complete solution that
keeps GravityMark running needs an accepted native-backed CPU mapping contract
or a coherent staging design with measured acceptable cost. No claim of that
completion is made by host regression tests.

## Hardware checkpoint (integration owner only)

GravityMark D3D12 2k then 10k short profile, Wom1GpuFaultInfoRead=1. Require no
unmapped GPU fault and no Busy flood. If omitted leaves recur, GPU BeginJob
must refuse before acquiring a lease; distinguish this expected safety refusal
from benchmark completion. Successful completion additionally requires no
GPU-accessible segment-0 omissions. Collect fault, omitted-leaf, job refusal,
pool demand and hypervisor receipts before exact-package ordinary GPU-visible
rollback. Keep the known-good recovery artifact; no hardware action runs here.

## Busy ownership audit and limits

`AdmissionG3PoolPlace` clears the old allocation owner before the calling leaf
transaction replaces/revokes the old native mapping. The pool timer takes only
the pool mutex and can see that interval. PagingRefs counts CPU-only paging
PTEs, not GPU-process graph grants. Other process/VA aliases can also retain a
grant after the allocation loses its pool placement. Therefore zero owner and
paging references are necessary but insufficient for successful broker unmap.

`shared/src/apple_agx_gpuva_g3_graph.c:235-256` revokes a zero-reference system
backing. Replacement and unmap invoke it at :594 and :612; the retained-local
cache excludes frame-backed pool/system grants. No missing ordinary-path
release was established. A Busy result remains authoritative; never free the
slab or force a revoke based on those two pool counters.

The retry policy is intentionally conservative: a final native grant revocation
does not itself advance ExecutedSeq (pool_windows.c:505 and paging_windows.c:386
advance it after CPU paging records execute). A Busy granule can remain committed
until another such record executes, even after its grant is gone. This fixes
repeated commands within one epoch, not all pressure or prompt-release issues.
An m1n1 print rate limit is proposed only in REPORT; m1n1 is unchanged.

## Implementation/verification record

RED observed in the actual leaf replay (missing omission bit), actual BeginJob
replay (GPU lease acquired with an omitted indirect resource), and actual pool
state machine (second reclaim retried Busy). GREEN includes broker refusal,
partial map/repair/unmap, rollback after an earlier group published, CPU-only
paging, alias-protected subtree retirement, reset, and same/older/new executed
sequences. Sanitizers are enabled for the C replays. Exact final commands,
counts, baseline line and commit hashes are in the handoff REPORT.

Bitmap cost: 256 bytes per table shadow; each 32 MiB VA span has 2048 native
leaves. A 128 MiB heap spanning four leaf tables adds 1 KiB. BeginJob scans 32
words per shadow, stopping on the first omission, rather than rescanning PFNs
or copying heap data. Healthy C75c allocation, maps and zero-copy transfers are
unchanged. Hardware timing is unmeasured.
