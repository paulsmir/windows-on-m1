# RC51: native placement constraint and idle pool reclaim

The universal single-copy canonical-map policy is **not implemented**. Option A
(native-only support for every ordinary CPU-visible allocation) is rejected by
the current Windows contract. Option B requires control of aperture backing that
the inspected WDDM interfaces do not provide. This branch fixes a distinct,
reproducible idle-reclaim leak. It does not fix EXP1974's incompatible backing or
establish that GravityMark 100k/200k canonical maps finish correctly.

Work is offline on `codex/rc51`, base `eb1bd163`. No hardware, guest, builder,
network, package or other-worktree action. Integration owns experiment/change
ledger updates. The task's pinned reports and RC50 evidence were read completely.

## Observations and causal decision

RC50's exact EXP1974 arm-B join remains decisive: mapped/locked SystemDirect
allocation `0x400035c0`, VA `0x1192c10000`, 128 MiB, PID 6472, later receives
five segment-0 updates omitting all 8,192 native groups, then LeafAbsent. See
`RC50-canonical-map-native-refusal.md:25` and shared handoff
`rc50/evidence.json`. This is a saved observation, not a new hardware result.

RC51 decoded the specified EXP1973 logs directly. Paths below are relative to
shared `.local/experiments/EXP1973-readiness-cache/evidence/`:

| Evidence | Meaning |
| --- | --- |
| `EXP1973/gm1973a-out.txt:19,32,46,48` | FHD 100k completed at score 1275 / 7.6 FPS. |
| `EXP1973/gm1973b-out.txt:29-37` | FHD 50k reached Ready, started its benchmark and resized its frame. The saved output does not say it failed before scene load. |
| `EXP1973/kmd-receipts-probe-end.txt:161` | All 64 reserve failures are headroom vetoes. CommitFailures, NewSlabFailures, BusyRetries, ReleaseFailures, PinnedGranules and Candidates are zero. |
| Same receipt | Last refusal: 24.75 MiB request, 3019.375 MiB promised, 3020 MiB held, 2985.875 MiB committed; sampled HighMemory and LowMemory both clear. The resulting promise was below the 3136 MiB capacity. |
| Same receipt | Endpoint held=244 MiB, promised=235.8125 MiB, committed=202.3125 MiB; 4611 slabs acquired minus 4489 freed equals 122 held. Peak held=3136 MiB. |
| `rec1973h/kmd-receipts-pre-disable.txt:161` | Later held=268 MiB; no new failures or busy releases. Other live desktop work can change totals. |

These are post-arm aggregate receipts, not exact between-arm snapshots or a
per-process allocation census. They exclude a stranded-busy/pinned pool leak as
the recorded cause and show substantial physical return. They cannot establish
what other system/process memory remained at arm B's failure. The observed
creation boundary is the protected HighMemory headroom policy, not physical slab
allocation failure or the aperture geometry failure of EXP1974. No override of
that protected policy is made.

Handoff `rc51/decode_evidence.py` reproduces `evidence.json`, including input
SHA-256 values and line numbers. The original logs are read-only.

## Segment policy: why neither proposed shortcut is a valid implementation

Driver references in this note are relative to `drivers/apple-agx/`.

1. `render-admission/src/allocation_windows.c:515-554` preserves aperture in
   CPU-visible class allocations' support sets while preferring pool segment 3.
   The native-only GPU-invisible case already exists there and at `:558-574`.
   `EvictionSegmentSet=0` at `:587` does not remove aperture from the resident
   supported set. GPU-only eviction to VidMm backing and native page-in is a
   different contract from permitting resident, GPU-referenced system pages.
2. Pinned WDK26100 `d3dkmddi.h:3928-3935` separates segment preferences,
   supported sets and eviction sets; `:3804` carries CpuVisible. Preferences
   do not impose a physical-page contract. Segment masks use the declared
   aperture ID 1; segment 0 in PTE receipts denotes actual system backing.
3. The **cached current runtime itself** has the diagnostic
   `CPUVisible allocations must include an aperture segment in the supported
   segment set` at dxgmms2 RVA `0xf61e8`. The checked instruction sequence at
   `0x88758-0x887b0` compares the runtime version field against 3000, checks
   CPU-visible bit 0, aperture-set summary bit 1, a supplied pointer and an
   internal bit-30 exception, then rejects through `0x87b80-0x87bb8` with
   **STATUS_INVALID_PARAMETER (0xc000000d)** at literal `0x89488`. This agrees
   with EXP1027/1266/1917's saved native-only admission failures. Internal
   exceptions are not public KMD flags or a proven backing-provider API.
4. `rc51/vidmm-admission.py` pins dxgmms2 SHA-256
   `abeb2c41191ce793ad7806e4f2599428d0402fab5537bc26490e8abd0bab2335` and emits
   the exact instructions in `vidmm-admission.txt`. The prior official-spec
   inventory at `R142-render-backing.md:57-69` independently records the
   WDDM 3.0 fallback requirement. This task used local sources only; it did not
   freshly fetch Microsoft's GPU-upload-heap specification or certify a newer
   WDDM admission contract. Production is WDDM 3.0 in
   `render-admission/AppleAgxRenderAdmission.vcxproj:24-27`.
5. Aperture descriptors (`render-admission/src/memory_windows.c:22-59`) describe
   a mapping facility, not a native-page allocator. WDK `d3dkmddi.h:2720-2749`
   defines size, aperture CommitLimit and CPU translation; `:4951-4958` and
   `:5043-5051` give MapAperture its input MDL/ADL, with no replacement output.
   `:3918-3923` explicitly marks Minimum/RecommendedPageSize as **inputs** over
   Alignment. `:2113-2161` describes page support, not mandatory native PFNs.
   The existing 64 KiB opt-in and subsequent scattered backing are recorded in
   `M2-vidmm-64k-decision.md:92-106`. Repeating those knobs is not Option B.
6. `render-admission/src/gpuva_g3_paging_windows.c:723-745` requires complete,
   aligned, contiguous, equally permissioned native quartets. Windows's given
   logical pages remain the content authority. Replacing scattered originals
   with new contiguous pages without a coherent CPU/backing lifetime owner
   silently creates a second copy; it is not a segment flag correction.

Decision: preserve the admitted segment outputs and the full native proof.
Native-only placement is the correct invariant for GPU-only allocations, but
there is no supported extension to all ordinary CpuVisible allocations in this
inspected profile. A different CPU mapping/backing owner or a documented newer
Windows contract is needed. Changing a reserved bit, hiding CpuVisible while
continuing to Lock2, changing the WDDM version, or returning paging success with
missing native data does not satisfy the task.

## Backing promise, persistent maps and finite capacity

`render-admission/src/render_pool_slab.c:22-31` retains
`min(live reservations, published capacity) + private leases`. Creation obtains
the promise at `allocation_windows.c:537-542`; destruction releases it at
`:690-696`. This continues to protect RC28's serial page-ins when no later OS
allocation can succeed. The actual headroom/allocator replay exercises three
serial restores, over-cap live reservations, replacement placements and
destruction-driven shrink. It is not an execution of VidMm or proof against
every possible Windows 0x10E cause.

The premise “Locked means permanently native” is contradicted by EXP1974.
`umd/src/umd_win32_screen.c:1227-1335` restores residency, locks, proves the current
pool mapping and publishes the CPU pointer. `umd_residency_trim.h:25-42` excludes
mapped SystemDirect allocations from this UMD's trim; it does not pin VidMm's
physical placement. `windows/icd/agx_kmt_gpuva_bridge.c:453-470` uses Lock2 and
rejects extra legacy flags. WDK `d3dukmdt.h:567-577` reserves every Lock2 flag;
`:1459-1490` gives MakeResident budgets, handles and a paging fence, with no
requested physical segment. CantTrimFurther can still fail beyond maximum
budget; MustSucceed requests a device error, not unlimited memory.

Even a future supported native-only map contract cannot fit more simultaneous
unmovable mapped bytes than the native segment. If every eligible victim is
held and the remaining requested working set exceeds capacity, a fallible
allocation/residency/map boundary must report failure, or clients must release
holds. Retaining min(live,cap) physical bytes protects placement materialization;
it neither enlarges capacity nor proves all persistent maps can succeed.
No silent eviction of exposed CPU pointers or new memory-pressure guarantee is
introduced. The 3136 MiB capacity and protected headroom veto stay unchanged.

## Confirmed leak and correction

The normal destruction chain is sound: `allocation_windows.c:690-706` invalidates
identity, forgets the placement, unreserves, frees the allocation and reclaims.
`render_pool_demand.c:190-245` requires no owner/pin/paging references, completed
records and acknowledged broker unmap. `pool_windows.c:366-380` gives a slot
back only after UNMAP_RANGE succeeds; `:305-310` returns whole empty slabs with
MmFreePagesFromMdl/ExFreePool. `render_pool_slab.c:27-46,122-126` retains promises
and at most the configured empty spare; pressure removes that spare at
`pool_windows.c:401-411`. Partially occupied slabs remain legitimately held.

The leak is at `render_pool_demand.c:210-214`: after one broker Busy reply,
reclaim skips the granule until ExecutedSeq/AnyExecuted changes. That includes
the existing periodic worker, so no later paging means no future unmap attempt.
However `gpuva_g3_windows.c:2520-2600` tears down process graphs independently;
the broker's backing hold can end with no KMD paging-record completion. A timer
can continue waking forever while never returning the now-unreachable pages.

The fix at `pool_windows.c:585-598` acquires the existing pool mutex, clears only
the cached Busy suppression on candidates during a periodic wake, then calls
the same reclaim policy and unlock/rearm path. The timer period remains 500 ms
(`:209`, `:478-483`). Ordinary frequent reclaim calls still suppress same-epoch
retries. Retry cost is bounded to one attempt per eligible candidate per wake;
this is not a fixed whole-worker execution-time bound. All owner, pin, paging,
sequence, aging, promise, stop and broker-acknowledgement checks remain.

There is no new public function, layout, wire ABI, capability or firmware
change. The header change documents who may clear the retry suppression.
Actual worker/policy bodies compile as C and C++ in the host harness, with
`extern "C"` declarations linking the C allocator objects. Native WDK/MSVC x86
`/TP`, ARM64 package compilation and live worker timing remain integration gates.

The RED replay fails on the old worker's missing retry. GREEN proves an
independent broker release returns surplus without a new paging epoch, and
covers continued Busy, hard refusal, aging, pressure, owner/pin/paging-record
holds, stop, spare policy and successful surviving-allocation restore while
physical allocations are denied. **EXP1973 has zero BusyRetries, so this fix
is not attributed to its observed OOM.**

## Primary implementation comparison and ownership

Asahi's native 16 KiB page contract is in the local primary
`asahi-linux-asahi/drivers/gpu/drm/asahi/pgtable.rs:39-43`, GEM alignment/cache
policy at `gem.rs:160-211`, retained DMA SG backing at `mmu.rs:1191-1229`.
Linux supplies compatible minimum page geometry; Windows logical 4 KiB pages
do not gain that property from allocation alignment.

Saved exact m1n1 `075e3980` sources in shared `rc46/source-evidence/` accept
eligible raw RAM with native alignment (`src/hv_agx_gpuva_v5.c:459-521`). Pool
unmap checks actual broker holds and synchronizes translations before success
(`src/hv_agx_pool_window.c:118-150`). Neither path promises that the hold changes
only when Windows executes a paging record. Saved Mu `361a2134` in
`rc30/source-evidence/` publishes RAM and owns reserved ranges in
`MemoryInitPeiLib.c:252-290,440-459,510-514`; changing it cannot repair this
Windows placement/reclaim boundary. No external implementation was copied.

VidMm owns placement, backing store and budgets; UMD owns residency references
and CPU pointers; KMD owns MDLs, window mappings and ordered reclaim; m1n1 owns
grant authorization and UAT/stage-2/TLBI acknowledgement; Mu owns boot RAM/ACPI.
Initialization, runtime DMA, interrupt, power and recovery owners are unchanged.
Live ADT/registers/IRQ routes, generated ACPI and launch contracts were not newly
measured; source exports and supplied saved experiments bound the analysis.

## Remaining gate and smallest future checkpoint

No new canonical-map benchmark is justified as a cure by this branch. The
production placement requirement remains unresolved; it needs a documented
native-compatible owner for all persistent CPU/GPU references, with finite
capacity/backpressure and coherent eviction/restore semantics.

Independent review also identified an inherited stop-timeout risk, outside the
delivered correction: `pool_windows.c:652-660` detaches Context->PoolDemand when
a worker is still pending, while the unlock path at `:427` retrieves the runtime
through that field. A worker already in reclaim can later encounter the detached
field. The original worker used the same path. A separate lifetime repair needs
a regression that resumes the actual worker after timeout and retains its full
runtime/context lifetime; the existing stop test only models pending counts.
No native stop-timeout safety claim is made by this branch.

For the delivered reclaim correction, integration can exercise: destroy/unplace
an allocation while broker-unmap is Busy, end the last grant with no new paging
record, and verify periodic reclaim returns surplus after acknowledgement.
Surviving allocations must retain exact bytes and restore under denied growth.
Measure worker cost as well as held/committed/reserved bytes. Preregister exact
artifact hashes and preserve known-good recovery; collect evidence before the
normal exact-package GPU-visible cleanup. Emergency GPU-hidden remains fallback.

Once the missing placement implementation passes persistent CPU write -> GPU
read/write -> CPU readback under pressure, run **FHD 1920x1080 100k then 200k,
canonical maps enabled**, plus repeated same-boot arms with receipts before and
after each process exits. Compare 100k to EXP1973 **1275 / 7.6 FPS** and the
supplied macOS **7700 / 5500** scores at 100k/200k. Capture actual dimensions,
pixels, score, FPS, memory/refusal provenance and worker cost. These are targets
and conditional future checks, not results or predictions from this work.
