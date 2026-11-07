# C75b: large direct allocations and the CPU tiling crash

2026-10-23; offline, branch `codex/t6-large-direct-mapping`, base
`5f24eb4a6e1c7c2d3274fed5e89a578a81df1b9f`.

**Verdict:** no 16 MiB CPU-window truncation was found. EXP1909's archived
D3D12 logs show residency OOM with zero slots trimmed before a terminal backend
fault. A failed BO map can then reach the CPU tiler unchecked. That crash path
is reproducible offline and is fixed here. The crash register values are absent,
so this is a supported explanation, not a proven reconstruction of the original
faulting address. C75 remains reverted: this patch does not make large buffers
zero-copy or establish that doing so is safe under pressure.

## Evidence and decision

Evidence root: `.local/experiments/EXP1909-ib-direct/evidence/EXP1909/`.

- `gm1909b-umd.log:7883` onward contains repeated `reject-make-resident`
  `hr=0x8007000e`. The final request at line 7901 reports count 3, rounds 1,
  trimmed slots 0, trimmed bytes 0, `NumBytesToTrim=0x1adf3a`. Lines 7902-7904
  record batch refusal and the first backend fault. `gm1909b-done.txt` records
  the application AV at RVA `0x5d998`. Run A also ends with residency OOM and a
  backend fault. These are application failures, not evidence of a 16 MiB
  mapping boundary.
- `umd/src/umd_gpuva_windows.c:331` decodes those receipt fields and stops
  retrying when no slot can be trimmed. `umd/include/umd_residency_trim.h:29`
  excludes **every** SystemDirect slot. Its comment documents why: re-residency
  can place CPU-visible BOs into 4 KiB system pages unsuitable for the UAT.
  C75 converts the large formerly trimmable GPU-local BOs to this category.
- `mesa/winsys/agx_win32_asahi_bo.c:846` returns without a map when the backend
  is failed; failed native maps also latch failure. Pinned Mesa
  `src/gallium/drivers/asahi/agx_state.h:958` added a mip/layer offset to that
  possibly NULL map. `agx_pipe.c:1048` passed it to `ail_tile` without checking.
- Offline disassembly of the supplied `UmdArm64-1909.dll` confirms
  `str q0, [x20, w6, uxtw #4]` at RVA `0x5d998`, with Morton masks in its loop.
  Pinned Mesa `src/asahi/layout/tiling.cc:87` through line 104 performs the
  corresponding destination store for 128-bit elements. Neither the DLL nor
  the WER text supplies x20, w6, the exception address, or the BO identity.

Paths below beginning `src/`, `umd/`, or `include/` are relative to
`drivers/apple-agx/render-admission/`, unless explicitly marked Mesa/m1n1/Mu.
Primary Mesa and WDK files are the local `.local/reference/mesa` and
`.local/reference/wdk26100` copies. No online documentation was fetched.

## WINDOWS CONTRACT

1. **Creation:** `umd/src/umd_win32_screen.c:560` describes a one-row A8 byte
   allocation with CPU visibility. Line 572 chooses SystemDirect only through
   the retained 16 MiB policy. The non-direct branch changes the version/type
   to GPU-local. The runtime receives `pfnAllocateCb` at line 614. A direct
   slot has no private staging copy (line 624).
2. **KMD admission:** `src/render_win32_transport.c:70` validates version,
   CPU visibility, shape and class size. The General class can exceed the
   legacy bound using `AdmissionGeneralMaximumBytes` (`allocation_windows.c:81`).
   `allocation_windows.c:517` reserves the allocation's rounded size in the
   pool and selects segment 3, with aperture fallback only for CPU-visible
   allocations. Line 544 sets `FlagsWddm2.CpuVisible`; line 556 makes class BOs
   GPUVA allocations, not physically accessed allocations. Reservation buys
   capacity; it is not a placement or a CPU mapping.
3. **Segment exposure:** `lifecycle.c:880` routes QUERYSEGMENT4;
   `memory_windows.c:21` translates topology flags. Lines 186-196 publish the
   pool's full size and `CpuTranslatedAddress=pool.GuestIpaAddress`.
   QUERYSEGMENT5 has the equivalent at lines 113-121. The pool is CpuVisible
   and PopulatedFromSystemMemory. Segment 2 is firmware-reserved DDR and uses
   PopulatedByReservedDDRByFirmware instead (lines 183-184).
4. **WDK ABI:** `d3dkmddi.h:2559` defines DXGK_SEGMENTFLAGS;
   `d3dkmddi.h:2605` defines the GPU base, CPU translated base and size;
   `d3dkmddi.h:2720` defines the descriptor actually used by QUERYSEGMENT4.
   `d3dkmddi.h:3800` defines the WDDM2 allocation flags. These have no 16 MiB
   CPU mapping-size field. Prior inspected Microsoft segment documentation
   is recorded in `M2-f3-demand-pool.md:12`: CPU-visible memory segments are
   mapped through their CPU translated address; FillVirtual/TransferVirtual
   move or initialize their contents. This note does not claim to have freshly
   verified uncached Learn prose while offline.
5. **Lock:** `umd_win32_screen.c:924` prepares the map, calls `pfnLockCb`
   with `LockEntire=1` and zero page-list fields (lines 971-987), rejects failed
   HRESULT/NULL (line 1001), then adds the caller's byte Offset exactly once
   (line 1016). The CPU handle selector at line 20 uses KernelAllocation for
   SystemDirect. WDK `d3dumddi.h:3480` describes D3DDDICB_LOCK and its output
   pointer. `winsdk26100/shared/d3dukmdt.h:544` defines LockEntire. There is no
   `DxgkDdiLock` entry in this pinned WDDM interface or this driver's callback
   table. The related KMD callback is AcquireSwizzlingRange
   (`d3dkmddi.h:3707`), which this driver does not register. A proposed new
   KMD Lock handler would therefore not be an implementation of this path.

The required end-to-end invariant is a usable full CPU view before returning a
successful map. The offline pool tests prove the narrower full-placement
backing invariant. They cannot prove VidMm's ordering of Lock, zero-fill and
placement callbacks. `M2-f3-demand-pool.md` already records that ordering gap.

## AGX/ASAHI CONTRACT

- The native winsys maps the entire BO (`agx_win32_native_bo.c:37`), and can
  report failure. Mesa's texture transfer uses a linear temporary then tiles
  into the real BO on unmap (`agx_pipe.c:986`, `:1030`). A void unmap callback
  must record a context fault if writeback fails; it cannot return a pointer
  or HRESULT to the caller.
- `pool_windows.c:27` bounds a legacy MAP_APPEND command to 16 MiB. The loop
  at line 179 advances through **all** MDL frames and checks the full mapped
  size at line 199. This constant is not a BO mapping cap.
- The active demand path uses 64 KiB granules and up to 1024 granules per
  commit run (`pool_windows.c:259`, `:324`), mapping physical slab runs with
  MAP_AT. At line 719 it maps the **full segment** CPU IPA window, not 16 MiB.
- `gpuva_g3_paging_windows.c:446` derives placement base from segment address
  minus AllocationOffset; line 493 places the allocation's full aligned size.
  `render_pool_demand.c:73` commits every granule before publishing ownership,
  splitting runs at MaxRun. Invalid/unidentifiable placement falls back to
  pinning the referenced granules, so this is not an unconditional guarantee
  for arbitrary fragmented placement. A successful full placement retains its
  tail even if only an early PTE was supplied.
- PTE placement precedes leaf publication (`gpuva_g3_paging_windows.c:605`).
  Paging-process PTEs hold references (line 614); FillVirtual and
  TransferVirtual resolve them (line 912) and preserve backing through record
  execution (line 1042). The pool CPU copy address is full-window base plus
  validated segment offset (line 1078).
- Pinned m1n1 `21c3648d`: `src/hv_agx_pool_window.c:167` bounds MAP_AT by window
  capacity, not 16 MiB. `src/hv_vm.c:407` prepares its page tables; line 426
  maps individual stage-2 pages. An absent stage-2 entry takes the
  `HV: Unmapped IPA` failure at line 1265. That differs from a user-mode
  unmapped VA; the supplied application AV is insufficient to prove this
  hypervisor failure occurred. EXP1909 used a newer experiment m1n1 per the
  handoff; this pinned-source inspection is not an assertion of binary identity.
- Pinned Mu `361a2134`: T810X MemoryInitPeiLib.c:513 reserves the
  receipt-sized firmware region and builds its reserved-memory HOB. This owns
  segment 2's carve-out; the demand-backed Windows segment 3 is a KMD runtime
  allocation. No firmware, ACPI, interrupt, DMA or power change is warranted
  by the evidence here.

## TRANSLATION

Ownership remains: VidMm chooses residency/placement and constructs user CPU
views; KMD reserves/commits/reclaims segment-3 pages; m1n1 owns the IPA aliases;
Mu owns the separate firmware reserve; the Windows Mesa adaptation owns map
failure propagation into Gallium. Hardware initialization, runtime interrupts,
DMA authorization, power and recovery remain with their existing platform/KMD
owners. This patch changes only the last of those CPU failure boundaries.

The smallest deterministic fix is in the existing Windows Mesa source
projector (`mesa/scripts/build-native-asahi-state.py:6274`):

- Preserve NULL before adding the texture's mip/layer offset.
- Before CPU tiling on unmap, check the resulting pointer. On failure, mark
  the context fault and branch to the existing transfer cleanup, skipping the
  data-valid update. Successful writeback is unchanged.
- Retain source uniqueness checks and overlay SHA-256 manifests. Only derived
  Windows build inputs change; external Mesa files and their MIT notices stay
  intact. No external implementation is vendored into this repository.

`tests/test_umd_texture_map_failure.py` compiles the actual projected helper
and unmap bodies with ASan/UBSan. The old code fails at the tiler's destination
assertion. The fixed code covers failed first map, nonzero mip/layer offset,
later-layer failure, cleanup, validity, and successful two-layer writeback.

`render_pool_demand_test.c` additionally exercises full placements of 16 MiB +
64 KiB, 25 MiB, 64 MiB, and 64 MiB + 64 KiB at a nonzero base, plus failure in
the second commit run. These tests pass with the existing pool implementation:
they are invariant coverage, not evidence that a new KMD fix was needed.

Do not reapply C75 yet. Widening SystemDirect also removes large BOs from the
trim set. The zero-copy policy needs a supported residency/eviction contract;
simply allowing SystemDirect trimming would reopen the known 4 KiB system-page
fallback problem. A CPU null guard alone does not solve OOM or that contract.

## WHAT IS STILL UNKNOWN

- Original crash x20/w6, effective address, BO size/identity and mip/layer.
  The supplied DLL establishes the instruction, not a fault offset within a BO.
- Whether the last CPU map was refused because the backend had failed, LockCb
  failed, or a valid mapping became unusable. Existing lock receipts retain
  only handle-present/pointer-present booleans, and can be rate-limited.
- Exact VidMm Lock/placement ordering, fragmented placement, and aperture
  fallback behavior for large CPU-visible allocations under pressure.
- Which live allocations consumed the untrimmable budget. Both archived
  D3D12 runs show zero slots trimmed, but the logs do not contain a full
  allocation inventory at the failure.
- Windows native compilation and hardware behavior of this guard. Offline
  sanitizer tests do not validate WDDM or GPU performance.
- Read-side detiling and other callers' failure propagation are outside this
  writeback fix; this is not a claim that all Mesa CPU-map failures are handled.

## Checkpoint

WHY THIS HYPOTHESIS:

1. EXP1909B reports OOM, zero trimmed slots, then a terminal backend fault.
2. SystemDirect excludes large BOs from trim; the failed backend can refuse a
   new map, and the CPU tiler previously consumed that failure unchecked.
3. Actual projected code reproduces that unchecked destination offline;
   the full-placement pool tests do not reproduce a 16 MiB cutoff.

Integration owner only; no run is authorized or performed by this task.
First, retain the normal direct threshold and run the regression/pressure path
with the guard: a forced or natural failed map must produce device/context
failure without a process AV, invalid-level publication or transfer leak.
Capture the first fault and lock result before cleanup. Preserve the accepted
package and use the existing GPU-visible exact-package rollback.

Before another broad C75 run, use a fresh-device direct-allocation probe at
16 MiB + 64 KiB, 25 MiB, 64 MiB and 64 MiB + 64 KiB. Write/read every page,
including tails, before GPU use and after submission. Record Lock HRESULT,
pointer span, BO identity, placement, pool receipts and fault registers.
This separates a mapping defect from aggregate budget exhaustion. Only after
residency/pressure handling is established should GravityMark D3D12/D3D11
test XFER approximately zero, all regressions and PRESSURE. A no-crash result
that merely reports OOM is not a performance success.
