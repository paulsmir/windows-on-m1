# D3D12 C7: swapchains and present

Status 2026-10-17. C1-C6 cover device, resources, compute and graphics on
the native owner. C7 makes a D3D12 swapchain show on screen.

Sources inspected: WDK 26100 d3d12umddi.h -- PFND3D12DDI_PRESENT_0003
(command-list table: `(hCommandList, hCommandQueue, const
D3D12DDIARG_PRESENT_0001*, D3D12DDI_PRESENT_0003*)`), D3D12DDIARG_PRESENT_0001
(phSurfacesToPresent, hDstResource, Flags, FlipInterval, VidPnSourceID,
dirty rects, private data), D3D12DDI_PRESENT_0003 (out: hSrcAllocation,
hDstAllocation, hContext, broadcast contexts, AddedGpuWork,
BackBufferMultiplicity), D3D12DDIARG_BLT, D3D12DDI_TABLE_TYPE_DXGI,
D3D12DDI_HEAP_FLAG_PRIMARY; the repository's D3D10/11 presentation path
(mesa/winsys/agx_d3d10_windows.cpp AgxD3d10WindowsPresentationCreate/Open:
an AdmissionUmd kernel resource allocated with the runtime resource handle
and pPrimaryDesc, an Asahi import of it as the render target;
AgxD3d10WindowsPresentationSubmitSurfaces: flush for present, publish
direct maps, AdmissionUmdSubmitPresent -> pfnPresentCb).

## WINDOWS CONTRACT

- D3D12 permits only flip-model swapchains. DXGI creates the buffers
  through the D3D12 runtime (CreateHeapAndResource with an hRTResource,
  heap flags such as PRIMARY for scan-out buffers) and presents through
  the command-list DDI pfnPresent: the driver may record GPU work (a blt)
  into the runtime's list and returns the source/destination allocations
  and the kernel context; the runtime then submits the present to the
  kernel itself.
- The DXGI DDI table (D3D12DDI_TABLE_TYPE_DXGI) carries the DXGI device
  functions (gamma caps, display mode, residency, blt) the D3D11 path
  implements in its DXGI functions.

## AGX/ASAHI CONTRACT

- Presentable surfaces must be KMD allocations that the presentation path
  (KMD Present/blt or DWM composition) can read -- the D3D11 path's
  AdmissionUmd presentation resources -- rendered through an Asahi import
  of the same allocation, flushed before the present.

## TRANSLATION (candidate, pending C7A)

1. Swapchain buffers: CreateHeapAndResource with an hRTResource (and/or
   PRIMARY) creates an AdmissionUmd presentation resource (as
   AgxD3d10WindowsPresentationCreate) and imports it into the owner as the
   texture of the committed resource.
2. pfnPresent: flush the owner for present, publish the buffer's direct
   map, return hSrcAllocation = the presentation allocation, hContext = the
   queue's kernel context, AddedGpuWork = FALSE.
3. DXGI table: the D3D11 path's gamma/display-mode/residency answers.

## WHAT IS STILL UNKNOWN

- The 26100 runtime's internal order between DXGI and d3d12core: which
  DDIs it calls for swapchain creation (DXGI table fill, resource flags,
  open/shared resources) and present -- not documented; measured by C7A.

## Checkpoint

Probe C7A (measurement): window, CreateSwapChainForHwnd (2 x 64x64 RGBA8,
FLIP_DISCARD) on the direct queue, back buffer 0 cleared green, transition
to PRESENT, Present(0, 0), signal; every step prints its HRESULT; the UMD
receipts (d3d12-fill-table, measure-d3d12-create-heap-resource,
measure-d3d12-rejected) name the first missing DDI. It runs last in the
probe because a refused swapchain may remove the device.
