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

/* The pixel shader prologue at b's cursor (a fragment shader): the edge
 * varyings, the covered-sample mask of the forced pattern (16.8 fixed
 * point, E at the centre in 64 bits), a demote for an uncovered pixel, and
 * the centroid offset (the centre when every sample is covered, else the
 * lowest covered sample). */
struct nir_builder;
struct nir_def;
void dxbc_tir_coverage(struct nir_builder *b, unsigned samples, struct nir_def **mask,
                       struct nir_def **centroid);

/* Sample 'index' of the forced pattern, in pixels from the centre (0 past
 * the count). */
struct nir_def *dxbc_tir_sample_offset(struct nir_builder *b, unsigned samples, struct nir_def *index);

/* The forced-sample-count variant of a fragment shader that still holds its
 * input variables (the DXIL path): the coverage prologue, SV_Coverage
 * (load_sample_mask_in), centroid inputs and interp_deref_at_sample/centroid
 * on the forced pattern. false for another stage or count. */
bool dxbc_tir_lower_fs(struct nir_shader *nir, unsigned samples);

/* The expansion GS for a key (NULL without a position). */
struct nir_shader *dxbc_tir_gs(const struct nir_shader_compiler_options *options,
                               const struct dxbc_tir_gs_key *key);

#ifdef __cplusplus
}
#endif

#endif /* DXBC_TIR_GS_H */
