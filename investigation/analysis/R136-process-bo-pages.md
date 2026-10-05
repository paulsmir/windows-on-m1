# R136 process BO page contract: offline no-go

## Evidence and inspected sources

- EXP854B `Wom1G4SubmitFailure` first rejects Process[0], write 64 KiB at
  VA `0x3b0000`, with no native graph mapping. The saved logical PTEs are
  segment 0 and backed by discontiguous 4 KiB PFNs. R135 fixes a separate
  empty-root reuse bug, still unvalidated on Air.
- `shared/include/apple_agx_g4_submit.h` and
  `mesa/winsys/agx_win32_gpuva_batch.c`: all nine Process ranges are allocated
  by the UMD. `AppleAgxG4ProcessRequiredBytes` rounds every size to 64 KiB;
  `agx_bo_create` rounds size and GPU VA alignment to 64 KiB already. The
  nine ranges are page list, block list, block heap, user buffer, tilemap,
  heap metadata, tail-pointer cache, preemption scratch, and auxiliary FB.
- `render-admission/src/allocation_windows.c`: the KMD reports 64 KiB
  `Alignment`, local preference, and aperture plus local allowed segments for
  CPU-visible general BOs. `render_win32_transport.c` carries the UMD class
  and flags. Pinned WDK 26100 `d3dkmddi.h:3921-3922` marks WDDM 3.2
  `MinimumPageSize` and `RecommendedPageSize` **input** fields. They share
  storage with `Alignment`. Writing both as enum 4 in KMD would replace
  `Alignment=0x10000` with `0x00040004`, invalid for a 64-KiB local segment.
- EXP854B's exact build uses the G1b 16-KiB local segment profile while
  R134 advertises system-memory 64-KiB support. The R136 page policy applies
  in G1b profiles 16 and 64; profile 0 fails closed for marked process BOs.
- EXP836's live R105 matrix rejected local-only `SupportedWriteSegmentSet`
  across the tested Mesa classes. The Microsoft `DXGK_ALLOCATIONINFO` and
  64-KB-page documentation says VidMm uses the write segment set and chooses
  the PTE page size from allocation and resident segment properties; system
  memory can force a return to 4 KiB PTEs. See
  https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfo
  and https://learn.microsoft.com/en-us/windows-hardware/drivers/display/support-for-64kb-pages .
- `m1n1_windows/src/hv_agx_local_reserve.c`,
  `m1n1_windows/src/hv_agx_power_mmio.c`, Mu generated
  `J313AppleAgxAbiAdmission.asl.inc`, and KMD `memory_windows.c` retain the
  existing 64 MiB R64 segment and broker/ACPI contract. They are unchanged.
  The existing G4 builder follows Asahi's per-process TVB/user-buffer split
  in `drivers/gpu/drm/asahi/buffer.rs` and `queue/render.rs` on the Asahi
  branch (https://github.com/AsahiLinux/linux/tree/asahi/drivers/gpu/drm/asahi);
  no Asahi code is copied here.

For a 2560x1600, one-layer scene with 16x16 utiles, the current UMD
requests the following 64-KiB-rounded `AgxWin32BufferClassGeneral` BOs.
All have 64-KiB VA alignment. KMD currently allows aperture (segment 1)
and local R64 (segment 2) for each; only Process[0]'s actual segment-0
system backing was measured in EXP854B. The other actual placements are
unmeasured.

| Process | Purpose | Logical bytes | Allocation bytes |
| --- | --- | ---: | ---: |
| 0 | TVB page list | 512 | 65,536 |
| 1 | TVB block list | 256 | 65,536 |
| 2 | TVB block heap | 4,194,304 | 4,194,304 |
| 3 | user buffer | 65,664 | 131,072 |
| 4 | tilemap | 102,400 | 131,072 |
| 5 | heap metadata | 512 | 65,536 |
| 6 | tail-pointer cache | 1,310,720 | 1,310,720 |
| 7 | preemption scratch | 18,144 | 65,536 |
| 8 | auxiliary FB | 32,768 | 65,536 |

## Ownership and offline verdict

The UMD owns process BO creation, CPU initialization, GPU VA, and residency
references through the render fence. VidMm owns placement and 4/64 KiB paging;
the KMD describes allocations and owns page-table updates, interrupts,
submission, completion and recovery. m1n1/Mu own the existing power, reserve
and ACPI exposure; none should change for this boundary.

At 2560x1600, one process set is 6,094,848 bytes with 16x16 utiles
(5,046,272 with 32x32); the 64 MiB R64 budget is unchanged. Existing per-BO
maximum is 16 MiB. The current BO sizes and alignments already meet the
proposed 64-KiB multiple, yet EXP854B observed discontiguous 4-KiB PFNs for
Process[0]. Thus that proposed change cannot distinguish a new outcome.

The apparent per-allocation KMD page-size output path is invalid: the pinned
WDK marks both fields as input, and the earlier R21 review expressly forbids
writing them in KMD. The supported local-segment alternative is also not
available in the present CPU-visible class: EXP836 tested local-only write
set 0x2 across read/MMU, physical-access and page-size variants; VidMm rejected
every row with `STATUS_INVALID_PARAMETER`. The admitted set 0x3 plus a local
preference already produced segment-0 backing in EXP854B. A new marker alone
would not change that outcome. A process marker scoped through a shared UMD
backend would additionally race unrelated BO creation, so any later design
must pass intent explicitly to one allocation call.

Microsoft documents nonpaged `VirtualAlloc2(MEM_64K_PAGES)` as a way to fail
if contiguous 64-KiB system pages are unavailable, but the present
`pfnAllocateCb` class uses runtime-managed system backing and does not
establish that its `pSystemMem`/`ExistingSysMem` path supports a UMD-owned
buffer. The documented `D3DKMTCreateAllocation` ExistingSysMem example is
restricted to standard allocations. This is an offline contract question,
not a basis for silently changing the package:
https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualalloc2
and https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmthk/nf-d3dkmthk-d3dkmtcreateallocation .

Next causal target: prove one supported UMD allocation path that either
preserves user-supplied, nonpaged contiguous 64-KiB backing in VidMm or lets
CPU-initialized process BOs reside only in R64. That requires an offline
WDK/runtime contract and a targeted callback replay before a package is
built. The smallest eventual Air checkpoint remains the first G4 failure
moving off Process[0] VA `0x3b0000` or accepted submission without a new
`0x10E`. Evidence must be collected before exact cleanup and ordinary
EXP377/392 Code28 recovery. `GO_EXP855` was absent during this analysis;
no Air action was taken.
