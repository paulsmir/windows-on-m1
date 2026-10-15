# D3D-H4: tessellation (hull and domain shaders) (2026-10-15, design record)

Goal: SM 5 hull (HS) and domain (DS) shaders run on AGX through the DXBC ->
NIR translator and Asahi's tessellation (tessellation runs as compute:
agx_draw_patches launches the TCS as a grid, libagx tessellates, the TES
runs as the hardware vertex stage). Tessellation is one of the FL 11_0
requirements (D3D-H5).

## Sources inspected

- Windows SDK 10.0.26100 `d3d11TokenizedProgramFormat.hpp` (HS phases:
  `hs_decls`, `hs_control_point_phase`, `hs_fork_phase`, `hs_join_phase`,
  `dcl_hs_fork_phase_instance_count`, `dcl_hs_join_phase_instance_count`,
  `dcl_input_control_point_count`, `dcl_output_control_point_count`,
  `dcl_tessellator_domain/partitioning/output_primitive`,
  `dcl_hs_max_tessfactor`; operand types vOutputControlPointID,
  vForkInstanceID, vJoinInstanceID, vicp (input control points), vocp
  (output control points), vpc (patch constants), vDomain, vPrim) and
  `d3d10umddi.h` (CreateHullShader/CreateDomainShader with
  D3D11DDIARG_TESSELLATION_IO_SIGNATURES; Hs*/Ds* setters).
- fxc listings of tests/fixtures/dxbc/hs50/ds50 and an isoline HS
  (`dcl_output_siv o0.x, finalLineDensityTessFactor`, `o1.x
  finalLineDetailTessFactor`; a pass-through HS has no control-point phase).
- Mesa (9aa1215f) `src/microsoft/compiler`: the GL -> DXIL direction maps
  `gl_TessLevelOuter[i]` to `SV_TessFactor[i]` and `gl_TessLevelInner[i]` to
  `SV_InsideTessFactor[i]` index for index (dxil_nir_fixup_tess_level_for_domain
  only resizes: triangles 3/1, quads 4/2, isolines 2/0), `ccw` to
  TRIANGLE_CCW directly, and `load_tess_coord` to DomainLocation unchanged.
- Asahi Gallium (agx_state.c): TCS/TES are ordinary NIR stages;
  `so->tess` records ccw, point_mode, spacing, output patch size
  (tcs_vertices_out), primitive and patch outputs; agx_draw_patches merges
  primitive/spacing/point_mode as MAX2(tcs, tes) but takes the winding
  (`params.ccw`, line 4577, and the rasterizer front face, line 3653) from the
  TES only.

## Mapping

| D3D | NIR |
|---|---|
| HS (all phases) | one MESA_SHADER_TESS_CTRL shader; `tcs_vertices_out` = output control point count; `info.tess` primitive/spacing/ccw/point_mode from the HS declarations |
| control-point phase | runs in every TCS invocation; vOutputControlPointID = load_invocation_id; o# -> per-vertex outputs `gl_out[invocation]` (VARYING_SLOT_VAR0 + #, system values by name) |
| no control-point phase (pass-through) | each invocation copies its input control point's registers to its output control point |
| fork / join phases | after a workgroup barrier, invocation 0 runs every phase instance in order (vForkInstanceID / vJoinInstanceID = loop counter); vocp reads the output control points, vicp the input ones; o# -> patch outputs (VARYING_SLOT_PATCH0 + #) and tess factors |
| SV_TessFactor / finalQuad/Tri/Line*TessFactor | `VARYING_SLOT_TESS_LEVEL_OUTER[i]` by the name's index (tri U/V/W == 0 -> 0/1/2; quad U==0, V==0, U==1, V==1 -> 0..3; line density -> 0, detail -> 1) |
| SV_InsideTessFactor / final*InsideTessFactor | `VARYING_SLOT_TESS_LEVEL_INNER[i]` (quad U, V -> 0, 1; tri -> 0) |
| join phase reading vpc (fork outputs) | patch outputs read back (load_output of the patch slot) |
| DS | MESA_SHADER_TESS_EVAL; `info.tess._primitive_mode` from `dcl_tessellator_domain`; vDomain = load_tess_coord; vicp -> per-vertex inputs (VAR0 + #), vpc -> patch inputs and tess levels by name |
| partitioning integer / pow2 / fractional_odd / fractional_even | TESS_SPACING_EQUAL / EQUAL (pow2 rounds up: the factor is rounded in the shader epilogue) / FRACTIONAL_ODD / FRACTIONAL_EVEN |
| output_point / output_line / output_triangle_cw / output_triangle_ccw | point_mode / isolines / ccw false / ccw true |
| dcl_hs_max_tessfactor | clamp of the factors written (min in the shader) |

Winding: D3D declares it in the HS only; Asahi reads it from the TES only.
Smallest change in the layer that owns it: Asahi merges `ccw` like the other
fields (`tcs->tess.ccw || tes->tess.ccw`), which leaves GL unchanged (GLSL
declares vertex order only in the TES, so a GL TCS never sets it).

## Frontend (D3D11 table)

- CreateHullShader / CreateDomainShader: translate (no TGSI path),
  `create_tcs_state` / `create_tes_state`; the signatures argument is not
  needed (registers carry the linkage, as for VS/GS/PS).
- HsSet*/DsSet* setters: the common-shader setters with
  MESA_SHADER_TESS_CTRL / TESS_EVAL (as Cs*).
- Draws with an HS bound use PIPE_PRIM_PATCHES with the IA topology's
  control point count (D3D11 topology 33..64 = 1..32 control points):
  `set_patch_vertices`.
- Capability: FL 11_0 only (D3D-H5); HS/DS stay fail-closed until then and
  are tested offline (translation + Asahi lowering of the fixtures) and on
  hardware once 11_0 is reported.

Implemented (offline): translator (c8d90f67), D3D11 table entries, the
common setters for TESS_CTRL/TESS_EVAL, DestroyShader, patch topologies
33..64 (`set_patch_vertices`, MESA_PRIM_PATCHES) and the Asahi winding merge
in the projection.  Unknown until hardware: whether GL's winding convention
(Asahi's "OpenGL is backwards" front-face flip) equals D3D's for the same
`ccw` value -- Mesa's GL -> DXIL path maps it unchanged, so the first
tessellation smoke checks culling of a cw and a ccw domain.

## Checkpoints

1. Offline: hs50/ds50, an isoline HS, a quad HS with join phase and a
   pass-through HS translate and validate through Asahi's TCS/TES
   preprocessing on the host build.
2. Hardware (with FL 11_0): a tessellated triangle/quad smoke whose pixel
   coverage equals the D3D tessellator's (integer partitioning, factors 1, 2,
   3, 4) and a pass-through HS.
