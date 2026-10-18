# D3D-H3: DXBC -> NIR translator (2026-10-15, design record)

Decision (c) of D3D-H-ddi-plan.md: the frontend translates DXBC token streams
straight to NIR, replacing ShaderParse/ShaderTGSI + tgsi_to_nir. This record
fixes the contract the translator must honour so that the NIR path is a
drop-in replacement for the TGSI path on Asahi, then grows to SM 4.1 / 5.x.

## Sources inspected

- Microsoft tokenized program format: d3d10/d3d11/d3d12TokenizedProgramFormat.hpp
  (Windows SDK 10.0.26100, copied to .local/reference/wdk26100).
- Mesa 26.3.0-devel 9aa1215f (the UMD's Mesa): frontends/d3d10umd ShaderParse.c,
  ShaderTGSI.c, Shader.cpp; auxiliary/nir/tgsi_to_nir.c (ttn_emit_declaration,
  ttn_src_for_file_and_index, ttn_add_output_stores, ttn_finalize_nir);
  drivers/asahi agx_state.c (agx_create_shader_state, agx_shader_initialize).
- This repository's projection (build-native-asahi-state.py): the TGSI
  fragment-output lowering (EXP1287), the stream-output -> nir_xfb_info
  rebuild (D3D-G8), the VS-only SO variant (D3D-G10e).

## Layer ownership

| Piece | Owner |
|---|---|
| Token decoding | drivers/apple-agx/mesa/dxbc/dxbc_parse.{h,c} (done: 511ce37e, fxc-listing equality on 10 fixtures) |
| DXBC semantics -> NIR | drivers/apple-agx/mesa/dxbc/dxbc_to_nir.{h,c} (this record) |
| Binding model (cb/t/s/u slots), linkage, stream output | the d3d10umd frontend (unchanged slot numbering) |
| NIR lowering, I/O, textures, compilation | Asahi (agx_create_shader_state -> agx_shader_initialize), unchanged |

## Contract the TGSI path establishes today (must be preserved)

- Constant buffer `cb#[i]` -> UBO index `#`, byte offset `16*i` (ttn: CONST[0]
  as load_uniform, then nir_lower_uniforms_to_ubo; CONST[k>0] as load_ubo k-1,
  shifted by one). The frontend binds D3D slot k at pipe constant buffer k.
- Inputs/outputs link by register number: VS input `v#` ->
  VERT_ATTRIB_GENERIC0+#; other stage I/O `#` -> VARYING_SLOT_VAR0+# (TGSI
  GENERIC index = register); system values by name: position -> POS,
  clip/cull distance -> CLIP_DIST0/1, render target array index -> LAYER,
  viewport array index -> VIEWPORT, vertex id -> VERTEX_ID_ZERO_BASE, instance
  id -> INSTANCE_ID, primitive id -> PRIMITIVE_ID, is_front_face -> FRONT_FACE,
  sample index -> SAMPLE_ID. PS targets `o#` -> FRAG_RESULT_DATA0+#, oDepth ->
  FRAG_RESULT_DEPTH, oMask -> FRAG_RESULT_SAMPLE_MASK.
- Outputs live in registers and are stored to the output variables at the end
  (and before every return / GS emit), as ttn_add_output_stores does; this is
  also what makes fragment exports terminal (EXP1287).
- Textures: separate texture `t#` and sampler `s#` indices on nir_tex_instr
  (ttn after nir_lower_samplers); the result follows the resource operand's
  swizzle; the type follows dcl_resource's return type.
- Stream output: the frontend builds nir_xfb_info from the SO declaration for
  NIR shaders (the TGSI-specific rebuild in agx_create_shader_state is skipped
  when nir->xfb_info is set).
- Finalization: the ttn_finalize_nir sequence (returns, vars/regs to SSA,
  system values, samplers) followed by screen->finalize_nir, so Asahi receives
  NIR in the same state as from tgsi_to_nir.

## Values that differ from TGSI conventions (translated directly)

- DXBC registers are untyped 32-bit; comparisons produce 0xFFFFFFFF/0;
  booleans tested as integer != 0; is_front_face reads 0xFFFFFFFF/0.
- Shifts use the low 5 bits (NIR shift semantics already mask).
- imul/umul/udiv write two destinations (high/low, quotient/remainder);
  sincos writes sin and cos; either destination may be null.

## Staging and checkpoints

1. SM 4.0 VS/PS/GS parity. Offline: every fixture translates and passes
   nir_validate after the finalize sequence on the host Mesa build
   (libnir.a + libasahi_compiler.a). Hardware: behind a UMD probe switch
   (TGSI stays the default), the existing D3D smokes (D3D11, GS, MRT, query,
   depth, SO, sampling, format, MSAA) and DWM must be unchanged.
2. SM 4.1: gather4, lod, sample_pos, sample_info, ld_ms, per-sample
   interpolation; then D3D-H1 can report FL 10_1.
3. SM 5.0 compute: UAV typed/raw/structured, atomics, groupshared, sync,
   bit ops, then HS/DS phases (Asahi tessellation through compute), class
   linkage (interfaces resolved to switch over function tables), doubles.

Smallest falsifiable hardware checkpoint for stage 1: the D3D11 smoke with
the probe switch on draws the same pixels as with TGSI.
