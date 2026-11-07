# RC48: transactional reservation rollback; elastic segment remains incomplete

Offline on `codex/rc48`, base `69ca3297`, 2026-10-26. No hardware, guest,
builder, package operation or other-worktree access. This change fixes excess
physical backing retained after a failed creation. It does **not** implement
the requested production elastic segment, remove the headroom veto, raise
capacity, or release the backing promise of evicted live allocations.

## Observed boundary and the two independent protections

Saved evidence is under the shared `.local/experiments/EXP1971-escape-batch/`.
The exact v8 record at `evidence/EXP1971/kmd-receipts-probe-end.txt:159` and
`evidence/rec1971h/kmd-receipts-pre-disable.txt:159` has 43 ReserveFailures,
43 FloorRefusals, zero NewSlabFailures and zero CommitFailures. RC46's
`failure-evidence.md:73-116` decodes the accounting and final refusal.

All driver paths below have prefix `drivers/apple-agx/render-admission/`.

- `src/pool_windows.c:389-396`: `HighMemory && !LowMemory` decides whether
  creation may take another slab. Missing HighMemory also refuses growth.
  This is a conservative **Windows/DWM headroom policy**, not proof that the
  next MDL allocation cannot succeed. It samples system pressure; it neither
  reserves Windows working-set bytes nor guarantees system commit headroom.
- `src/render_pool_slab.c:22-31`: retained backing covers
  `min(Reserved, ReserveCap) + PrivateCount`. This separate protection covers
  **already-admitted placement/restore** when Windows can supply no new pages.
- `src/allocation_windows.c:531-542`: creation reserves backing and may return
  STATUS_NO_MEMORY. Destruction ends its promise at `:693-696`. Eviction is
  not destruction and does not repeat CreateAllocation on each page-in.
- `src/pool_windows.c:258-302`: physical slabs already grow lazily at creation,
  requesting 2 MiB contiguous then 32 aligned 64 KiB chunks, with every PFN
  checked. `MmAllocatePagesForMdlEx` remains fallible.
- `src/render_pool_demand.c:190-245`: reclaim requires no owner, pin or paging
  reference, executed sequence and acknowledged broker release. Reclaim of a
  window mapping does not imply that its underlying slab can be freed.

EXP1971's final 64 MiB refusal would raise live promises from 3036.4375 to
3100.4375 MiB, below the 3136 MiB segment cap. Held backing was 3042 MiB.
The event veto occurs before requesting more slabs; this is not an observed
physical allocator failure. Conversely those saved counters do not prove
that bypassing the veto would preserve Windows/DWM or load the full scene.

The distinction matters because removing the high gate previously enlarged
allocation reach while DWM failed with STATUS_FATAL_MEMORY_EXHAUSTION; the
saved rationale is `investigation/analysis/M2-f7c-dwm-headroom.md:8-34,60-78`.
The handoff's `protected-invariants.txt:5` records EXP1914 validation of the
restored gate. README explicitly requires a separate proposal rather than
committing a weakened protected invariant. An asynchronous clarification was
requested; absent an override, this branch preserves it. The task's general
request to fix a veto is not evidence that actual-allocation-only admission
is a safe replacement for the system-pressure protection.

## Confirmed rollback defect and fix

At the base revision, reserve failure restored Reserved and called Trim.
With the production SpareLimit=1, Trim could retain a slab allocated solely
for the failed creation. Example in 64 KiB slots: old promise 32 with one
occupied slab; request 64 more; acquire one slab, fail the next. The request
is rejected, but the new empty 2 MiB slab remains cached. Pressure reclaim
may subsequently return it if HighMemory is clear; an allocation failure
while HighMemory remains set does not take that immediate reclaim path.
This is unnecessary retained memory, not an unbounded leak and not the cause
of the 43 recorded gate refusals.

`src/render_pool_slab.c:100-112` now records the old list head and returns the
new prefix on failure. AddSlab prepends empty slabs (`:48-66`), reservation
never hands out slots, and all slab APIs require the caller's pool lock
(`include/render_pool_slab.h:62-67`). Consequently the prefix belongs only to
this transaction. The loop does not free any preexisting occupied/private/
spare slab. Reserved rolls back; cumulative NewSlabs/FreedSlabs and PeakSlabs
still describe actual transient work; PeakReserved records successful totals.
No public function, structure, symbol or receipt ABI changed.

`tests/render_pool_slab_test.c:185` tests allocator failure and a headroom
veto after partial growth, preserving older promises, an occupied slot and
existing spares. It also tests a null old-head with two new slabs to unwind.
RED: the old code fails the retained-slab count assertion. GREEN: the fixed
allocator passes under ASan/UBSan. This is the independently reviewable fix.

## The elastic contract still needs a restore admission boundary

Let S be published segment capacity, L live admitted slots, H held native
slots and C current residents. If all further native allocations fail, VidMm
can restore up to min(L,S) slots of existing allocations without another Create/Open.
A necessary backing bound, ignoring overlap/private overhead, is

    H >= min(L,S), or free reserve >= min(L,S) - C.

An allocation-creation gate cannot help a third serial restore when only two
restores' worth of backing remain. This does not depend on concurrent paging.
Raising S makes the worst-case obligation larger. Neither a smaller reserve,
a refill worker, arbitrary paging failure status nor success with absent
native leaves satisfies the requirement.

`tests/pool_headroom_test.c:220` executes the actual Windows reserve/reclaim/
unreserve wrappers and portable allocator: three admitted allocations are
evicted, further allocation is denied, and all three restore serially with
no new allocation. Destruction then returns safe backing one allocation at
a time. `:256` checks live reservations above the segment cap, changing the
resident set without new physical allocation, and final shrink. The RC48
handoff's `check_restore_mutation.py` temporarily caps the floor to two batches;
the third restore assertion fails. Tracked production source is not mutated.
These are host policy/allocator tests, not execution of Windows paging DDIs
or a general proof against every 0x10E cause.

## Capacity numbers and primary contracts

No capacity number changes in this candidate:

| Quantity | Exact current value / consequence |
| --- | --- |
| Saved Windows RAM | 7,218,794,496 B = 6884.37890625 MiB |
| Current native segment | 3136 MiB = 3,288,334,336 B |
| Synthetic CPU window | 4096 MiB |
| Broker coverage | 4608 MiB, shared with other registered system backing |
| Broker reserve in sizing | 256 MiB; broker-derived native ceiling 4352 MiB |
| Window-clamped ceiling | 4096 MiB, before the tighter Windows RAM formula |
| Windows graphics allowance from stored accounting study | 3442.189453125 MiB on this RAM total, before aperture allowance |
| Current formula | floor64MiB(RAM/2 - 256 MiB) = 3136 MiB |

`src/render_pool_demand.c:261-284` computes the RAM/window/broker bound.
A memory segment cannot truthfully advertise 4608 MiB while permanently
capping retained backing below min(live admitted slots, advertised capacity)
without another supported restore-admission mechanism. Pinned WDK26100 `d3dkmddi.h:2720-2749` says the independent
CommitLimit applies to an aperture, not this memory segment. Its
`:2565-2589` distinguishes system-populated memory from reserved firmware
DDR. The stored Microsoft graphics-accounting study at
`investigation/analysis/M2-f3-demand-pool.md:9-13` counts system-populated
segments against the graphics allowance. Raw online docs were not refreshed
in this offline task; the accounting conclusion uses that stored study.
Faking dedicated VRAM/firmware reservation is not an established replacement.

Primary sources inspected, in required order after saved evidence:

- Local Asahi Linux `drivers/gpu/drm/asahi/Kconfig:18` requires 16 KiB pages;
  `mmu.rs:1191-1228` pins GEM DMA ranges and checks native alignment. Mesa
  `src/asahi/lib/agx_bo.c:355-390` falls back through cache/allocator and can
  fail. Linux's native page geometry does not transfer to Windows PFNs.
- RC46's exact exported m1n1 075e3980 source:
  `src/hv_agx_gpuva_v5.c:459-521` already registers eligible raw guest pages,
  translating IPA and checking owner/generation. `hv_agx_power_mmio.c:115-133`
  sizes metadata to window plus extra, bounded by RAM. More broker metadata
  does not provide restore RAM or change the Windows segment contract.
- RC30's exported Mu 361a2134 `MemoryInitPeiLib.c:440-459` removes the backing
  of the low alias from ordinary RAM before publishing it; `:510-514` retains
  the firmware AGX reserve. No change to RAM exposure is needed for rollback.
- SDK26100 `km/wdm.h:29767-29775` supplies contiguous-chunk/full-allocation
  flags; the existing Windows allocator validates the resulting MDL. These
  flags do not establish eviction/re-admission of still-live private backing.

VidMm retains placement, eviction/content and budget ownership. KMD retains
physical MDLs, pool accounting, CPU cache policy and ordered reclaim. m1n1
retains stage-2/native-grant/TLBI ownership. Mu retains boot RAM/ACPI exposure.
Initialization, IRQ, DMA protocol, power and recovery behavior are unchanged.
No external implementation was copied. No new live ADT/register/memory-map,
standalone launch or native Windows compilation result is claimed.

## What is still unknown and owner-only checkpoint

The full task remains incomplete: no supported admission boundary has been
established for *every restore of an existing allocation* after returning its
native backing. The remaining work is a real backing-provider or complete
content/cache/restore protocol, with coherent persistent CPU maps and a clean
fallible boundary before accepted work. It cannot be supplied by changing
only segment sizing, reservations and m1n1 coverage. Rc47 readiness is untouched.

The narrow rollback fix needs an owner-run pressure/create-refusal test first:
hash-pin the package, provoke partial backing growth followed by rejection,
verify held slabs return to the prior count, existing allocations restore with
exact bytes, and DWM survives. It does not justify claiming HD/FHD 200k loads.

Once a complete elastic candidate exists, require real physical RAM return on
eviction while allocations stay live, denial of further native allocations,
and a supported successful restore or clean pre-work refusal with exact bytes.
Include persistent CPU maps, private leases, partial placements, queued paging,
shared aliases and uncertain revoke/reset. Then require both HD 1280x720/200k
and FHD 1920x1080/200k to load and finish using the tracked resolution-aware
runner, checking launch JSON and actual app dimensions. Compare FHD against
the task's approximately 5500-point macOS reference at matching settings;
that is an acceptance target, not a predicted score or a result of this fix.

The integration owner preserves exact known-good package/launch/recovery
artifacts, preregisters hashes, collects receipts/dumps/traces first and uses
the established exact-package GPU-visible cleanup. The GPU-hidden image remains
emergency recovery only. No recovery command or control plane was run here.
