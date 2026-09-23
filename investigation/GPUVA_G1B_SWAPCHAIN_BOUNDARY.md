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

## EXP760 correction — 2026-09-23

The same current UMD/INF bytes under a WDDM3.0 KMD repeated CreateSwapChain
failure in a proven console Session1. Its sole process-local first refusal was
`CreateResource line467 E_INVALIDARG`; line467 in the exact ARM64 native source
propagates failure from `AgxD3d10WindowsPresentationCreate`. This rejects
attributing EXP758's aggregate HRESULT to WDDM3.2 alone. The prior question
about a failing KMD/UMD boundary is answered at UMD; no second unchanged64-KiB
diagnostic is needed. The new smallest checkpoint is a valid RGBA backbuffer
with `pPrimaryDesc`: return `NO_SCANOUT`, allocate it offscreen, then reach
CreateSwapChain without a CreateResource rejection. Microsoft documents that
`pPrimaryDesc` can be supplied with BIND_PRESENT and the driver can request
Blt-style presentation through `DXGI_DDI_PRIMARY_DRIVER_FLAG_NO_SCANOUT`.

## Offline correction gate after EXP760

The exact ARM64 native `Resource.cpp` SHA256
`d21485faa2a7df9a9c63ee19c120141584f00e74347b3c3d828406d10a050c266`
propagates `AgxD3d10WindowsPresentationCreate`'s HRESULT at line467.
The repo's `umd/src/umd.c::AdmissionUmdDescribePrimary` specifically rejected
`DXGI_FORMAT_R8G8B8A8_UNORM` whenever `pPrimaryDesc` was nonnull, while the
same UMD already builds an offscreen RGBA allocation and the winsys imports it
as a render target for Blt conversion. The standard client requests an RGBA
swapchain; Microsoft documents that DXGI may pass a nonnull `pPrimaryDesc`
with BIND_PRESENT and lets the driver set `NO_SCANOUT` to force Blt rather than
flip. Pinned WDK26100 `um/dxgiddi.h:214-226` defines the same flag and output
field. Asahi owns RGBA render support; the Windows UMD owns primary admission
and choosing Blt; KMD/Scanout stays BGRA, m1n1/Mu unchanged.

The focused invariant is: RGBA primary candidate is admitted, returns
`NO_SCANOUT`, carries RGBA allocation format with `Displayable=0`, and is not
marked as a scanout primary in `pfnAllocateCb`. BGRA primary remains scanout
eligible. The correction removes only the RGBA+nonnull-primary rejection,
sets the documented output flag, and keeps KMD physical-primary identity for
displayable resources. It also adds an argument-bearing rejection receipt for
any remaining invalid primary input. No source from Asahi was copied.

Deterministic test gate: the old UMD implementation with the new isolated
contract test exits3 (three exact CHECK_FAILs at lines5275-5278); corrected
implementation exits0 with the existing full x64 native frontend/runtime
suite. ARM64 UMD compiles/links with pinned WDK26100. Evidence manifest:
`.local/experiments/EXP761-rgba-primary-no-scanout/offline/manifest.json`
SHA256 `220185e9295ab6022e059fb43efae9dd0e6abcb9aea17d9fb0d07808456025bc`.
This is offline proof of UMD admission, not a hardware Present verdict.

## EXP761 correction — actual runtime input

The exact same KMD/INF with the changed UMD did not pass CreateSwapChain.
Its new `reject-primary` receipt shows Format28 RGBA, **pPrimaryDesc=NULL**,
Bind PRESENT|RT, MiscFlags0x8, 2560×1600/sample1. The NO_SCANOUT branch was
not exercised. `D3D10_DDI_RESOURCE_MISC_DISCARD_ON_PRESENT` is documented by
Microsoft for `DXGI_SWAP_EFFECT_DISCARD` backbuffers; the current SHARED-only
MiscFlags validator rejects it. See EXP761 causal result SHA256
`34ca160081a16c2a94bec1b831f321f023565c8cd30bb0d562369ed0b2652e75`.

Microsoft also limits successful NO_SCANOUT opt-out to
`DXGI_DDI_PRIMARY_OPTIONAL`. The earlier unconditional RGBA+nonnull-primary
success path lacks the non-optional proxy/rotation companion and must be
superseded, not silently treated as validated. The active fix target is the
observed DISCARD_ON_PRESENT Usage/Bind contract, with a negative case for
invalid combinations and all other misc bits reviewed individually.
