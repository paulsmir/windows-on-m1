# D3D12 C: implementation stages

Status 2026-10-17. Builds on D3D12-A1 (ABI gate), D3D12-B (hybrid
architecture) and EXP1344 (the 26100 runtime's first calls).

## Measured runtime order (EXP1344)

`OpenAdapter12` -> `GetCaps(D3D12DDICAPS_TYPE_0081_3DPIPELINESUPPORT1,
8 bytes)` -> `GetCaps(D3D12DDICAPS_TYPE_3DPIPELINESUPPORT, 4 bytes)` ->
`GetSupportedVersions` (count, then list: 0003) -> `CloseAdapter` when the
pipeline caps are refused. `D3D12CreateDevice` returns
`DXGI_ERROR_UNSUPPORTED`.

## Admission rule

Normal processes keep `E_NOTIMPL` from `OpenAdapter12`. The development
profile stays process-scoped (`APPLE_AGX_D3D12=1`): only there do caps admit
the device, and only for behaviour the current stage implements; every
other DDI stays fail-closed with `pfnSetErrorCb`. No D3D12 caps reach
ordinary processes until the FL 11_0 baseline below passes on hardware.

## Stages (one hardware checkpoint each)

| Stage | Scope | Windows contract | AGX/KMD side | Checkpoint |
|---|---|---|---|---|
| C1 | caps for the queried types, CalcPrivateDeviceSize, CreateDevice storing runtime callbacks, FillDDITable (device core 0003, command list 3D 0003, queue core 0001) fail-closed | d3d12umddi.h caps 1007/1074, CreateDevice_0003, FillDDITable | none | probe: D3D12CreateDevice S_OK, receipts list every caps type and table the runtime asks for |
| C2 | committed heaps/resources (buffers, 2D textures) | CreateHeap/CreateResource, pfnAllocateCb, GpuMmu VA callbacks | existing GPUVA KMD allocations | probe: CreateCommittedResource + Map/Unmap round trip |
| C3 | command queue, fence, empty ExecuteCommandLists | CreateCommandQueue, monitored-fence sync objects, SignalFence/WaitForFence | KMD context per queue; render fence | probe: Signal/Wait/GetCompletedValue |
| C4 | copy lists | CopyBufferRegion/CopyTextureRegion, allocators, Close/Reset | Asahi blit/compute copy through the winsys | probe: GPU copy verified on CPU |
| C5 | root signature + compute PSO (DXBC SM 5.x via dxbc_to_nir) + Dispatch | CreateRootSignature, CreatePipelineState (compute), descriptor heaps | NIR binding lowering to AGX descriptors | probe: compute writes a UAV |
| C6 | graphics PSO, RTV/DSV, clear, draw | graphics PSO, OMSetRenderTargets, Draw | render batch (existing G4 path) | probe: triangle readback |
| C7 | DXGI presentation from a queue | DXGI 1.4+ swapchain on a D3D12 queue | existing present path | windowed present |
| C8 | FL 11_0 baseline (formats, MSAA, queries, tier-1 binding, residency) | D3D12 feature-level table | as D3D11 | conformance subset, then admit outside the profile |

## Ownership

UMD D3D12 frontend (new, owns ABI objects, heaps/VA records, root
signatures, lists, barriers, queues/fences); shared: DXBC->NIR, libagx
compiler, AGX layouts/encoders (D3D12-B option 3); KMD: unchanged until a
stage proves it needs a new contract (monitored fences and per-queue
contexts are the first candidates, C3).

## Unknowns per stage

C1: which further caps/tables the runtime requests after the pipeline caps;
whether it calls FillDDITable before CreateDevice. C2: the UMD-side GPU VA
contract for D3D12 heaps (MapGpuVirtualAddressCb vs reserved ranges). C3:
monitored-fence support required of the KMD (DXGK caps). Measure each at
its checkpoint; no feature claim ahead of it.
