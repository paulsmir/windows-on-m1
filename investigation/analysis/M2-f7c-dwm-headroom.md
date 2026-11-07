# M2-f7c: protect system headroom at segment-3 admission

2026-10-23; T8; software-only candidate, not hardware validated.

## Evidence and decision

The T8 handoff reports EXP1911: DWM stopped with `0xc00001ad`
(STATUS_FATAL_MEMORY_EXHAUSTION) while GravityMark D3D12 filled a 2688 MiB
pool on a guest reporting 5988 MiB RAM. PRESSURE survived EXP1908–1911,
refusing at 347–390 MiB available. These are supplied observations, not a
new measurement or proof that physical-page pressure alone caused the crash.

WHY THIS HYPOTHESIS:

1. C73 changed admission to accept `LowMemoryCondition == 0` even when
   `HighMemoryCondition == 0`. The prior high gate refused earlier;
   `M2-f7-memory-floor.md:66` records EXP1907's allocation refusal and the
   approximately 740 MiB available-memory observation. The interval between
   those event thresholds was deliberately opened to additional locked pages.
2. The driver obtains nonpaged physical backing in 2 MiB slabs
   (`pool_windows.c:262`, `:283`); it cannot count on ordinary working-set
   trimming to return those pages. Idle demand granules previously waited
   2000 ms, with a 500 ms reclaim timer (`pool_windows.c:208`).
3. The current segment cap already leaves more than half of this guest's RAM
   outside segment 3 (`render_pool_demand.c:250`). A new `RAM - 1 GiB` cap
   would not bind before the reported failure. Global free-memory pressure is
   causally closer than another capacity constant.

Decision: require the documented high-memory signal for **new reservation
slabs**, and reclaim safe idle surplus without the idle delay while that
headroom condition is absent. Keep segment sizes, budget groups, firmware,
DDI capabilities, signing and recovery unchanged. This deliberately prioritizes
system headroom over the extra allocation reach introduced by C73.

All unqualified C file paths below are under
`drivers/apple-agx/render-admission/src/`.

## WINDOWS CONTRACT

- WDK 26100 `.local/reference/wdk26100/d3dkmddi.h:2605` defines
  `DXGK_SEGMENTDESCRIPTOR`: Size, CommitLimit and segment flags. For a memory
  segment CommitLimit equals Size; a separate lower commit limit is an
  aperture mechanism. Segment 3 cannot be given a smaller CommitLimit while
  keeping its size as if it were an aperture. [DXGK_SEGMENTDESCRIPTOR](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_segmentdescriptor)
- VidMm's graphics allowance is
  `min(80% RAM, max(RAM - 16 GiB, 50% RAM))`. Dedicated system memory counts
  against it; shared memory is limited by the remaining allowance and
  aperture limits. These accounting limits leave system memory outside the
  graphics allowance but are not a live available-memory reserve.
  [Calculating Graphics Memory](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/calculating-graphics-memory)
- SDK `.local/reference/winsdk26100/shared/dxgi1_4.h:1303` defines Budget,
  CurrentUsage, AvailableForReservation and CurrentReservation. Budget is an
  OS-selected per-application target, not a fixed segment size or a guarantee
  that another application cannot consume RAM. Reservation is a minimum
  working-set hint. [DXGI_QUERY_VIDEO_MEMORY_INFO](https://learn.microsoft.com/en-us/windows/win32/api/dxgi1_4/ns-dxgi1_4-dxgi_query_video_memory_info)
- On UMA, L0 and LOCAL refer to the same system-memory pool. This driver puts
  memory segments 2 and 3 in LOCAL and the aperture in NON_LOCAL
  (`memory_windows.c:14`). [D3D12_MEMORY_POOL](https://learn.microsoft.com/en-us/windows/win32/api/d3d12/ne-d3d12-d3d12_memory_pool)
- `MmAllocatePagesForMdlEx` returns nonpaged physical pages. FULLY_REQUIRED
  makes a short allocation fail; it does not reserve headroom for Windows.
  The caller must eventually free its MDL pages. Here that owner is the KMD,
  not a user-mode Budget query. [MmAllocatePagesForMdlEx](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-mmallocatepagesformdlex)
- HighMemoryCondition signals free physical memory above an OS threshold;
  LowMemoryCondition signals below another OS threshold. The former is a
  documented signal for aggressive allocation, the latter for returning
  unused memory. Both remain signaled while their conditions hold. This is
  a documented driver mechanism, not a numerical free-page API or a promise
  about the observed 740 MiB value. [Standard Event Objects](https://learn.microsoft.com/en-us/windows-hardware/drivers/kernel/standard-event-objects)
- `KeReadStateEvent` permits DISPATCH_LEVEL or lower (SDK `km/wdm.h:23200`);
  this code samples under the existing fast mutex at APC_LEVEL. The RAM-range
  query (`km/ntddk.h:10735`, `pool_windows.c:646`) describes managed physical
  ranges, not currently available pages. No documented numerical available-
  page query was established in this review; no private kernel counter or
  undocumented SystemInformation class is introduced.

Thus segment reporting should respect the graphics allowance, and the owner
of extra locked backing must also respond to system pressure. The high gate
is this driver's conservative policy, not a Windows mandate for every UMA KMD.
The M2-f7b statement that a real UMA driver needs no gate is not a sufficient
contract for this driver's separately reserved nonpaged backing.

## AGX/ASAHI CONTRACT and ownership

- Asahi reference `.local/reference/mesa/src/asahi/lib/agx_bo.c:355` tries its
  cache, allocation, cache wait, cache eviction, then allocation again; it
  returns NULL if still unsuccessful. Its idle cache ages BOs (`:81`). This
  does not specify Windows kernel headroom and is not a reason to keep a KMD
  physical-page cache under pressure. No external implementation was copied.
- KMD owns slab allocation/free, lifetime reservations, 64 KiB granules and
  reclaim. `render_pool_slab.c:22` promises backing up to
  `min(Reserved, ReserveCap)`; `:28` frees only an empty slab whose removal
  leaves those promises covered. `render_pool_demand.c:190` checks ownership,
  pins, paging refs and executed sequence before calling release.
- m1n1 pinned revision `21c3648d32423def46086e258e0995c737dc9284`,
  `src/hv_agx_pool_window.c:119`, refuses unmap while the broker holds a page;
  `:186` checks range/alignment. KMD `pool_windows.c:361` preserves that BUSY
  result and frees slab slots only after successful unmap. Hypervisor owns
  stage-2 aliasing/broker access safety, not Windows free-memory policy.
- Mu pinned revision `361a2134b2f742f331f8d75ae5f60ad4f7f413f1`,
  `Silicon/Apple/T810XFamilyPkg/Library/MemoryInitPeiLib/MemoryInitPeiLib.c:498`
  validates and reserves AGX local DDR (`:513`). That firmware carveout is
  segment 2; `memory_windows.c:104` distinguishes it from segment 3's
  PopulatedFromSystemMemory backing. No memory map, ACPI, ExitBootServices,
  initialization, interrupt, DMA, power or firmware recovery behavior changes.
- Live ADT/register/IRQ state was not queried: hardware and guest access are
  prohibited for T8. Source was read from this worktree, reference files and
  exact objects in the shared Git database, not another worktree's files.
  The supplied EXP1911 boundary is the hardware reference for this change;
  it does not authorize a new launch or establish a new launch contract.

## TRANSLATION and deterministic invariant

`pool_windows.c:392` permits new reservation backing exactly when the high
signal exists and is set, and a present low signal is clear. Missing high
fails closed; missing low does not override a known high signal. The existing
slab loop checks permission before **each** slab (`render_pool_slab.c:79`).
Failure rolls back the request, increments ReserveFailures/FloorRefusals,
and propagates STATUS_NO_MEMORY from `allocation_windows.c:525`.

Pressure reclaim (`pool_windows.c:401`) bypasses only ReleaseDelay and the
empty-spare cache. It runs from periodic reclaim, executed-record retirement,
and after a reservation attempt. Reservation runs first so already-held
pages can cover it without being discarded prematurely. Reclaim then removes
only surplus; zero-count unreserve trims already-empty slabs even when no
candidate granules remain. All of this stays under the existing pool mutex.
Pressure is sampled at these existing triggers, not by a new event-wait
thread. The existing timer remains 500 ms when demand candidates exist. When
high headroom returns, normal aging and the one-empty-slab cache resume.

The invariant is **no new reservation slab while the sampled high headroom
signal is absent; at a reclaim trigger, pressure does not retain safely reclaimable surplus
merely for aging/cache reasons; live promises and reachability always win**.
The paging `SlabTake` path remains ungated, including overlap growth: changing
that contract can turn a creation refusal into process/device failure.

HeldGranules in `pool_windows.c:455` counts *all physical slab slots*, including
committed demand granules. Do not add it to CommittedGranules as though these
were disjoint. Immediate logical reclaim also need not release physical RAM:
reservation promises or live granules in the same slab may keep it allocated.

Alternatives considered:

| Mechanism | Decision |
| --- | --- |
| `RamBytes - reserve` physical cap | Existing cap is already stricter for ordinary fixed reserves on this guest; cannot track other consumers. |
| Numeric available-pages floor | No supported query established; use documented events. |
| Smaller segment 3 | Would lower advertised capacity but requires choosing an unproven new size; does not track system pressure or directly force release of pinned backing. |
| Low event alone | C73 policy; reported DWM failure motivates rejecting it for aggressive slab growth. |
| High event plus safe immediate reclaim | Chosen; preserves the existing allocation/paging contract and is testable without a new ABI. |

## Offline regression and WHAT IS STILL UNKNOWN

`tests/test_apple_agx_pool_headroom.py:27` compiles actual extracted Windows
policy/wrappers with the real portable demand/slab implementations, using
ASan/UBSan. `tests/pool_headroom_test.c` (under render-admission) exercises
missing/set/clear events, refusal counters, recovery, reuse without growth,
pressure reclamation before aging, zero-candidate spare removal, reservation
promises, pinned/owned/paging/unexecuted/busy pages, and admitted paging.
RED: original event policy failed; after that fix, delayed reclaim failed.
A further RED caught discarding a reusable spare before reservation. GREEN:
all cases pass. Existing slab and demand tests also pass.

Unknowns and limits:

- This is a mitigation with an executable software invariant, not proof that
  DWM can never exhaust memory. Other consumers can allocate concurrently;
  event sampling and allocation are not atomic system-wide. Already admitted
  paging can still grow. Physical headroom does not guarantee commit headroom.
- The high threshold is OS-selected. Its size and hysteresis are not inferred
  from the 740 MiB observation. Event-open failures now refuse new growth.
- EXP1907's earlier GravityMark refusal may recur. Removing C73's extra reach
  is intentional; whether cache reclaim avoids device removal is unknown.
  A failure confined to the requesting application is preferable to desktop
  starvation, but is not a claim of no application regression.
- Busy/pinned/promised/fragmented backing can prevent release of the reported
  196–278 MiB idle/held excess. The fixture cannot predict the real peak.
- Reclaim performs the existing broker unmaps while holding the pool mutex.
  Its pressure-time latency and effect on frame pacing require measurement.
- Host C tests do not replace a WDK build or the prescribed hardware run.

## Checkpoint (for Claude; not run by T8)

On one hash-pinned package with only this policy change and unchanged firmware,
repeat GravityMark D3D12 at the same settings through its prior failure window,
then PRESSURE and the normal smoke/corpus probes. Record available physical
memory, commit/pagefile usage, LOCAL Budget/CurrentUsage, event states, pool
Reserved/Committed/Held/FreedSlabs and ReserveFailures/NewSlabFailures, DWM PID
and WER/application/device-removal events. Use existing receipts; no new ABI.

Pass: DWM remains alive without c00001ad, desktop/SSH remain responsive,
PRESSURE refuses safely, existing probes pass, and GravityMark either proceeds
or reports a bounded application allocation failure. Separately report whether
its duration/output regresses; desktop survival alone is not "no app regression".
Reject if desktop starvation continues, paging promises fail, or new corruption
appears. If safe refusal still causes a device removal, inspect the application/
UMD error path rather than reopening the low-memory interval without evidence.
Claude owns preregistration, known-good recovery preservation and exact-package
GPU-visible rollback; use the experiment's ordinary recovery contract. This
branch runs no package build, install, boot, recovery or guest operation.
