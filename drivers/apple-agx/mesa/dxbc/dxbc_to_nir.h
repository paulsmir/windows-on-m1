/*
 * DXBC (shader model 4.x/5.x) -> NIR translation.
 *
 * The NIR follows the conventions the D3D10 frontend's TGSI path hands a
 * Gallium driver after tgsi_to_nir (investigation/analysis/D3D-H3-dxbc-nir.md):
 * cb# reads are UBO # loads, I/O links by register number (GENERIC slots) or
 * system-value name, outputs are kept in registers and stored at the end of
 * the program (and before each return or geometry emit), textures carry
 * separate texture and sampler indices.  The caller finalizes the shader as
 * tgsi_to_nir does (returns, registers to SSA, screen->finalize_nir).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DXBC_TO_NIR_H
#define DXBC_TO_NIR_H

#include "dxbc_parse.h"
#include "dxbc_resources.h"

#ifdef __cplusplus
extern "C" {
#endif

struct nir_shader;
struct nir_shader_compiler_options;

/* Binding conventions for shader model 5 resources (the frontend binds to
 * match): a typed UAV u# is image slot #, a raw or structured UAV u# is
 * shader-buffer slot #, its hidden append/consume counter (one 32-bit word
 * at offset 0) is shader-buffer slot DXBC_NIR_UAV_COUNTER_SLOT + #, and a
 * raw or structured SRV t# is texel-buffer sampler view # read one 32-bit
 * dword per texel.
 * UAV registers are u0..u7 (D3D11_PS_CS_UAV_REGISTER_COUNT, the D3D11.0
 * DDI), so buffers and counters fill shader-buffer slots 0..15 of Gallium's
 * 32.  The 64 slots of the D3D11.1 DDI use dxbc_translate_tokens_uav64. */
#define DXBC_NIR_MAX_UAVS 8
#define DXBC_NIR_UAV_COUNTER_SLOT DXBC_NIR_MAX_UAVS

/* D3D11.1 (feature level 11_1): UAV registers u0..u63
 * (D3D11_1_UAV_SLOT_COUNT). A typed view u# stays image slot #
 * (PIPE_MAX_SHADER_IMAGES 64); buffer views and their counters move to
 * consecutive shader buffers, buffer views in register order first:
 * buffer[u#] and counter[u#] name the slot, DXBC_NIR_UAV_UNUSED where the
 * shader has none. A shader needing more than PIPE_MAX_SHADER_BUFFERS (32)
 * is refused. */
#define DXBC_NIR_UAV11_SLOTS 64
#define DXBC_NIR_UAV_UNUSED 0xffu
struct dxbc_nir_uav_slots {
   uint8_t buffer[DXBC_NIR_UAV11_SLOTS], counter[DXBC_NIR_UAV11_SLOTS];
   uint64_t typed;     /* typed views: bit u# */
   uint32_t buffers;   /* shader buffers used */
};

struct dxbc_nir_error {
   char message[160];
   uint32_t offset;     /* DWORD offset of the instruction refused */
   uint32_t opcode;
};

/* Returns NULL (and fills 'error') for a program this translator does not
 * handle yet; the caller keeps its other path for it. */
struct nir_shader *
dxbc_to_nir(const struct dxbc_program *program,
            const struct nir_shader_compiler_options *options,
            struct dxbc_nir_error *error);

/* A hull shader whose control points pass through (no control-point
 * phase): the D3D runtime's output signature names the registers the phase
 * copies ('registers') and those holding SV_Position ('positions').
 * dxbc_to_nir translates every other program; a pass-through hull shader
 * there copies no register. */
struct nir_shader *
dxbc_to_nir_hs(const struct dxbc_program *program,
               const struct nir_shader_compiler_options *options,
               uint32_t passthrough_registers, uint32_t passthrough_positions,
               struct dxbc_nir_error *error);

/* The finalization tgsi_to_nir applies before handing NIR to a driver
 * without screen->finalize_nir (Asahi): lower returns and continue
 * constructs, registers and variables to SSA, system values, then a
 * standard optimization loop; the shader info is gathered and validated. */
void dxbc_nir_finalize(struct nir_shader *nir);

/* Asahi's integer pixel-coordinate lowering requires frontend fragment-center
 * normalization. Call exactly once on a fresh fragment shader, before handing
 * it to Asahi. Kept explicit: other frontends may already normalize position. */
void dxbc_nir_lower_fragment_position(struct nir_shader *nir);

/* Parse a tokenized program (version and length tokens first), translate
 * and finalize it.  NULL when the program is malformed (error->opcode is
 * 0x10000 | the dxbc_status) or not handled yet. */
struct nir_shader *
dxbc_translate_tokens(const uint32_t *tokens,
                      const struct nir_shader_compiler_options *options,
                      struct dxbc_nir_error *error);

/* D3D12 static SM5.1 namespaces.  SM4/5.0 retains identity bindings and
 * returns an inactive map; every failure clears the map. */
struct nir_shader *
dxbc_translate_tokens_bindings(const uint32_t *tokens,
   const struct nir_shader_compiler_options *options,
   const uint32_t limits[DXBC_BINDING_KIND_COUNT], struct dxbc_binding_map *map,
   struct dxbc_nir_error *error);

/* C37: dxbc_translate_tokens_bindings for a hull shader with the
 * pass-through registers of its output signature (other stages pass 0). */
struct nir_shader *
dxbc_translate_tokens_bindings_hs(const uint32_t *tokens,
   const struct nir_shader_compiler_options *options,
   const uint32_t limits[DXBC_BINDING_KIND_COUNT], struct dxbc_binding_map *map,
   uint32_t passthrough_registers, uint32_t passthrough_positions,
   struct dxbc_nir_error *error);

/* dxbc_translate_tokens with the D3D11.1 UAV registers (see
 * dxbc_nir_uav_slots); hull shaders pass their pass-through registers,
 * other stages 0. */
struct nir_shader *
dxbc_translate_tokens_uav64(const uint32_t *tokens,
                            const struct nir_shader_compiler_options *options,
                            uint32_t passthrough_registers, uint32_t passthrough_positions,
                            struct dxbc_nir_uav_slots *slots, struct dxbc_nir_error *error);

/* FL 11_1: the pixel shader variant for a forced sample count of 4, 8 or 16
 * (dxbc_tir.h; investigation/analysis/D3D11-FL111-TIR.md), with the
 * D3D11.1 UAV registers: coverage of the forced pattern from the expansion
 * GS's edge varyings, SV_Coverage, sampleinfo/samplepos of the rasterizer,
 * centroid and pull-model evaluation on that pattern. */
struct nir_shader *
dxbc_translate_tokens_tir(const uint32_t *tokens,
                          const struct nir_shader_compiler_options *options,
                          struct dxbc_nir_uav_slots *slots, unsigned forced_samples,
                          struct dxbc_nir_error *error);

/* dxbc_translate_tokens for a hull shader with the pass-through registers
 * of its output signature (see dxbc_to_nir_hs). */
struct nir_shader *
dxbc_translate_tokens_hs(const uint32_t *tokens,
                         const struct nir_shader_compiler_options *options,
                         uint32_t passthrough_registers, uint32_t passthrough_positions,
                         struct dxbc_nir_error *error);

/* D3D stream output of a vertex or geometry shader (D3D-H5): each output
 * names a D3D output register, its components and a buffer offset in
 * dwords; the frontend resolves the declaration's holes and strides. */
#define DXBC_NIR_MAX_XFB_OUTPUTS 64
#define DXBC_NIR_MAX_XFB_BUFFERS 4
struct dxbc_nir_xfb_output {
   uint32_t reg;
   uint8_t start_component, num_components, buffer, stream;
   uint32_t dst_offset;   /* dwords */
};
struct dxbc_nir_xfb {
   uint32_t num_outputs;
   struct dxbc_nir_xfb_output outputs[DXBC_NIR_MAX_XFB_OUTPUTS];
   uint32_t stride[DXBC_NIR_MAX_XFB_BUFFERS];   /* dwords */
   /* the output-signature registers holding SV_Position (DXIL capture,
    * agx_dxil_nir_xfb; the DXBC translator reads its own declarations) */
   uint32_t position_registers;
};

/* dxbc_translate_tokens for a vertex or geometry shader whose outputs are
 * also captured: the NIR carries nir_xfb_info built from the translator's
 * own output locations (the TGSI path rebuilds it from TGSI semantics in
 * Asahi's state creation).  Stream 0 only; system-value outputs other
 * than the position are refused. */
struct nir_shader *
dxbc_translate_tokens_xfb(const uint32_t *tokens,
                          const struct nir_shader_compiler_options *options,
                          const struct dxbc_nir_xfb *xfb,
                          struct dxbc_nir_error *error);

/* C39: dxbc_translate_tokens_bindings_hs that also captures stream output as
 * dxbc_translate_tokens_xfb does (xfb NULL captures nothing). */
struct nir_shader *
dxbc_translate_tokens_bindings_xfb(const uint32_t *tokens,
   const struct nir_shader_compiler_options *options,
   const uint32_t limits[DXBC_BINDING_KIND_COUNT], struct dxbc_binding_map *map,
   uint32_t passthrough_registers, uint32_t passthrough_positions,
   const struct dxbc_nir_xfb *xfb, struct dxbc_nir_error *error);

/* The tessellation info Asahi reads from a hull or domain shader:
 * ccw | point_mode << 1 | domain << 8 | spacing << 16 (receipts). */
uint32_t dxbc_nir_tess_info(const struct nir_shader *nir);

/* Capture immutable direct-slot cube requirements before shader creation
 * consumes NIR.  False for dynamic/ambiguous bindings; output is cleared. */
bool dxbc_nir_srv_dimensions(const struct nir_shader *nir,
                             uint8_t dimensions[DXBC_RESOURCE_SLOTS]);

/* Direct image and raw/structured UAV requirements before native consumes NIR.
 * False for dynamic/ambiguous/unhandled image bindings; output is cleared. */
bool dxbc_nir_uav_dimensions(const struct nir_shader *nir,
                            uint8_t dimensions[DXBC_UAV_DIMENSION_SLOTS]);

/* The thread-group size (dcl_thread_group) and groupshared bytes of a
 * translated compute shader: the block the frontend's Dispatch launches and
 * the static shared memory of its pipe_compute_state. */
void
dxbc_nir_compute_info(const struct nir_shader *nir, unsigned block[3],
                      unsigned *shared_bytes);

#ifdef __cplusplus
}
#endif

#endif /* DXBC_TO_NIR_H */
