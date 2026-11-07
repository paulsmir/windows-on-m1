# T7g — large native BO CPU-access receipts

Offline, 2026-10-23, branch `codex/t7g-cpu-map-evidence`. Receipt-only candidate
for the T7f CPU-access unknown. This does not implement shadow backing or fix
GravityMark's G4 refusal. No hardware, guest, builder, package or firmware work.

## Evidence and ownership

Paths below are relative to `drivers/apple-agx/` unless stated otherwise.

- `render-admission/umd/src/umd_win32_screen.c:1063` invokes LockCb;
  `:1083` records successful callback completion and `:1097` separately records
  successful native pointer publication. Private process staging takes neither
  a VidMm lock nor an unlock. `:1134` calls UnlockCb before clearing Mapped;
  failed unlock leaves the map and active-lock observation intact.
- `render-admission/umd/src/umd_gpuva_windows.c:804` and `:1029` also lock
  staging temporarily for inspection/copies. These calls can occur without
  ScreenMapBuffer and therefore must contribute to lifetime lock counts.
- `mesa/winsys/agx_win32_asahi_scene.c:233` maps the actual resource BO;
  `mesa/winsys/agx_win32_asahi_bo.c:229` releases its retained CPU map at
  disposal. Gallium transfer unmap is not equivalent to this native unmap.
- `mesa/winsys/agx_d3d12_windows.cpp:432` conditionally acquires a persistent
  CPU pointer for a CPU-visible heap; `:442` records its successful handoff.
  The C45 rule leaving CPU-invisible heaps unmapped remains intact.
- `d3d12/agx_d3d12_ddi.cpp:2620` returns the cached native heap pointer;
  `:2646` leaves its underlying map alive on native UnmapHeap. Receipt-only
  calls here distinguish API maps/unmaps from underlying LockCb calls.
- `.local/reference/wdk26100/d3dumddi.h:3480` defines LockCb's allocation
  handle and output pData; `:3494` defines the batch Unlock allocation list.
  `.local/reference/wdk26100/d3d12umddi.h:1820` defines MapHeap's output CPU
  pointer. No ABI, lock flag or result is changed.
- `.local/reference/mesa/src/asahi/lib/agx_bo.c:188` counts retained BO
  `_map` storage, and `:396` can eagerly map newly allocated BOs. The Windows
  adaptation's native map lifetime is the observed boundary, not each CPU load
  or each Gallium transfer. No external implementation is copied.
- `render-admission/umd/src/umd_runtime_device.c:214` rejects more than 16
  receipt words, `:84` exempts measure-* from the startup budget, and `:218`
  admits measure-* in refusals-only mode. The new format fits that contract.

VidMm still owns placement and CPU mappings; UMD owns these CPU observations,
serialization and submission/residency requests; KMD owns backing and native
leaf validation; m1n1/Mu retain their platform/firmware responsibilities. No
initialization, interrupt, DMA, power, launch or recovery contract changes.
T7f's ownership analysis and C75c's direct/evictable behavior are preserved.
The supplied EXP1915–1918 evidence is not independently remeasured here.

## Implemented invariant

Each authoritative screen slot holds saturating lifetime counters. Slot zeroing
starts a new lifetime; owner cookie plus token distinguishes reused handles.
Every successful *actual* LockCb counts even if its pData is unexpectedly NULL;
only a successfully published native map increments Maps. Successful UnlockCb
reduces active locks. API UnmapHeap only reduces API depth and cannot erase
persistent underlying CPU access. An entire successful batch unlock is recorded
before the first fallible native-cache release, including later entries if
publication fails. None of these counters controls driver behavior.

One record is emitted at the first admitted destruction attempt, before handles
or slot state are cleared. This may precede a failed deallocation and is not a
proof of completed destruction. Failure snapshots emit every active slot above
16 MiB (thus every >64 MiB candidate), once per device; D3D12 owner teardown has
a separate once-only snapshot before resources are released. A snapshot is
bounded by the existing 4096-slot table; destruction is at most one record per
slot lifetime. There is no arbitrary top-N cutoff that could hide the refused
large allocation. No receipt is written for each map or frame.

Failure triggers are failed G4 SubmitCommandCb, the D3D12 error-report routes,
explicit hung returns from owner buffer reads/copies, and submit completion.
The owner error recorder accepts DEVICE_REMOVED, DEVICE_HUNG or terminal owner
state. The UMD cannot retrieve Wom1G4SubmitFailure v3 Va through the current
SubmitCommandCb ABI, so the join uses all large ranges offline.

## Exact receipt v1

```
measure-bo-cpu-access hr=0x........ pid=... tid=... session=... qpc=... w0 ... w15
```

All 16 payload words are eight-digit hexadecimal. Every 64-bit pair is **low,
high**, combined as `lo | (hi << 32)`. The normal logger prefix is unchanged.

| Word | Meaning |
|---|---|
| 0 | `1 | (reason << 8)`: 0x101 destruction attempt, 0x201 first failure, 0x301 owner teardown |
| 1–2 | OwnerCookie |
| 3–4 | Slot Token, unique within owner lifetime |
| 5 | Current UMD KMD allocation handle (canonical GPU backing) |
| 6–7 | Latest canonical GPU VA base |
| 8–9 | Logical BO bytes; range is `[base, base + bytes)` |
| 10 | Successful underlying native map publications (not every cached agx_bo_map request) |
| 11 | Successful actual LockCb calls, including temporary staging locks |
| 12 | Successful persistent-pointer handoffs from owner CreateBuffer; cumulative across BO-cache reuse |
| 13 | Successful cached native MapHeap pointer returns |
| 14 | Observed MapHeap depth, decremented by native UnmapHeap; zero does not end the underlying map |
| 15 | Flags below |

Flags: bit 0 Mapped; 1 LockedBase non-NULL; 2 persistent owner pointer handed out
at least once; 3 at least one actual LockCb not yet successfully unlocked;
4 Direct; 5 SystemDirect; 6 Borrowed; 7 PrivateStaging present; 8 Transition;
9 currently associated NativeBo; 10 any counter saturated. Saturation never
wraps to zero. Persistent-ever is bit 2; its underlying map is currently
retained when bit 0 is also set. OwnerPointers is lifetime history, not a count
of currently live D3D12 heap objects.

## Decode and interpretation

Set the existing `APPLE_AGX_UMD_TRACE_FILE` for the workload; measure receipts
also survive `APPLE_AGX_UMD_REFUSALS_ONLY=1`. No new environment switch is needed.
Use the failing process's `reason=2` rows. Match the KMD v3 `Va` with
`base != 0 && base <= Va && Va - base < bytes`. Group by pid, owner cookie and
token; never join only by a recycled allocation handle. Prefer the latest
failure snapshot at/before the relevant removal and use destruction rows for
lifetimes already released. Compare allocation handles only within their
respective process/device handle domains; a KMD object pointer is not this
UMD runtime handle. VA and size are the cross-layer join.

For the matched allocation, Maps>0 proves a successful underlying CPU map;
Locks>0 proves an actual callback succeeded; OwnerPointers>0 proves a cached
persistent pointer was exported; HeapMaps>0 proves a native MapHeap return.
Mapped/LockedBase/active-lock flags describe observed current state. Staged
slots' LockCb counts may refer to their staging allocation, whereas SystemDirect
locks its canonical allocation. This distinction is why the placement flags
must accompany the counts. Zero counters are evidence about these instrumented
paths only, never permission to activate a KMD shadow.

## Verification and remaining gates

The tests execute production C/C++ bodies under ASan/UBSan: failed Lock/Unlock,
S_OK with NULL pData, private staging, temporary locks, repeated maps, nested
MapHeap, persistent unmap, complete batch unlock despite native release failure,
slot reuse, saturation, receipt field order and all large candidates beyond a
top-N-sized set. A two-translation-unit link regression proves that direct C++
and extern-C-wrapped header consumers resolve the same C receipt symbols. Existing narrow replays use a receipt sink where receipt
semantics are separately exercised. The final commands/results and commits are
recorded in the task handoff REPORT.md. Known-failure lists and ledgers remain
unchanged; the integration owner owns hardware/change-ledger rows.

WHAT IS STILL UNKNOWN: GravityMark's actual CPU-access history; Windows ARM64
compilation/hot-swap and trace delivery; cache visibility/coherence; foreign
process and nonnative heap maps; in-progress runtime callbacks at snapshot time;
all sparse aliases/historical remaps (the receipt names the latest canonical
range); true memory placement and eligibility for coherent shadow backing.
No CPU pointer value or memory content is recorded. The counter payload is 28 bytes per
screen slot (112 KiB raw across 4096 slots), plus the per-device snapshot word;
Windows structure padding and final binary footprint were not measured.

Smallest checkpoint, Claude only: rebuild the complete UMD using the changed
internal layout/header, retain/hash the previous UMD, hot-swap ARM64 UMD under
the established EXP1918 contract, then run GravityMark D3D12 to its first G4
refusal/removal. Collect the trace and Wom1G4SubmitFailure v3, join Va to the
large ranges and report Maps/Locks/OwnerPointers/HeapMaps/current flags for the
matched lifetime. Stop with evidence if no row matches; do not infer unmapped.
Restore the previous UMD under the existing rollback contract. No reboot or
KMD/package change is part of this candidate; this offline task performs none
of those operations.

Final offline result: 74 focused tests passed. The unmodified baseline runner
completed with exit 0:

```text
ran 401 tests in 172 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

Three temporary extracted-code mutations (omit MapHeap observation, truncate
failure inventory, omit successful batch-unlock facts) each failed assertions;
the unchanged production replay passed. All four protected invariants and
`git diff --check` passed. No new environment failure was suppressed.
