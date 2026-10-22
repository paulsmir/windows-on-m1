/*
 * FL 11_1 forced sample count: the expansion geometry shader (dxbc_tir_gs.c,
 * investigation/analysis/D3D11-FL111-TIR.md).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DXBC_TIR_GS_H
#define DXBC_TIR_GS_H

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

struct nir_shader;
struct nir_shader_compiler_options;

/* The GS reads viewport 0 from this constant buffer: one vec4
 * {scale.x, scale.y, offset.x, offset.y} in pixels, so that viewport
 * x = ndc.x * scale.x + offset.x (D3D: scale.y = -height / 2). No D3D11
 * constant buffer slot (0..13) uses it. */
#define DXBC_TIR_GS_CB 15u

#define DXBC_TIR_MAX_VARYINGS 40u

struct dxbc_tir_varying {
   uint8_t slot;          /* gl_varying_slot */
   uint8_t components;    /* float vector width */
   uint8_t interp;        /* INTERP_MODE_SMOOTH, _NOPERSPECTIVE or _FLAT */
};

struct dxbc_tir_gs_key {
   unsigned count;
   struct dxbc_tir_varying v[DXBC_TIR_MAX_VARYINGS];
   bool primitive_id;     /* the pixel shader reads SV_PrimitiveID */
};

/* The key for the last vertex stage's outputs feeding the pixel shader
 * variant's inputs (interpolation from the pixel shader): false when an
 * output is not a plain float vector or there is no position. */
bool dxbc_tir_gs_key_from(const struct nir_shader *last, const struct nir_shader *ps,
                          struct dxbc_tir_gs_key *key);

/* The expansion GS for a key (NULL without a position). */
struct nir_shader *dxbc_tir_gs(const struct nir_shader_compiler_options *options,
                               const struct dxbc_tir_gs_key *key);

#ifdef __cplusplus
}
#endif

#endif /* DXBC_TIR_GS_H */
