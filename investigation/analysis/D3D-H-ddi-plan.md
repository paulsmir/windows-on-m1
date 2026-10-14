# D3D-H: from the D3D10.0 DDI to FL 11_0 (2026-10-14, plan)

The D3D groundwork, D3D-phase-plan.md step 3, is nearly complete. These items run on hardware:
- compute inside render (G1);
- MRT (G2);
- occlusion queries (G3a);
- depth-stencil (G4);
- the tile buffer by Mesa's rule (G6).

Built or queued for hardware:
- MSAA (G5) and depth-only passes (G7): EXP1285;
- predication (G3b): EXP1285;
- stream output (G8): EXP1286.

This document is step 4 of D3D-phase-plan.md, option A: extend the Mesa d3d10umd frontend.

## Sources inspected

**Pinned WDK, `.local/reference/wdk26100/d3d10umddi.h`.**
- Function tables:
  - `D3D10DDI_DEVICEFUNCS` has 103 entries.
  - `D3D10_1DDI_DEVICEFUNCS` has 105. It adds `pfnResourceConvert` and `pfnResourceConvertRegion`. It also switches `CalcPrivate/CreateBlendState` to `D3D10_1_DDI_BLEND_DESC` (independent per-target blend) and `CalcPrivate/CreateShaderResourceView` to the 10.1 SRV description (cube arrays).
  - `D3D11DDI_DEVICEFUNCS` has 152, i.e. 47 more than 10.1:
    - compute: `CreateComputeShader`, `Cs*`, `Dispatch`, `DispatchIndirect`;
    - UAVs: `Create/Destroy/CalcPrivateUnorderedAccessView`, `ClearUnorderedAccessView{Float,Uint}`, `CopyStructureCount`;
    - tessellation: `Create{Hull,Domain}Shader`, `Hs*`, `Ds*`;
    - deferred contexts and command lists: `CreateDeferredContext`, `*CommandList*`, `Recycle*`;
    - `Draw{Indexed}InstancedIndirect`;
    - `SetResourceMinLOD`;
    - `*SetShaderWithIfaces` (class linkage).
- Interface versions:
  - `D3D10_0/10_1/11_0_DDI_INTERFACE_VERSION`;
  - `D3D11_0_DDI_SUPPORTED`;
  - `D3D11DDICAPS_THREADING` (driver command lists are optional; the runtime emulates them when the driver reports none).

**Mesa d3d10umd (pinned).**
- `SUPPORT_D3D10_1` and `SUPPORT_D3D11` (State.h) gate only the version lists and the `CreateDevice` interface switch.
- No 10.1 or 11 entry points are implemented.
- ShaderParse/ShaderTGSI translate SM 4.0. The projection adds `ld2dms`.

**Our projection (`build-native-asahi-state.py`).**
- It advertises `D3D10_0_DDI_SUPPORTED` and `D3D10_0_x` only.
- It reports `D3D11DDICAPS_3DPIPELINESUPPORT` as 10_0 and `D3D11DDICAPS_THREADING` as 0.

## Contract and ownership

| Layer | FL 10_1 needs | FL 11_0 adds |
|---|---|---|
| d3d10umd frontend | the 10.1 table; per-target blend (pipe already supports it); cube-array SRVs; `ResourceConvert*` (copies between compatible formats); SM 4.1 `gather4`, `lod`, `sampleinfo`, `samplepos` | the D3D11 table; the SM 5.0 parser (UAV ops, atomics, `sync`, HS/DS phases, doubles optional); compute and UAV binding; tessellation states; indirect draws (pipe `draw_vbo` indirect); deferred contexts left to the runtime (THREADING caps 0) |
| Mesa Asahi (Gallium) | already: 4x MSAA (G5), independent blend, gather, LOD | already: compute grids, images/SSBOs (UAVs), tessellation through compute (agx_draw_patches), indirect draws |
| winsys / KMD | nothing new (render batches) | compute-only submissions exist (G2); UAV writes are plain BOs in the written set; tessellation runs as compute inside render (G1) |

## Order (each step with its hardware checkpoint)

1. **D3D-H1: D3D10.1 DDI (FL 10_1).**
   - Advertise `D3D10_1_*_DDI_SUPPORTED`.
   - Accept the 10.1 interface in `CreateDevice` and fill `D3D10_1DDI_DEVICEFUNCS`.
   - Implement the 10.1 blend and SRV descriptions and `ResourceConvert*`.
   - Report `D3D11DDICAPS_3DPIPELINESUPPORT` 10_1 only when 4x MSAA is CONFIRMED (EXP1285).
   - Checkpoint: D3D11CreateDevice returns FL 10_1; a per-target blend smoke and a gather4 smoke pass; every D3D regression is unchanged.
2. **D3D-H2: the D3D11 DDI table at FL 10_x.**
   - Accept `D3D11_0_DDI_INTERFACE_VERSION`.
   - Fill `D3D11DDI_DEVICEFUNCS` with the 10.1 entries plus:
     - fail-closed compute/UAV/tessellation entries;
     - indirect draws;
     - `SetResourceMinLOD`;
     - `*WithIfaces` mapped to the plain setters (no class linkage).
   - Threading caps stay 0.
   - Checkpoint: the D3D11 runtime opens the device through the 11.0 DDI at FL 10_1, and every regression is unchanged.
3. **D3D-H3: SM 5.0 parse and translate** (compute first).
   - Pieces: CS, UAV load/store/atomics, `sync`, groupshared memory.
   - These go to TGSI (and then NIR) or directly to NIR. The choice is recorded after reading Mesa's ShaderParse/ShaderTGSI coverage and the NIR builder cost.
   - Checkpoint: a compute shader with UAV writes and atomics; ClearUAV.
4. **D3D-H4: tessellation** (HS/DS phases → TGSI tess stages → Asahi's tessellation through compute).
   - Checkpoint: a tessellated quad.
5. **D3D-H5: report FL 11_0.**
   - First check every FL 11_0 requirement against the Microsoft feature-level table: formats, 16K textures, UAVs, CS 5.0, tessellation, BC6H/BC7.
   - Acceptance: DWM DDisplay without `DisableDDisplay` (EXP952), then CS2 (x64, needs the ARM64X UMD).

## Known UMD gaps carried into D3D-H

These remain from the groundwork:
- copies of multisampled resources;
- stream output without geometry shader code (VS-only SO);
- SO-statistics, timestamp and pipeline-statistics queries;
- ResourceConvert.
