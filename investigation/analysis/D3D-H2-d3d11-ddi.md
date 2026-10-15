# D3D-H2: the D3D11 DDI (2026-10-15, design record)

Goal: the D3D11 runtime opens this UMD through `D3D11_0_DDI_INTERFACE_VERSION`
(then 11.1), first at FL 10_x with every existing D3D10 behaviour, then grows
compute, UAVs, indirect draws and tessellation towards FL 11_0.

## Sources inspected

- Windows SDK 10.0.26100 `d3d10umddi.h` (D3D11DDI_DEVICEFUNCS: 152 entries;
  D3D10_2DDICAPS_TYPE; D3D11DDI_THREADING_CAPS / SHADER_CAPS /
  3DPIPELINESUPPORT_CAPS), `d3d11TokenizedProgramFormat.hpp`.
- Mesa d3d10umd (Adapter.cpp `OpenAdapter10_2`, `SupportedDDIInterfaceVersions`,
  `GetCaps`; Device.cpp `CreateDevice` 10.0/10.1 tables) and this repository's
  projection of it (only the 10.0 interfaces are advertised; GetCaps answers
  THREADING 0 and 3DPIPELINESUPPORT 10_0).
- D3D-H3 (DXBC -> NIR translator: SM 5 compute/UAV/atomics translate offline;
  binding conventions in dxbc_to_nir.h).

## The table, by origin of each entry

| Origin | Entries | Plan |
|---|---|---|
| D3D10 signature (105) | draws, Set*, state creation (rasterizer, depth-stencil, sampler, element layout), queries, Map/Unmap, copies, CheckFormatSupport, ... | the existing 10.0 implementations, unchanged |
| D3D10.1 signature (2) | CreateBlendState, CalcPrivateBlendStateSize (10.1 blend desc, per-target blend) | upstream CreateBlendState1 (compiled out today by SUPPORT_D3D10_1 0) |
| D3D11 signature (45), same meaning, new argument struct | CalcPrivateResourceSize, CreateResource (D3D11DDIARG_CREATERESOURCE), Calc/CreateShaderResourceView (cube arrays, BufferEx raw views), Calc/CreateDepthStencilView (read-only depth/stencil flags), SetRenderTargets (adds UAVs bound with RTVs), Calc/CreateGeometryShaderWithStreamOutput (multiple streams, rasterized stream), RelocateDeviceFuncs | adapters converting to the existing paths; refuse (E_INVALIDARG/E_NOTIMPL through SetError) only fields with a behaviour not yet implemented |
| D3D11, new behaviour | CreateComputeShader, CsSet*, Dispatch, DispatchIndirect, Calc/Create/DestroyUnorderedAccessView, CsSetUnorderedAccessViews, ClearUnorderedAccessView{Uint,Float}, CopyStructureCount, DrawInstancedIndirect, DrawIndexedInstancedIndirect, SetResourceMinLOD | stage 2 (compute) and stage 3 (indirect) below |
| D3D11, tessellation | CreateHullShader, CreateDomainShader, CalcPrivateTessellationShaderSize, Hs*/Ds* setters | stage 4; until then fail-closed and FL 10_x reported |
| D3D11, class linkage | *SetShaderWithIfaces | the plain setters; interface tables resolved by the translator (D3D-H3) |
| D3D11, deferred contexts and command lists | CheckDeferredContextHandleSizes, Calc*/CreateDeferredContext, Create/Destroy/Abandon/Recycle*CommandList, CommandListExecute | not provided: D3D11DDICAPS_THREADING reports neither FREETHREADED nor COMMANDLISTS, so the runtime emulates deferred contexts; the entries stay NULL |

## Capabilities reported

- D3D11DDICAPS_THREADING: 0 (single-threaded driver; the runtime serializes
  and emulates command lists).
- D3D11DDICAPS_SHADER: 0 at first (no doubles; no raw/structured buffers or
  compute at SM 4.x); `COMPUTE_PLUS_RAW_AND_STRUCTURED_BUFFERS_IN_SHADER_4_X`
  once stage 2 is confirmed.
- D3D11DDICAPS_3DPIPELINESUPPORT: 10_0 until the 4x MSAA defect is fixed and
  SM 4.1 goes through the translator (FL 10_1); 11_0 only after compute,
  UAVs, indirect draws and tessellation are confirmed (8x MSAA cannot be
  supported by AGX; D3D-H-ddi-plan.md).

## Binding model additions (frontend owns, translator conventions)

- UAV slots u0..u7 in the pixel stage (bound with RTVs) and in compute
  (D3D11_PS_CS_UAV_REGISTER_COUNT on the D3D11.0 DDI): typed UAVs -> pipe
  images, raw/structured UAVs -> pipe shader buffers at slot #, hidden
  append/consume counters -> shader buffer 8 + # (slots 0..15 of Gallium's
  32; the D3D11.1 DDI's 64 slots need per-shader slot compaction).
- Raw/structured SRVs: texel-buffer sampler views read one dword per texel.
- Compute: pipe->create_compute_state from the translator's NIR (no TGSI
  path exists for SM 5), launch_grid for Dispatch/DispatchIndirect.

## Order and checkpoints

1. H2a: accept D3D11_0 (and 11.1 later) in OpenAdapter/CreateDevice, fill the
   table from the 10.0 implementations plus the adapters above, keep FL 10_0.
   Checkpoint: every D3D smoke and DWM through the D3D11 interface
   (receipts name the interface), unchanged pixels.
2. H2b: compute and UAVs (translator NIR, images/shader buffers, UAV clears,
   CopyStructureCount). Checkpoint: a compute smoke (typed/raw/structured
   writes, atomics, groupshared, append/consume) with exact results.
3. H2c: indirect draws and dispatch, SetResourceMinLOD.
4. H2d: tessellation (translator HS/DS phases -> NIR TCS/TES; Asahi
   tessellates through compute) and FL 11_0 reporting.

Smallest falsifiable checkpoint for H2a: the D3D11 smoke on the D3D11 DDI
interface draws the same pixels as on the 10.0 interface.

## H2b first checkpoint: compute on 10.x hardware (2026-10-15)

SM 5 compute (cs_5_0) needs a FL 11_0 device; the D3D11 runtime creates it
only when the driver reports 11_0, which needs tessellation and the FL 11_0
format/MSAA set first (D3D-H5). A FL 10_x device runs compute when the driver
reports `D3D11DDICAPS_SHADER_COMPUTE_PLUS_RAW_AND_STRUCTURED_BUFFERS_IN_SHADER_4_X`
(d3d10umddi.h 0x2): cs_4_0 (cs_4_1 on 10_1) with raw and structured buffers,
one UAV slot, groupshared memory written at SV_GroupIndex, no atomics, no
typed UAVs, no append/consume. That is the first truthful hardware checkpoint
for the compute plumbing every later stage reuses.

| Layer | Contract |
|---|---|
| Windows (d3d10umddi.h 10.0.26100) | CreateResource with `D3D11_DDI_BIND_UNORDERED_ACCESS`, `BUFFER_ALLOW_RAW_VIEWS`/`BUFFER_STRUCTURED` + `ByteStride`; SRV `BUFFEREX` with `D3D11_DDI_BUFFEREX_SRV_FLAG_RAW` (format R32_TYPELESS) or a structured view (format UNKNOWN); `D3D11DDIARG_CREATEUNORDEREDACCESSVIEW` Buffer {First, Num, Flags RAW/APPEND/COUNTER}; CreateComputeShader, CsSet*, CsSetUnorderedAccessViews (initial counts for counter views), Dispatch, ClearUnorderedAccessViewUint (raw/structured: values[0] in every word). D3D11 orders a Dispatch's writes before later commands without an API barrier. |
| Translator (D3D-H3) | cs_4_x and cs_5_0 -> NIR compute: workgroup size from dcl_thread_group, groupshared with explicit offsets and `shared_size` (dxbc_nir_finalize), raw/structured SRVs as R32 texel buffers, UAVs as shader buffers u# (counters at 8 + u#). |
| Frontend (projection + agx_d3d11_ddi.cpp) | buffers with unordered access / raw views / structured layout admitted (`PIPE_BIND_SHADER_BUFFER`), stride recorded; raw/structured SRVs become R32_UINT buffer views of the same bytes; UAV objects hold a `pipe_shader_buffer`; CreateComputeShader translates (no TGSI path) and records the block; Dispatch = `launch_grid` (block = numthreads) + `memory_barrier(ALL)`; ClearUAV = `clear_buffer` (libagx fill kernels, as GS already uses). Typed UAVs, counters, DispatchIndirect, CopyStructureCount and pixel-stage UAVs stay fail-closed. |
| Asahi | `create_compute_state` (NIR only), `set_shader_buffers`, `launch_grid` into a compute batch (G2 compute-only submission), `memory_barrier` flushes batches with incoherent writes. |

Capability: reported only with `APPLE_AGX_D3D11_DDI=1` and
`APPLE_AGX_D3D11_COMPUTE=1` in the process until the checkpoint is confirmed.
Checkpoint (agx_compute_smoke.c): the device reports
ComputeShaders_Plus_RawAndStructuredBuffers_Via_Shader_4_x; cs_4_0 structured
(structured + raw SRV, CB, groupshared + barrier), raw (2-D groups, byte
addresses) and chained (ClearUAV, dispatch, dispatch reading the first
through an SRV) results equal the CPU expectation word for word, x86 and
ARM64, every iteration.
