/*
 * FL 11_1 forced sample count / target-independent rasterization
 * (investigation/analysis/D3D11-FL111-TIR.md): the contract between the
 * expansion geometry shader (dxbc_tir_gs.c) and the pixel shader variant
 * (dxbc_translate_tokens_tir).
 *
 * The GS hands the pixel shader the original triangle's edges as
 * DXBC_TIR_EDGE_SLOTS flat ivec4 varyings at VARYING_SLOT_TEX0 + i, legacy
 * slots the D3D translators never use:
 * - TEX0 + i (i = 0..2) = {x_i, y_i, A_i, B_i}: vertex i in 1/256 pixel (D3D
 *   16.8 snapping) and the edge function E_i(p) = A_i (p.x - x_i) +
 *   B_i (p.y - y_i) of the edge from vertex i to vertex i + 1, positive
 *   inside;
 * - TEX3 = {bias_0, bias_1, bias_2, 0}: a sample p is covered when every
 *   E_i(p) + bias_i >= 0 (bias -1 off the top-left edges).
 *
 * SPDX-License-Identifier: MIT
 */
#ifndef DXBC_TIR_H
#define DXBC_TIR_H

#include <stdint.h>

#define DXBC_TIR_EDGE_SLOTS 4u
/* 16.8 fixed point: 256 units per pixel, pixel centres at +128 */
#define DXBC_TIR_SUBPIXEL 256

/* The D3D standard sample patterns, 1/16 pixel from the pixel centre
 * (identical to Vulkan's standard sample locations, Mesa
 * vk_standard_sample_locations.c). */
static const int8_t dxbc_tir_pattern4[4][2] = {{-2, -6}, {6, -2}, {-6, 2}, {2, 6}};
static const int8_t dxbc_tir_pattern8[8][2] = {{1, -3}, {-1, 3}, {5, 1}, {-3, -5},
                                               {-5, 5}, {-7, -1}, {3, 7}, {7, -7}};
static const int8_t dxbc_tir_pattern16[16][2] = {
   {1, 1}, {-1, -3}, {-3, 2}, {4, -1}, {-5, -2}, {2, 5}, {5, 3}, {3, -5},
   {-2, 6}, {0, -7}, {-4, -6}, {-6, 4}, {-8, 0}, {7, -4}, {6, 7}, {-7, -8}};

/* The pattern of a forced sample count (4, 8 or 16), NULL for others. */
static inline const int8_t (*dxbc_tir_pattern(unsigned samples))[2]
{
   return samples == 4 ? dxbc_tir_pattern4 : samples == 8 ? dxbc_tir_pattern8 :
          samples == 16 ? dxbc_tir_pattern16 : (const int8_t (*)[2])0;
}

/* The edge data of one triangle (the GS's varyings). */
struct dxbc_tir_edges {
   int32_t x[3], y[3], a[3], b[3], bias[3];
};

/* Reference for the expansion GS's setup: vertices in 1/256 pixel (snapped,
 * y down), reordered so that the signed area is positive and every edge
 * function is positive inside; D3D's top-left rule keeps samples on a top
 * edge (horizontal, interior below) or a left edge (going up) and drops
 * them on the others. 0 for a zero-area triangle (D3D draws nothing). */
static inline int
dxbc_tir_setup(const int32_t vx[3], const int32_t vy[3], struct dxbc_tir_edges *e)
{
   const int64_t area = (int64_t)(vx[1] - vx[0]) * (vy[2] - vy[0]) -
                        (int64_t)(vy[1] - vy[0]) * (vx[2] - vx[0]);
   const unsigned order[3] = {0u, area > 0 ? 1u : 2u, area > 0 ? 2u : 1u};
   if (!area)
      return 0;
   for (unsigned i = 0; i < 3; ++i) {
      const unsigned from = order[i], to = order[(i + 1) % 3];
      const int32_t dx = vx[to] - vx[from], dy = vy[to] - vy[from];
      e->x[i] = vx[from];
      e->y[i] = vy[from];
      e->a[i] = -dy;
      e->b[i] = dx;
      e->bias[i] = (dy < 0 || (dy == 0 && dx > 0)) ? 0 : -1;
   }
   return 1;
}

/* Reference for the pixel shader variant: the covered samples of pixel
 * (px, py) under the forced pattern of 'samples'. */
static inline unsigned
dxbc_tir_mask(const struct dxbc_tir_edges *e, int32_t px, int32_t py, unsigned samples)
{
   const int8_t (*pattern)[2] = dxbc_tir_pattern(samples);
   const int32_t cx = px * DXBC_TIR_SUBPIXEL + DXBC_TIR_SUBPIXEL / 2;
   const int32_t cy = py * DXBC_TIR_SUBPIXEL + DXBC_TIR_SUBPIXEL / 2;
   unsigned mask = 0;
   for (unsigned s = 0; pattern && s < samples; ++s) {
      int inside = 1;
      for (unsigned i = 0; i < 3; ++i) {
         const int64_t centre = (int64_t)e->a[i] * (cx - e->x[i]) + (int64_t)e->b[i] * (cy - e->y[i]) +
                                e->bias[i];
         const int32_t step = e->a[i] * pattern[s][0] * (DXBC_TIR_SUBPIXEL / 16) +
                              e->b[i] * pattern[s][1] * (DXBC_TIR_SUBPIXEL / 16);
         inside &= centre + step >= 0;
      }
      mask |= (unsigned)inside << s;
   }
   return mask;
}

/* Reference for conservative rasterization (D3D12 Tier 1): whether the
 * primitive touches pixel (px, py) -- the separating-axis test of the
 * triangle against the pixel square: every edge function at the square's
 * outermost corner (centre + half a pixel along each axis) is >= 0, and the
 * bounding boxes overlap. Exact, inside the Tier 1 1/2-pixel uncertainty. */
static inline int
dxbc_tir_touched(const struct dxbc_tir_edges *e, int32_t px, int32_t py)
{
   const int32_t cx = px * DXBC_TIR_SUBPIXEL + DXBC_TIR_SUBPIXEL / 2;
   const int32_t cy = py * DXBC_TIR_SUBPIXEL + DXBC_TIR_SUBPIXEL / 2;
   int32_t minx = e->x[0], maxx = e->x[0], miny = e->y[0], maxy = e->y[0];
   for (unsigned i = 0; i < 3; ++i) {
      const int64_t centre = (int64_t)e->a[i] * (cx - e->x[i]) + (int64_t)e->b[i] * (cy - e->y[i]);
      const int64_t reach = (int64_t)((e->a[i] < 0 ? -e->a[i] : e->a[i]) + (e->b[i] < 0 ? -e->b[i] : e->b[i])) *
                            (DXBC_TIR_SUBPIXEL / 2);
      if (centre + reach < 0)
         return 0;
      minx = e->x[i] < minx ? e->x[i] : minx;
      maxx = e->x[i] > maxx ? e->x[i] : maxx;
      miny = e->y[i] < miny ? e->y[i] : miny;
      maxy = e->y[i] > maxy ? e->y[i] : maxy;
   }
   return minx < cx + DXBC_TIR_SUBPIXEL / 2 && maxx > cx - DXBC_TIR_SUBPIXEL / 2 &&
          miny < cy + DXBC_TIR_SUBPIXEL / 2 && maxy > cy - DXBC_TIR_SUBPIXEL / 2;
}

#endif /* DXBC_TIR_H */
