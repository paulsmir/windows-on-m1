# RC56 native staging implementation plan

**Goal:** remove CPU copy escapes for native ring-backed BOs while preserving
Windows allocation provenance, fences, native geometry and teardown ownership.
**Spec:** `.local/handoff/codex/rc56/TASK.md` (shared handoff).
**Execution:** inline, offline in codex/rc56; no hardware/package/guest activity.

## Contract selected for implementation

Use B: a dedicated original WC MDL with verified 64-KiB chunks, mapped to its
owning process. Register each slice as ordinary ExistingSysMem storage supported
only in the aperture/system segment. VidMm still owns GPUVA/residency; the KMD
owns the original pinned physical pages. Existing logical/native PTE checks
remain mandatory. No firmware, segment-capability or stream-validator change.

Microsoft's process-notify callback runs in the last exiting thread before
address-space deletion. It removes CPU views; graph destruction and allocation
destruction separately retire GPU and WDDM ownership. A referenced EPROCESS
identifies the owner. A mapping-only mutex serializes foreign attach/unmap with
the exit callback. This composition is an engineering inference from documented
primitives, independently reviewed and exercised by host interleaving tests;
Windows memory-manager/Verifier stress remains a hardware gate.

Primary source inventory: WDK d3dkmddi.h:3804,3913,3990 (ExistingSysMem and
allocation ABI), d3dukmdt.h:419 (pSystemMem); winsdk km/wdm.h:29624,29696,29832
(mapping/unmapping/allocation); Asahi gem.rs:160,195 and agx_blit.c:617 (native
pages, CPU cache policy and GPU copies); saved m1n1 075e3980
hv_agx_retained_platform.c:50 and hv_agx_gpuva_v5.c:459 (native raw-RAM grants);
saved Mu 361a2134 MemoryInitPeiLib.c (RAM/reserve publication). The supplied
EXP1974/1975 observations are reference evidence; no live state is inspected.

Official sources:
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/ntddk/nc-ntddk-pcreate_process_notify_routine
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-mmmaplockedpagesspecifycache
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/wdm/nf-wdm-mmunmaplockedpages
- https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfoflags_wddm2_0
- https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-segments
- https://learn.microsoft.com/en-us/windows-hardware/drivers/display/allocation-usage-tracking

Host aperture is not the selected mechanism: SupportsCpuHostAperture excludes
CpuVisible and retains aperture fallback requirements. ExistingSysMem supplies
the backing rather than trying to make ordinary scattered system pages native.
Linux GEM lifetime is not a Windows process-rundown contract. Mu/m1n1 continue
to own RAM publication, hardware initialization, power, interrupts and grant/TLB
enforcement. Mesa retains copy/hazard ownership; UMD retains CPU access/fences.

## Steps and review focus

- [x] Establish a mapping/teardown contract, including forced foreign
  teardown, before publishing a production user pointer.
- [x] Add executed RED tests for mapping authorization, geometry, zeroing,
  partial failure, process exit, GPU uncertainty and aggregate charged budget.
- [x] Implement KMD native mapping owner and versioned buffered ABI. Refuse
  unknown fields, foreign/stale identity and invalid ranges; no kernel pointers
  returned. Retain pages until user views, WDDM references and GPU grants end.
- [x] Register native slices as ExistingSysMem allocations; preserve ordinary
  supported sets and full PTE checks for every other allocation. Verify actual
  backing against the authenticated MDL, not merely a UMD pointer claim.
- [x] Integrate ring lifetime with UMD BO holds and real completion fences;
  route existing GPU upload/readback/query consumers through that storage.
  Keep an explicit receipt for capacity/layout/map fallback to current copies.
- [x] Targeted regressions, full `cd tests && python3 run_against_baseline.py`
  until BASELINE_OK, independent review, small commits, final REPORT.md.

Highest-risk cases: foreign-context destruction racing last-thread exit;
failed TDR with a live user view; alternate pSystemMem despite a valid token;
ring reuse with outstanding GPU/CPU references; allocation-failure rollback
without double charging or freeing another owner's pages. Tests must cover
these with production code and explicit OS-boundary limitations.

Smallest future checkpoint (integration only): exact CPU upload -> GPU copy ->
CPU readback, wrap and two outstanding batches, abrupt exit/foreign teardown,
TDR, capacity refusal and normal reclaim. Only then compare HD 50k (1632/9.8)
and FHD 100k (1275/7.6), with macOS FHD 100k 7700 as the supplied comparison.
No matching HD 50k macOS reference was supplied. Recovery stays the existing
hash-pinned GPU-visible exact-package procedure. No hardware is preregistered
or run by this plan.

## Delivered mechanism and evidence

Paths in this section are under `drivers/apple-agx/`. No external implementation
was copied. The native arena is dedicated Windows RAM with native PFN geometry;
it is not an unowned offset stolen from VidMm's segment 3.

| Source | Contract implemented or retained |
| --- | --- |
| `render-admission/src/native_staging_windows.c:148,165` | Original WC MDL, full allocation and every 64-KiB chunk verified before UserMode mapping. The allocation omits DONT_ZERO; the API guarantees zeroed initial pages. |
| Same file `:62,87,362,413` | Original-process exit unmap and serialized foreign attach/unmap; independent CPU, WDDM-binding and graph ownership. Normal process destruction releases pages only after all three end. |
| `render-admission/src/gpuva_g3_windows.c:2524` | Exact buffered request, matching live device/context/process, process-reference overflow refusal. Map/Cancel holds NativeMapRefs through manager work; closing and successful reply publication are serialized under the G3 lock. |
| Same file `:2590,2621,2672` | Process closing prevents new map admission. CPU unmap occurs outside the G3 lock, including when GPU teardown later refuses. Process free requires zero native operations; graph acknowledgement precedes physical release. |
| `render-admission/include/render_win32_transport.h:13,32,37`; `src/render_win32_transport.c:45` | Version 1 mapping/cancel envelope and extended version 3 allocation. Fixed-width fields; nonzero token, 64-KiB range, no overflow/reserved/shareable intent. |
| `render-admission/src/allocation_windows.c:613` | Bind token/range to referenced owner before publishing allocation. ExistingSysMem=1, CpuVisible=1, AccessedPhysically=0, aperture-only support; no duplicate ordinary native-pool reservation. |
| Same file `:726,959`; `src/gpuva_g3_paging_windows.c:686` | Binding survives allocation lifetime; foreign opens rejected. Actual PTE PFNs must match the original MDL at the allocation offset before any native leaf is published. |
| `render-admission/src/pool_windows.c:615,496` | Low-memory growth policy and aggregate charge apply to dedicated MDLs. Receipt v10 adds charged native bytes and total promise; ordinary min(live,cap)+private slab promise remains unchanged. |
| `render-admission/umd/include/umd_staging_ring.h:190,205` | BO-owned slices survive repeated submissions. FIFO reclaim requires final BO release and actual last-use completion, not a synthetic ticket fence. |
| `render-admission/umd/src/umd_win32_screen.c:747,966,986,1073` | Lazy arenas, WOW64 range validation, slot Transition before unlocked mapping, zero-on-reuse, and retained slot ownership through failed-allocation rollback. |
| Same file `:1459,1681,1728`; `src/umd_gpuva_windows.c:2176` | CPU reads and final slice release wait the real render timeline; native maps do not call runtime Lock/Unlock or report fictitious locks. |
| `render-admission/umd/src/umd_gpuva_windows.c:1505` | SystemDirect native slots bypass the per-copy escape dispatcher; WC ordering barriers remain at submission/CPU-return boundaries. Ordinary residency/readiness and scheduler completion are retained. |
| `mesa/winsys/agx_d3d12_windows.cpp:895,2013,2376,2383,2451,2500` | Existing GPU buffer copies, query result-resource writes, and texture staging use the ordinary BOs now backed by native slices. No recursive Gallium submission is introduced inside the transport callback. |

The mapping request is 32 bytes (`Magic,Version,Bytes,Capacity,Token,UserVa`).
Map input has nonzero 64-KiB-multiple Capacity and zero Token/UserVa; cancel
input has Capacity=0, a nonzero Token and zero UserVa. Cancel can promptly free
only a never-bound ring. The extended allocation is 88 bytes: the existing
72-byte declaration plus Token/Offset. The runtime receives the user address
separately as pSystemMem, which is why exact PTE authentication is necessary.

The documented [ExistingSysMem contract](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfoflags_wddm2_0)
accepts page-aligned preallocated user memory and WC with Cached=0. Original
MDL ownership keeps those physical pages fixed; aperture-only supported
placement excludes a migrated local copy. [GPU segments](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpu-segments)
defines that system-memory placement and its ordinary per-process page tables.
Actual Windows admission/coherence is still a validation gate, not established
by a host fixture.

RC62 (EXP1984) replaced the original capacity rule of four 128-MiB arenas
(512 MiB). Every native BO is aperture-only, so all of them must fit in the
256 MiB aperture CommitLimit (NON_LOCAL budget group) beside every other
aperture placement. With three 128 MiB arenas mapped, a 128 MiB native heap
failed MakeResident with E_OUTOFMEMORY even after CantTrimFurther. The rule now:
the KMD charge is capped at a quarter of the aperture CommitLimit, at most
64 MiB, across all devices and processes (`pool_windows.c`
AdmissionNativeStagingCap). A UMD device maps arenas of 4, 8, 16 and 32 MiB by
demand, always the smallest unmapped arena that fits the request
(`umd_native_arena.h`). BOs over 16 MiB keep the ordinary path (fallback
reason 4). Each arena has a first-fit table of 256 owned slices, so a slice
released after its last-use fence is reusable while older slices stay live.
Metadata exhaustion, oversized BOs, low-memory refusal or runtime admission
failure fall back. A clean failed allocation releases its slice; failed handle
cleanup retains its ticket and terminal owner. Arenas remain charged for the
KMD-process graph lifetime, including after a UMD device closes; this is an
explicit bounded memory/performance tradeoff, not an eviction-capable arena.

TDR does not itself remove CPU views or permit physical page reuse. Failed
graph retirement retains backing. Normal Stop refuses live processes/rings
before tearing down the pool. Final Unload always removes its registered
process callback; an unexpected live owner loses its CPU view and process
reference but retains uncertain physical pages/metadata/quota in terminal
containment. That abnormal path is not normal reclamation.

`APPLE_AGX_D3D12_NATIVE_STAGING=0` opts out for comparisons. Otherwise the
existing HeapStaging D3D12 owner tries the native arena first. The existing
CPU_POOL_MAP setting still selects the ordinary fallback policy. D3D11,
borrowed/shared resources, GPU-only allocations, firmware and caps retain
their existing paths. No stream/submit validator was changed.

Receipts: `native-ring-map` records mapping status/capacity/token;
`native-ring-allocation` records token/ticket/offset/bytes;
`native-ring-use` records referenced native capacity per transfer phase, **not
dirty bytes**; `native-ring-fallback` reason 1 means arena unavailable/full, reason 4
means the BO is over the 16 MiB native slice limit, reason 3 means a refused
arena mapping and reason 2 means clean ExistingSysMem allocation refusal,
with requested bytes, aggregate occupancy/capacity and HRESULT. Pool receipt
v10 and its decoder separate NativeStagingBytes and TotalPromisedBytes while
preserving v7-v9 prefixes.
NativeStagingBytes includes an in-progress allocation charge. The decoder's
total_held_bytes adds that charge to held slab bytes, so it is an upper bound
on physical holdings during preparation, not a separate physical census.

## Verification and remaining hardware gate

The final targeted run executes 84 tests. Host manager tests compile production
code as C11 and C++17 with ASan/UBSan and concurrent exit/unmap interleavings;
arena helpers run 20 cases at each of native and simulated WOW64 pointer widths.
Review-driven RED/GREEN covers the pool-uncharge lifetime, initial creation and
rollback close guards, native-map lock accounting, process-operation reference,
and closing/reply publication. The final REPORT.md carries the exact full
baseline result, commits and hashes. No known-failure entry is changed.

Local Clang with cached Windows headers syntax-checks all three modified UMD
translation units for i686, x64 and ARM64. i686 emitted LLVM IR names the new
wait entry with C linkage. Native manager and pool translation units pass the
ARM64 WDK-header syntax check. Wider KMD units have the same pre-existing
header/profile errors on base and candidate (page-size enum, native-fence field,
integer-limit macros); no guessed interface flags or source fixes hide them.
This is not a native MSVC/WDK package build or link.

Required command, final exit 0:

```text
cd tests && python3 run_against_baseline.py
ran 663 tests in 279 modules; 6 failing, 6 known, 0 NEW, 0 FIXED
BASELINE_OK
```

The six failures are the unchanged known baseline; zero new failures is the
prescribed gate, not an assertion that every repository test passes.

The expected software effect is zero copy escapes for ring-backed BOs. Overall
copy_escape approaching zero depends on measured ring coverage and fallback.
The supplied 46 ms of 89 ms (HD) and 58 ms of 115 ms (FHD) are opportunity
bounds, not predicted frame-time reductions: WC reads/writes, zeroing, startup
allocation cost, retained memory, ring pressure and GPU copies remain real costs.

Integration must first compile/link the matched ARM64/x64/x86 package, then
check exact CPU upload -> GPU copy/query store -> CPU readback bytes, both native
chunks and wrap, two in-flight batches, stale/foreign/mismatched-PFN refusal,
failed allocation cleanup, four-arena pressure, abrupt exit and foreign close,
TDR success/failure and final page return. Use Driver Verifier for the MM
locking composition. Only afterward run HD 50k against 1632/9.8 FPS and FHD
100k against 1275/7.6 FPS; supplied macOS FHD 100k=7700 and FHD 200k=5500 are
comparison references, not predictions. No matched macOS HD 50k score is known.
