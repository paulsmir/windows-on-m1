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
 * 32.  The 64 slots of the D3D11.1 DDI need per-shader slot compaction. */
#define DXBC_NIR_MAX_UAVS 8
#define DXBC_NIR_UAV_COUNTER_SLOT DXBC_NIR_MAX_UAVS

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

/* Parse a tokenized program (version and length tokens first), translate
 * and finalize it.  NULL when the program is malformed (error->opcode is
 * 0x10000 | the dxbc_status) or not handled yet. */
struct nir_shader *
dxbc_translate_tokens(const uint32_t *tokens,
                      const struct nir_shader_compiler_options *options,
                      struct dxbc_nir_error *error);

/* dxbc_translate_tokens for a hull shader with the pass-through registers
 * of its output signature (see dxbc_to_nir_hs). */
struct nir_shader *
dxbc_translate_tokens_hs(const uint32_t *tokens,
                         const struct nir_shader_compiler_options *options,
                         uint32_t passthrough_registers, uint32_t passthrough_positions,
                         struct dxbc_nir_error *error);

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
