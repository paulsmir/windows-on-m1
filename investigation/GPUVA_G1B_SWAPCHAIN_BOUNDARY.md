# EXP758 first regression: standard CreateSwapChain on WDDM 3.2

Scope: **FULL GRAPHICS**, physical-mode 64-KiB control. This is one offline
causal pass after EXP758. It is not a GpuMmu implementation or a 16-KiB result.

## Primary sources inspected

- Pinned WDK26100 `km/dispmprt.h:2690-3043` and
  `shared/d3dkmddi.h:1859-1869,3857-3866,3912-3925,4141-4178,4624-4629,
  11050-11181` on the builder. The `d3dkmddi.h` SHA256 is
  `c13cecb0ce73e7bbdb6bec8586d05eea31932a8c532bec49b3dae4a03054770e`.
- Current KMD `driver.c`, `lifecycle.c`, `memory_windows.c`,
  `allocation_windows.c`, `callbacks.c`, `display.c`, and `receipts.c` in
  `drivers/apple-agx/render-admission/src/`.
- EXP736 standard Present verdict and EXP758 raw ETL SHA256
  `29631cc003e709f720c625d4b4eed5fe9ec017746989dae423ec5504e4cd82f5`,
  registry receipts, exact client output and final recovery evidence.
- Microsoft [GpuMmu model](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/gpummu-model)
  and [allocation information DDI](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3dkmddi/ns-d3dkmddi-_dxgk_allocationinfo).

## Observed contract and first boundary

EXP736's isolated standard client crossed CreateSwapChain, Draw and Present on
the physical WDDM3.0 package. EXP758's exact758 physical WDDM3.2 package
started APPL0002 Code0 and reported `WDDMVersion=0x3200`; the same client
reached `CreateDevice=S_OK` but `CreateSwapChain=0x887A0005`, device reason
`0x887A0020`, before any draw/Present. Its System32 UMD file hash is exact, but
a client-PID image-load event was not proven. This is a regression at the
swapchain boundary, not evidence that a native 16-KiB UAT page is unsupported.

The EXP758 DxgKrnl trace has 639494 events and lost0. The only exact nonzero
DxgKrnl `Status` events in the bounded full-trace pass were two early
`DdiQueryInterface=0xC00000BB` records; their optional interface GUIDs and
position before allocation do not identify the swapchain failure. DWM emits
`UpdateContextStatus=5/1` later; those are context state values, not the first
failing KMD NTSTATUS. The trace had no client-PID2252 UMD image record and no
client-correlated first failing DDI. `Wom1CleanQueryType/Status` preserve only
the last query. Therefore the owner of the first swapchain failure remains
unknown between DXGI/UMD admission, primary allocation/placement and a 3.2
query response. Do not infer a fault from the aggregate device-removed reason.

## Version-specific WDK inventory at this boundary

`dispmprt.h` adds optional-feature callback slots in 3.1/3.2 (native fences,
doorbells, dirty tracking, live migration, debug/context-priority/display
reset). `d3dkmddi.h` exposes QAI39-46, the fence-storage standard-allocation
branch, new paging operations and `DXGK_ALLOCATIONINFOFLAGS2` bits including
NoImplicitSynchronization/DisablePartialResidency/RestrictedToSingleSegment.
The current physical-mode KMD advertises none of those optional feature bits,
keeps `Flags2.Value=0`, and does not request native fence storage. Its relevant
3.2 changes are Version/caps, QAI paging/segment/MMU responses and the
segment descriptor. The pinned WDK excerpts do not prove that our new
`DXGK_SEGMENTDESCRIPTOR5` fields are accepted for every primary placement.

Ownership remains: VidMm allocates/places the primary and manages residency;
KMD describes supported segments/allocations and reports errors; UMD/DXGI
creates the swapchain; m1n1/Mu own unchanged hardware/firmware contracts.
No MMIO, RTKit, UAT or queue hypothesis is causal before CreateSwapChain.

## Next smallest discriminator and recovery

Keep the documented 64-KiB slab and exact isolated client. Add bounded
per-QAI39-46 status/slab receipts, first 16 incoming CreateAllocation page
field pairs, and first 16 **failed** KMD DDI receipts for
GetStandardAllocationDriverData, CreateAllocation, DescribeAllocation and
OpenAllocation. Turn on a process-local UMD refusals trace for that client.
These are observation-only changes. The first attributable failure decides
whether to fix UMD, KMD primary/allocation, or a query response before changing
the slab to 16 KiB. The hardware checkpoint is the existing CreateSwapChain
result; a new failure is not a reason to exercise render or presentation.
Before any run, preregister exact package hashes and recover to GPU-visible
Code28 with no AppleAgx package, service, module, signer or staged driver;
preserve user-requested autologon. If the receipts remain silent, stop this
hypothesis after that one discriminator and design a different measurement.
