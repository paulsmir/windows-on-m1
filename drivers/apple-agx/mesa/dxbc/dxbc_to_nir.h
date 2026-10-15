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

#ifdef __cplusplus
}
#endif

#endif /* DXBC_TO_NIR_H */
