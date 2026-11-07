# D3D12 C37: hull and domain shaders (tessellation)

## Why now

The D3D12 path refused pfnCalcPrivateTessellationShaderSize, pfnCreateHullShader
and pfnCreateDomainShader (AGX_REJECT, device removed), and RecordDraw refused
any PSO with a hull or domain shader (measure-d3d12-draw reason 4). FL 11_0
D3D12 titles and benchmarks commonly tessellate. The D3D11 path already
tessellates on hardware (D3D-H4, the fl11cs control in every regression run),
through the same DXBC translator and Asahi Gallium.

## WINDOWS CONTRACT

- WDK 26100 `d3d12umddi.h`: `PFND3D12DDI_CALC_PRIVATE_TESSELLATION_SHADER_SIZE`
  and `PFND3D12DDI_CREATE_TESS_SHADER_0003` take the tokenized program and
  `D3D12DDIARG_TESSELLATION_IO_SIGNATURES` (input, output = control point,
  patch constant signatures; entries `{SystemValue, Register, Mask, ...}`,
  same leading layout in the deprecated and _0012 entry types).
- `D3D12DDIARG_CREATE_PIPELINE_STATE_0001` carries `hHullShader` and
  `hDomainShader`; the runtime validates HS/DS pairing, the PATCH topology
  type and the control point count against the HS.
- `IASetPrimitiveTopology` passes D3D_PRIMITIVE_TOPOLOGY 33..64 for 1..32
  control point patch lists.
- Root parameters and static samplers use `D3D12DDI_SHADER_VISIBILITY_HULL`
  (2) and `_DOMAIN` (3).

## AGX/ASAHI CONTRACT

- Asahi Gallium: `create_tcs_state`/`create_tes_state` (agx_create_shader_state),
  `bind_tcs_state`/`bind_tes_state`, `set_patch_vertices`; `draw_vbo` with
  `MESA_PRIM_PATCHES` runs `agx_draw_patches` (tessellation on compute through
  libagx); indexed-indirect patches fall back to `util_draw_indirect`.
- `agx_blitter_save` saves the TCS/TES, so blits and clears are unaffected by
  bound tessellation stages.

## TRANSLATION

- DDI: hull/domain shader objects store the program like other stages; a
  hull shader also stores the registers of its output signature and which
  hold SV_Position (the D3D11 frontend's `hs_passthrough`), what a
  pass-through control point phase copies when fxc emits none.
- PSO: the HS and DS programs follow the GS in the PSO's program copy;
  `CompileGraphics` hands them (and the pass-through masks) to the owner.
- Draw recording: HS and DS must come together (reason 4); patch topologies
  draw exactly with an HS+DS pipeline (reason 9). Root bindings resolve per
  stage by visibility and are recorded as `kCommandHsBind`/`kCommandDsBind`
  slots before the draw, as C27 does for the GS. Replay hands a recorded
  stage binding only to a draw whose pipeline has that stage.
- Owner: `dxbc_translate_tokens_bindings_hs` (bindings + pass-through
  registers) creates the TCS/TES; the draw binds TCS/TES (NULL unbinds),
  sets the patch vertex count from the topology, and binds the optional
  stages' CBV/SRV/UAV/samplers through one per-stage loop shared with the GS
  (records on the heap, not the draw frame).

## WHAT IS STILL UNKNOWN

Asahi tessellation through this winsys's D3D12 owner (the D3D11 path uses
the same Gallium entry points); hull-shader root CBV binding on hardware.

## Checkpoint

Probe C37A: one triangle patch, HS factors from a HULL-only root CBV
(factor 0 would cull), DS colour from a DOMAIN-only root CBV; centre blue,
corners red, ~1311 blue pixels (WARP reference: 1352). C37B: then a plain
TRIANGLELIST pipeline draws a green corner triangle (WARP: 120 green); a
leftover HS/DS would recolour or drop it. WARP + debug layer: both PASS, only
the informational clear-value message.
