# D3D12 C38: pipeline-state serialization (cached PSOs)

## Why now

EXP1834: Microsoft's D3D12PipelineStateCache sample called
`ID3D12PipelineState::GetCachedBlob` right after creating its pipelines; the
runtime reached `pfnSerializeObject` (receipt `measure-d3d12-rejected
{table 0, offset 0x1c8}` = D3D12DDI_DEVICE_FUNCS_CORE_0003::pfnSerializeObject,
AGX_REJECT), the device was removed and the sample failed fast. Engines
commonly persist PSO caches this way.

## WINDOWS CONTRACT

WDK 26100 `d3d12umddi.h`:

- `PFND3D12DDI_SERIALIZEOBJECT(hDevice, HandleType, HANDLE hObject,
  SIZE_T *BlobSize, const UINT **ppBlob)` returns a driver-owned blob;
  `PFND3D12DDI_DESTROYOBJECTSERIALIZATION(hDevice, BlobSize, pBlob)` releases it.
- `PFND3D12DDI_CALCPRIVATEDESERIALIZEDOBJECTSIZE(hDevice, HandleType, BlobSize,
  pBlob)` and `PFND3D12DDI_CREATEDESERIALIZEDOBJECT(hDevice, HandleType,
  BlobSize, pBlob, hObject, hRTObject)` recreate an object from a blob.
- `D3D12DDI_HT_PIPELINE_STATE` = 21.
- The runtime wraps the driver blob with its own header (adapter, driver
  version) and hands only driver bytes back.

## AGX/ASAHI CONTRACT

None: a pipeline state compiles on the owner at its first use; the blob
carries the DXBC programs and state descriptions, not AGX binaries.

## TRANSLATION

- Blob: magic "AGXP", version 1, size; IBStripCutValue, SampleMask,
  SampleDesc; blend, rasterizer and depth-stencil descriptions; the element
  layout (DDI elements carry registers, no pointers); program sizes, hull
  pass-through masks, and the programs in PSO order.
- Serialize accepts only a live pipeline state (the identity registry) of
  type 21 and logs `measure-d3d12-serialize {type, valid, bytes}`.
- Deserialize validates magic/version/size and the program sizes against
  the blob size (`PsoBlobBytes`, offline test test_d3d12_pso_blob), rebuilds
  the pipeline state as CreatePipelineState would (S_FALSE, compiled at
  first use, published identity) and logs `measure-d3d12-deserialize`.

## WHAT IS STILL UNKNOWN

Whether `hObject` of pfnSerializeObject is the driver private pointer (the
serialize receipt's `valid` field answers it), and which DDI the runtime
uses for a CachedPSO creation (deserialize receipt).

## Checkpoint

Probe C38A: a triangle PSO's cached blob recreates a PSO that draws the
triangle (WARP: 3662-byte runtime blob, green 1352, PASS).
