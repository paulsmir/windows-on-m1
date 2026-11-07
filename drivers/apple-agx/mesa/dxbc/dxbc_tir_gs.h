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
/* C61C: a wrapped application GS that reads descriptor heap ranges has
 * constant buffer 15 as its heap mirror; its viewport rides at this byte
 * offset of the heap table, constant buffer 14 (dxbc_bindings.h: after the
 * 64 range vec4s and the 2048-entry sampler map) */
#define DXBC_TIR_GS_HEAP_TABLE_CB 14u
#define DXBC_TIR_GS_HEAP_VIEWPORT_BYTES ((4u * 64u + 2048u) * 4u)

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
   /* conservative rasterization: every edge moves out by one pixel (not
    * half) so the whole square of each touched pixel is inside and the
    * hardware covers all its samples (D3D12: all samples of a
    * conservatively rasterized pixel are covered); set by the caller */
   bool full_pixel;
};

/* The key for the last vertex stage's outputs (vertex or domain shader)
 * feeding the pixel shader variant's inputs (interpolation from the pixel
 * shader): false when an output is not a plain float vector, there is no
 * position, or a domain shader does not produce triangles. */
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

/* A copy of a shader (nir_shader_clone) for callers without NIR headers:
 * the TIR key reads a stage's I/O after the original went to the driver. */
struct nir_shader *dxbc_tir_clone(const struct nir_shader *nir);

/* The conservative-rasterization (D3D12 Tier 1) variant of a fragment
 * shader behind the expansion GS: it runs where the original triangle
 * touches the pixel (dxbc_tir_touched), SV_Coverage the full sample. */
bool dxbc_cr_lower_fs(struct nir_shader *nir);

/* C61C: rewrite a finalized application geometry shader (triangle-strip
 * output, one stream) to emit the expanded triangles itself; the key from
 * dxbc_tir_gs_key_from(gs, ps). false leaves it unusable for the variant. */
bool dxbc_tir_wrap_gs(struct nir_shader *gs, const struct dxbc_tir_gs_key *key, bool heap_viewport);

/* The expansion GS for a key (NULL without a position). */
struct nir_shader *dxbc_tir_gs(const struct nir_shader_compiler_options *options,
                               const struct dxbc_tir_gs_key *key);

#ifdef __cplusplus
}
#endif

#endif /* DXBC_TIR_GS_H */
