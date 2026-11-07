# Shared private-scene pool pressure

## Proven boundary and source evidence

EXP1378 immutable `rcpt-tool-fl11cs-a/Wom1G3PrivateAcquireFailure.bin` (decoded
`private-acquire-failure-decoded.json`) records Branch11, c000009a,
PreparePredicate3, FailedRange8, global1024/1024, owner42/246, largest run0,
64x64 render, two queued scenes, zero quarantine/uncertain/job/lease.
`AppleAgxG3PrivatePrepareObserved` captures this **before** rolling back its
partially allocated scene. Owner42 therefore includes temporary earlier scene
ranges, not only permanent ownership. The pool really had no free physical
unit when range8 was attempted; neither requester quota nor a GPU submission
error explains this snapshot.

The adjacent `Wom1G3PrivateProcs.bin` is a **different, rate-limited snapshot**:
850 global units, largest run75, 13 processes/11 listed, 58 explicitly cached
scenes across the listed owners. Its units sum to850. It cannot establish the
exact ownership of all1024 units at failure or rule out every leak. It does
establish retained foreign caches in the same workload. Source provides the
causal defect: adapter-wide allocator, per-owner cache, and allocation retries
that evict only the requester's cache. An owner with zero cache cannot recover
unused storage held by a different healthy process. This is a reclaim policy
bug; there is no evidence to call it a lost-unit leak.

Sources inspected:

- `drivers/apple-agx/render-admission/src/gpuva_g3_windows.c`: prepare, cache,
  trim, eviction, reap, release, retire context, submit, BeginJob.
- `drivers/apple-agx/shared/include/apple_agx_g3_private_{pool,storage,failure}.h`:
  physical units, owner VA/quota, generations, rollback, failure snapshot.
- `drivers/apple-agx/shared/src/apple_agx_gpuva_g3_graph.c`: TryLeafBacking,
  owner-keyed UPDATE_LEAF and backing revocation.
- `.local/reference/asahi-linux-asahi/drivers/gpu/drm/asahi/buffer.rs`: separate
  manager heap and temporary scene storage; 128KiB blocks; manager only grows.
- `.local/reference/mesa/src/asahi/lib/agx_bo.c`: failed allocation evicts unused
  BO cache before retry. Read for behavior; no external code copied.
- `m1n1_windows/src/hv_agx_gpuva_v5.c`: publish_entry refuses owner's live jobs,
  synchronizes tables and invalidates every occupied slot for that owner.
- `mu/Platform/MacBookAirMid2020Pkg/AcpiTables/J313AppleAgx.asl.inc`: APPL0002
  binding/resources unchanged. Firmware is not the cache/extent owner.
- Microsoft [DXGKDDI_ESCAPE](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/nc-d3dkmddi-dxgkddi_escape):
  driver-private UMD/KMD communication at PASSIVE_LEVEL; allocation refusal is
  possible. No WDDM capability/scheduler/segment advertisement changes apply.

## WINDOWS CONTRACT:

FULL GRAPHICS. Escape verifies process, device and context handles before
access. This is KMD-owned private backing, outside ordinary application GPUVA
allocation bookkeeping. An ACQUIRE must retain valid owner quota, generation,
root and lifetime checks. A retry must never reclaim live or uncertain storage.
The fix does not claim unlimited physical capacity; true uncacheable exhaustion
still returns failure.

## AGX/ASAHI CONTRACT:

A manager's page/block lists and grow-only heap survive scenes; scene temporary
storage can be reused only after completion/report and release. Foreign cache
reclaim must operate on its original owner's graph. m1n1, rather than an ACPI or
Windows-driver workaround, remains responsible for owner-slot invalidation and
refusing table mutation with live owner jobs. Heap shrink is deliberately absent.

## TRANSLATION:

Keep own LRU eviction first. Only after own cache is exhausted, permit one
foreign safe cached scene per retry, in process-list order and oldest within
that owner. Use the exact failed allocation's byte count and pre-rollback stats
to exclude requester quota and nonphysical failures (sufficient contiguous free
run). The allocator hard per-extent 8MiB ceiling is checked before any eviction;
a separate RED/GREEN assertion verifies that a 9MiB refusal frees nothing. The pre-rollback owner count answers whether **that failed allocation**
would violate quota; it is not reused as permanent accounting. Each retry runs
the real allocator afresh and obtains a new diagnostic after rollback/reclaim.
Fresh-manager range2 uses backed minimum heap blocks, not the full reserved VA
window. Table and heap allocation failures use their own exact sizes/snapshots.

Foreign selection excludes poisoned, uncreated, uncertain, in-flight and leased
owners, and queued/submitting/quarantined/unreported/unreleased scenes. It calls
the existing victim-owner release function: unpublish all leaves through broker,
then zero bytes, revoke backing and generation-checked free. Any release failure
stops this reclaim pass; existing quarantine/poison behavior remains. Manager,
heap, tables, root and context ownership do not transfer.

Serialization audit: PrivateEscape holds State->Lock from process lookup through
reuse/eviction/publication; PrivateReuseScene clears Cached under it. The render
SubmitCommandVirtual path takes the same lock before finding the private scene
and setting Submitting/Queued. Pending-queue publication nests SchedulerLock
inside State->Lock; BeginJob takes State->Lock before graph bind/job begin.
Completion/report and context retirement also acquire it. DPC cancellation only
publishes an interlocked marker and cannot activate a cached scene. Thus no
cached-to-submitting/queued/lease/job transition can interpose between foreign
eligibility and unmap/free. Broker publish_entry independently rejects live owner
jobs and invalidates the victim's slots.

## Deterministic verification

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A full shared physical pool must recover an unused foreign cache without
freeing a queued/submitting/leased/in-flight/poisoned/quarantined owner's scene,
or evicting foreign cache to work around a requester quota refusal. Fragmented
physical allocation must retry after a large enough contiguous extent is freed.

RED command: `python3 -m unittest discover -s tests -p test_g3_private_global_pressure.py`
failed with old requester-only pressure at the successful-foreign-eviction
assertion. The test compiles extracted production policy with the real allocator
and ASan/UBSan. GREEN same command passes: 1024/1024, requester24, only owner6
safe, reclaim20 units, then allocate; quota refusal and failed release retain
foreign storage; fragmentation19-unit run cannot satisfy32 units until an85-unit
foreign scene is reclaimed. Unsafe owner/scene classes are skipped.

Existing `test_g3_private_pool`, `test_g3_private_storage`, cache LRU and request
policy are run alongside it. Replay shim includes the new state and functions;
preexisting broader replay compilation defects (missing G4WithCompute,
G4ReparseV5 and related fields/receipt declarations) remain out of scope and must
be reported by the integrator, not described as a green full suite.

## WHAT IS STILL UNKNOWN:

Hardware must verify that owner-specific foreign graph invalidation/revocation
works during this workload, and that ordinary completed scenes are released into
cache promptly enough for global pressure reclamation. No hardware run/build was
performed in this lane; parent owns packaging, signing, preregistration/recovery.

## Dedicated hardware checkpoint and receipt

New `windows/icd/agx_d3d12_kmd_pool_probe.c` reuses the established pixel-checked
C6B triangle, with its own main and fresh child-process/device/queue/fence.
Children hold their device until a named release event. Parent has a110s
seeding bound and adds one bounded
child at a time (up to48), checks readiness/process death within20s, and stops
adding load as soon as a new foreign-reclaim receipt is observed. It then releases
and joins all children under one shared20s cleanup deadline, fitting the existing180s tool harness. No new escape or special admission path is introduced.
A child failing to finish is killed only during owned-probe cleanup. Total child
hold is bounded160s. Compile this separate source with ordinary probe shader
headers/libraries plus advapi32.lib; no ordinary-probe source edit is needed.

`AGX_D3D12_KMD_POOL: PASS` requires every completed child's pixel/fence/device
check and clean exit, plus a **new** successful foreign-reclaim receipt. No
pressure reached is FAIL/unproven, never a silent PASS. Ordinary fl11cs-a success
alone cannot validate this fix. Preserve stdout and before/after receipt binaries,
plus existing private failure/process/paging receipts. Run this separately from
the SM5.1 checkpoint and give each its own verdict. Parent still uses the exact
hash-verified experiment package and normal exact cleanup/recovery.

`Wom1G3PrivatePressure` is 12 little-endian ULONGs (48 bytes):

| Word | Meaning |
|---|---|
|0|version1|
|1|cumulative successful foreign-scene reclaims this boot/state|
|2|requester OS PID|
|3|victim OS PID|
|4,5|actual global units immediately before/after successful release|
|6|freed units (4 minus5)|
|7|actual requester units before release (after failed-prepare rollback)|
|8|failed allocation requested bytes|
|9,10|victim root IPA low/high|
|11|actual largest free contiguous run after release|

The KMD snapshots the receipt under State->Lock and writes after unlock only
when the count changed. Hardware PASS checks count increased relative to before,
requester/victim differ, positive exact freed-unit accounting, valid root, and
all worker results. Under concurrent unrelated reclaims the latest record can
be overwritten; retain before/after plus full stdout, and treat absence of a
new record as an unmet discriminator. Never infer that the historical EXP1378
snapshot itself had exactly the later receipt's cache distribution.
