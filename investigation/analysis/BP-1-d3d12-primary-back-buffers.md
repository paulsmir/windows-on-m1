# BP-1: D3D12 back buffers created with HEAP_FLAG_PRIMARY are kernel primaries

Status: implemented offline on arch/bp1 (cut from exp/df1d d5f073147). No hardware run yet.
Inputs: OV-ARCHITECT-HANDOFF.md entry "2026-11-12T05:59Z TO THE IMPLEMENTER: BP-1";
`.local/night-20261110/swapchain-buffers/REPORT.md`; `.local/night-20261110/guest-dxgkrnl-read/REPORT-check3-resource.md`.
Header paths: `wdk26100/` = `.local/reference/wdk26100/` (d3d12umddi.h there is byte-identical to `winsdk26100/um/d3d12umddi.h`).

Classification: FULL GRAPHICS (the allocation contract of a render target that is also presented).

## WINDOWS CONTRACT

Header (pinned):

- `wdk26100/d3d12umddi.h:313` `D3D12DDI_HEAP_FLAG_PRIMARY = 0x10,` in `D3D12DDI_HEAP_FLAGS` (307-316), carried by
  `D3D12DDIARG_CREATEHEAP_0001.Flags` (319-328). Microsoft (ne-d3d12umddi-d3d12ddi_heap_flags): "The heap supports
  resources allocated for primary." Nothing more is documented.
- `d3d12umddi.h:4818-4826` `D3D12DDI_ALLOCATION_INFO_FLAGS_0022_PRIMARY = 0x1` (STEREO 0x2, OVERRIDE_PRIORITY 0x4).
  Microsoft: "A primary value." The online page also lists a `_SHARED` constant that the pinned header does not have.
- `d3d12umddi.h:4828-4839` `D3D12DDI_ALLOCATION_INFO_0022 { ... D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId; (4834)
  D3D12DDI_ALLOCATION_INFO_FLAGS_0022 Flags; (4835) ... }`. Microsoft: VidPnSourceId is "A zero-based ID of the video
  present source in a path of a video present network topology." `D3D12DDICB_ALLOCATE_0022` (4841-4848) has no
  shared, swap-chain or displayable field.
- The D3D10/11-style callback this driver actually fills: `wdk26100/d3dukmdt.h:398`
  `D3DDDI_VIDEO_PRESENT_SOURCE_ID VidPnSourceId; // in: VidPN source ID if this is a primary` and 403
  `UINT Primary : 1;`. `BridgeAllocateCb` (`drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp:689-718`) copies both into
  `D3D12DDI_ALLOCATION_INFO_0022`.
- `d3d12umddi.h:562-569` `D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_PRIMARY = 0x4`. Microsoft: "Perform primary heap
  optimizations." No required behaviour is stated.
- `d3d12umddi.h:691` `#define D3D12DDI_FORMAT_SUPPORT_DISPLAY 0x00200000 // format supports Display (0x00100000 is
  reserved for displayables in d3d11)`; added in build rev 107 (12769). No semantics beyond the comment.
- D3D10/11 comparison: `wdk26100/d3d10umddi.h:493-494` `pPrimaryDesc` ("If pPrimaryDesc absent, blt/ copy style is
  implied"), `wdk26100/dxgiddi.h:220-226` `DXGI_DDI_PRIMARY_DESC { Flags; VidPnSourceId; ModeDesc; DriverFlags; }`,
  `d3d10umddi.h:398` `D3DWDDM2_0DDI_RESOURCE_MISC_DISPLAYABLE_SURFACE`.

Where does the D3D12 DDI give the VidPn source of such a buffer? **At creation it gives none.**
`D3D12DDIARG_CREATEHEAP_0001` (319-328) and the `D3D12DDIARG_CREATERESOURCE_*` structures have no source and no primary
description (grep of `VIDEO_PRESENT_SOURCE` in d3d12umddi.h: only 1638 and 4834). The only source the D3D12 DDI carries
is per present: `D3D12DDIARG_PRESENT_0001.VidPnSourceID` (1638). The code base has a single-output invariant: the KMD
reports `*NumberOfVideoPresentSources = 1;` (`drivers/apple-agx/render-admission/src/lifecycle.c:558`), and the PL-1
retention is defined per device for "one source, VidPnSource 0" (`umd/include/umd_primary_retention.h`). Source 0 is
therefore the only valid value. NOT DOCUMENTED: that the runtime expects the UMD to set Flags.Primary for a resource in
a PRIMARY heap; the header only names the flag and the callback fields.

## AGX-ASAHI CONTRACT

- The KMD admits the driver's `ADMISSION_PRESENT_RESOURCE_WRITTEN_PRIMARY` only for a GPU-only GDI-surface texture in a
  scan-out format (`render-admission/src/allocation_windows.c:442-448`, else STATUS_INVALID_PARAMETER) and derives
  `ScanoutCapable` itself (478-485). `AdmissionScanoutFormatSupported` (`scanout_windows.c:957-965`): A8R8G8B8 always;
  A8B8G8R8 only with broker `APPLE_AGX_SCANOUT_CAP_RGBA8888` (`shared/include/apple_agx_scanout.h:28`, bit 7).
- Broker capability on the bench images (source, not a live receipt): the s2v3 m1n1 (63c13618, local worktree
  `.local/m1n1-wt-s2-broker-v3`) and its closedv5-scaled1 base 14c893b2 both contain m1n1 21c3648d "scan out RGBA8888
  surfaces as DCP RGBA behind CAP_RGBA8888"; `src/hv_agx_scanout_broker.c:7-10` puts `HV_AGX_SCANOUT_CAP_RGBA8888` in
  `SCANOUT_V1_CAPS` unconditionally, and `format_valid` (42-47) accepts `HV_AGX_SCANOUT_FORMAT_RGBA8888` with it. The
  application plane's DCP format follows `AdmissionScanoutFormatCode` -> `MpoLayerFormat` (`mpo_layers.c:8-12`):
  RGBA8 storage -> `APPLE_AGX_SCANOUT_FORMAT_RGBA8888`. The closedv5 working image m1n1 06616be7 is not in the main
  m1n1 checkout, so its capability is NOT VERIFIED here; the next boot's KMD `ScanoutCapable` / broker-info receipt
  answers it.
- `DxgkDdiDescribeAllocation` (`allocation_windows.c:822-842`) reports Width/Height/Format and the created-with refresh
  rate, which is 0/1 for a resource without a primary descriptor (`render_allocation.c` `AdmissionPresentResourceRefreshRate`).
- User mode cannot see the broker capability: no UMD path reads `AdmissionScanoutBrokerInfo`. The UMD's own
  presentability rule ties A8B8G8R8 storage to Displayable 0 (`umd.c` `AdmissionUmdResourceIsPresentable`).

## TRANSLATION

| Input | Before BP-1 | After BP-1 |
|---|---|---|
| committed presentable 2D RT, heap Flags & PRIMARY | Flags.Primary 0, source 0, WrittenPrimary 0 | Flags.Primary 1, source 0; WrittenPrimary = Displayable (BGRA8 storage 1, RGBA8 0) |
| same, heap without PRIMARY | unchanged | unchanged (CreateFlags 0 = old call) |
| D3D10/11 pPrimaryDesc / DISPLAYABLE misc | unchanged | unchanged |
| allocation ownership / release of a D3D12 PRIMARY buffer | runtime-resource allocation, retirement Primary 0 | identical (never device-associated, never PL-1 retained) |

Code:
- `d3d12/agx_d3d12_ddi.cpp` `CreateHeapAndResource` (2664): `runtime_primary = presentable && (heap_arg->Flags &
  D3D12DDI_HEAP_FLAG_PRIMARY)`, passed to `AgxD3d12OwnerCreatePresentationTexture`; receipt
  `measure-d3d12-committed-texture` value 4 gains bit 0x400.
- `mesa/winsys/agx_d3d12_windows.cpp` `AgxD3d12OwnerCreatePresentationTexture`: one call
  `AdmissionUmdCreateResourceWithMetadata(..., Converted ? CONVERTED : 0, runtime_primary ? ADMISSION_UMD_CREATE_RUNTIME_PRIMARY : 0)`.
  The request is a UMD-local argument, never a resource-data bit (the KMD validator rejects unknown bits).
  Converted (10-bit / MSAA / BGRX) buffers get the same mark: their presentation allocation is the resource's kernel
  allocation named by Present.
- `render-admission/umd/include/umd_primary_backbuffer.h` (new, neutral): `AdmissionUmdPrimaryMap` gives
  AllocationPrimary, VidPnSourceId 0, WrittenPrimary, DeviceAssociated (pPrimaryDesc && !shared only) and
  RetirementPrimary (pPrimaryDesc only); `AdmissionUmdStorageDisplayable` (A8R8G8B8 -> 1).
- `umd.c` `AdmissionUmdCreateResourceWithMetadata` uses the map for resource-data Flags, `allocationInfo.VidPnSourceId`,
  `Flags.Primary`, `allocate.hResource` and `retirement->Primary`; `AdmissionUmdOpenResource` uses
  `AdmissionUmdStorageDisplayable`, so create and open give the same Displayable. Receipt `measure-present-primary-desc`
  gains CreateFlags, Displayable and resource-data Flags (17 -> 20 values; the row already had Flags.Value and source).
- PL-1 (d5f073147): retention requires `DeviceAssociated` and the allocation of the last successful SetDisplayMode.
  A D3D12 PRIMARY buffer keeps `hResource = runtime resource`, so `DeviceAssociated` is 0 and
  `AdmissionUmdRetentionDestroy` returns 0; the release takes the same path as before (retirement Origin 1, Primary 0).

Deliberately not done:
- `D3D12DDI_FORMAT_SUPPORT_DISPLAY`: left unreported. BGRA8 is always scan-out capable, but RGBA8 depends on a kernel
  capability user mode cannot read, and reporting it would change what DXGI does with swap-chain formats: a second
  variable. Open item.
- `D3D12DDI_RESOURCE_OPTIMIZATION_FLAG_PRIMARY` in `CheckResourceAllocationInfo` (1989-1990) and the 0022/0088
  variants (8120, 8710): the header gives no behaviour ("Perform primary heap optimizations"); our presentation layout
  is the same linear one either way, so there is nothing truthful to change.
- A PRIMARY heap whose resource is not on the presentable list (other formats, mips/arrays, placed) is still an owner
  texture without a kernel primary. RGBA8 user-mode Displayable stays 0 (conservative; the KMD decides scan-out).
- KMD `DisplayableSupport` cap (`d3dkmddi.h:2478`): not set; separate contract.

Side effect to watch (BGRA8 D3D12 swap chains only; GravityMark's RGBA8 buffers are not affected): such a buffer is
now WrittenPrimary 1, so where the screen adopts it as a direct slot, a submission that writes it lists its allocation
in `D3DDDICB_RENDER` WrittenPrimaries (`umd/src/umd_gpuva_windows.c:2710-2720`), as for DWM's BGRA primaries. That is
consistent with Flags.Primary 1 but has not run on hardware.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? (`tests/test_umd_primary_backbuffer.py`,
`umd/tests/primary_backbuffer_test.c`): the EXP2361 defect (PRIMARY dropped before `Flags.Primary`); WRITTEN_PRIMARY
requested for RGBA8 storage (KMD refusal, EXP1038); a D3D12 primary becoming device-associated, i.e. captured by PL-1
retention and released by HandleList; create and open disagreeing on Displayable; any change for resources without the
request. The serialization test executes the actual umd.c slice and the KMD's resource-data validator.

## WHAT IS STILL UNKNOWN

- Whether the D3D12 runtime's core-layer `pfnAllocateCb` and dxgkrnl accept `Flags.Primary` for an A8B8G8R8
  allocation of a committed resource with a runtime hResource, and whether dxgkrnl then calls
  `DxgkDdiDescribeAllocation` (refresh 0/1) and accepts it - hardware.
- Whether the runtime also sets CreateShared / SwapChainBackBuffer (d3dkmthk.h:1555,1565) on these allocations and
  whether DWM's check reaches `DxgkDdiCheckMultiPlaneOverlaySupport3` (U-2) - hardware (KMD CreateFlags receipt).
- Broker RGBA8888 on the closedv5 working m1n1 06616be7 (source not available in this checkout).
- Whether a D3D12 fullscreen SetDisplayMode would use these buffers (refresh 0/1 vs EXP954 rule) - not exercised by GM.
- MSVC-only: this machine builds neither the UMD nor the D3D12 driver for Windows (see the commit report).
