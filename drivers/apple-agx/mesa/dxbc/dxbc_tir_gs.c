/*
 * FL 11_1 forced sample count: the expansion geometry shader
 * (investigation/analysis/D3D11-FL111-TIR.md, dxbc_tir.h).
 *
 * For each triangle of the last vertex stage it emits one conservative
 * (overestimating) triangle that the hardware rasterizes at one sample,
 * and hands the pixel shader variant the original triangle's edges:
 * - the three positions are taken to viewport space (viewport 0, constant
 *   buffer DXBC_TIR_GS_CB) and snapped to D3D's 16.8 fixed point; the edges
 *   are oriented positive inside with the top-left bias, as
 *   dxbc_tir_setup;
 * - each edge moves outward by its half-pixel L-infinity extent plus a
 *   margin, so every pixel any sample of the original triangle can cover
 *   has its centre inside the expanded triangle;
 * - each new vertex is a barycentric combination of the original three,
 *   applied perspective-correctly (smooth), linearly (noperspective) or not
 *   at all (flat: the provoking vertex 0), so the attribute planes equal the
 *   original's;
 * - a zero-area triangle emits nothing; a triangle with a vertex at
 *   w <= DXBC_TIR_MIN_W is emitted unexpanded with every sample covered
 *   (the documented fallback).
 *
 * SPDX-License-Identifier: MIT
 */
#include "dxbc_tir_gs.h"

#include "dxbc_tir.h"
#include "nir.h"
#include "nir_builder.h"

#define DXBC_TIR_MIN_W 1e-6f
/* beyond half a pixel: covers the 1/256 pixel snapping of the vertices */
#define DXBC_TIR_MARGIN (1.0f / 64.0f)

static nir_def *
cross2(nir_builder *b, nir_def *ax, nir_def *ay, nir_def *bx, nir_def *by)
{
   return nir_fsub(b, nir_fmul(b, ax, by), nir_fmul(b, ay, bx));
}

struct nir_shader *
dxbc_tir_clone(const struct nir_shader *nir)
{
   return nir ? nir_shader_clone(NULL, nir) : NULL;
}

bool
dxbc_tir_gs_key_from(const struct nir_shader *last, const struct nir_shader *ps,
                     struct dxbc_tir_gs_key *key)
{
   memset(key, 0, sizeof(*key));
   bool position = false;
   nir_foreach_variable_with_modes(var, (nir_shader *)last, nir_var_shader_out) {
      const struct glsl_type *type = var->type;
      if (!glsl_type_is_vector_or_scalar(type) || glsl_get_base_type(type) != GLSL_TYPE_FLOAT ||
          var->data.location_frac || key->count >= DXBC_TIR_MAX_VARYINGS)
         return false;
      const unsigned slot = (unsigned)var->data.location;
      if (slot >= VARYING_SLOT_TEX0 && slot < VARYING_SLOT_TEX0 + DXBC_TIR_EDGE_SLOTS)
         return false; /* the edge varyings */
      unsigned interp = INTERP_MODE_SMOOTH;
      bool read = false;
      nir_foreach_variable_with_modes(in, (nir_shader *)ps, nir_var_shader_in) {
         if ((unsigned)in->data.location == slot) {
            read = true;
            interp = in->data.interpolation == INTERP_MODE_NONE ? INTERP_MODE_SMOOTH :
                     in->data.interpolation;
         }
      }
      /* the position and clip/cull distances feed the rasterizer */
      if (!read && slot != VARYING_SLOT_POS && slot != VARYING_SLOT_CLIP_DIST0 &&
          slot != VARYING_SLOT_CLIP_DIST1 && slot != VARYING_SLOT_CULL_DIST0 &&
          slot != VARYING_SLOT_CULL_DIST1)
         continue;
      position |= slot == VARYING_SLOT_POS;
      key->v[key->count++] = (struct dxbc_tir_varying){
         (uint8_t)slot, (uint8_t)glsl_get_vector_elements(type), (uint8_t)interp};
   }
   nir_foreach_variable_with_modes(in, (nir_shader *)ps, nir_var_shader_in)
      if (in->data.location == VARYING_SLOT_PRIMITIVE_ID)
         key->primitive_id = true;
   return position;
}

struct nir_shader *
dxbc_tir_gs(const struct nir_shader_compiler_options *options, const struct dxbc_tir_gs_key *key)
{
   nir_builder builder = nir_builder_init_simple_shader(MESA_SHADER_GEOMETRY, options, "dxbc_tir_gs");
   nir_builder *b = &builder;
   nir_shader *nir = b->shader;
   nir_variable *in[DXBC_TIR_MAX_VARYINGS], *out[DXBC_TIR_MAX_VARYINGS], *edge_out[DXBC_TIR_EDGE_SLOTS];
   nir_variable *primitive_out = NULL;
   int pos = -1;

   nir->info.separate_shader = true;
   nir->info.internal = false;
   nir->info.gs.input_primitive = MESA_PRIM_TRIANGLES;
   nir->info.gs.vertices_in = 3;
   nir->info.gs.output_primitive = MESA_PRIM_TRIANGLE_STRIP;
   nir->info.gs.vertices_out = 3;
   nir->info.gs.invocations = 1;
   nir->info.gs.active_stream_mask = 1;

   for (unsigned i = 0; i < key->count; ++i) {
      const struct glsl_type *type = glsl_vector_type(GLSL_TYPE_FLOAT, key->v[i].components);
      in[i] = nir_variable_create(nir, nir_var_shader_in, glsl_array_type(type, 3, 0), "tir_in");
      in[i]->data.location = key->v[i].slot;
      out[i] = nir_variable_create(nir, nir_var_shader_out, type, "tir_out");
      out[i]->data.location = key->v[i].slot;
      out[i]->data.interpolation = key->v[i].interp;
      if (key->v[i].slot == VARYING_SLOT_POS)
         pos = (int)i;
   }
   if (pos < 0)
      return NULL;
   for (unsigned i = 0; i < DXBC_TIR_EDGE_SLOTS; ++i) {
      edge_out[i] = nir_variable_create(nir, nir_var_shader_out, glsl_ivec4_type(), "tir_edge");
      edge_out[i]->data.location = VARYING_SLOT_TEX0 + i;
      edge_out[i]->data.interpolation = INTERP_MODE_FLAT;
   }
   if (key->primitive_id) {
      primitive_out = nir_variable_create(nir, nir_var_shader_out, glsl_uint_type(), "tir_primid");
      primitive_out->data.location = VARYING_SLOT_PRIMITIVE_ID;
      primitive_out->data.interpolation = INTERP_MODE_FLAT;
   }

   nir_def *p[3], *w[3], *sx[3], *sy[3], *ix[3], *iy[3];
   nir_def *vp = nir_load_ubo(b, 4, 32, nir_imm_int(b, DXBC_TIR_GS_CB), nir_imm_int(b, 0),
                              .align_mul = 16, .align_offset = 0, .range_base = 0, .range = ~0u);
   nir_def *ok = nir_imm_true(b);
   for (unsigned k = 0; k < 3; ++k) {
      p[k] = nir_load_array_var_imm(b, in[pos], k);
      w[k] = nir_channel(b, p[k], 3);
      ok = nir_iand(b, ok, nir_fgt_imm(b, w[k], DXBC_TIR_MIN_W));
      nir_def *rw = nir_frcp(b, w[k]);
      sx[k] = nir_ffma(b, nir_fmul(b, nir_channel(b, p[k], 0), rw), nir_channel(b, vp, 0), nir_channel(b, vp, 2));
      sy[k] = nir_ffma(b, nir_fmul(b, nir_channel(b, p[k], 1), rw), nir_channel(b, vp, 1), nir_channel(b, vp, 3));
      ix[k] = nir_f2i32(b, nir_fround_even(b, nir_fmul_imm(b, sx[k], DXBC_TIR_SUBPIXEL)));
      iy[k] = nir_f2i32(b, nir_fround_even(b, nir_fmul_imm(b, sy[k], DXBC_TIR_SUBPIXEL)));
   }
   nir_def *area = nir_isub(b, nir_imul(b, nir_i2i64(b, nir_isub(b, ix[1], ix[0])), nir_i2i64(b, nir_isub(b, iy[2], iy[0]))),
                           nir_imul(b, nir_i2i64(b, nir_isub(b, iy[1], iy[0])), nir_i2i64(b, nir_isub(b, ix[2], ix[0]))));
   nir_def *positive = nir_igt_imm(b, area, 0);

   /* the oriented order: 0, 1, 2 for a positive area, else 0, 2, 1 */
   nir_def *ox[3] = {ix[0], nir_bcsel(b, positive, ix[1], ix[2]), nir_bcsel(b, positive, ix[2], ix[1])};
   nir_def *oy[3] = {iy[0], nir_bcsel(b, positive, iy[1], iy[2]), nir_bcsel(b, positive, iy[2], iy[1])};
   nir_def *fx[3] = {sx[0], nir_bcsel(b, positive, sx[1], sx[2]), nir_bcsel(b, positive, sx[2], sx[1])};
   nir_def *fy[3] = {sy[0], nir_bcsel(b, positive, sy[1], sy[2]), nir_bcsel(b, positive, sy[2], sy[1])};
   nir_def *edge[DXBC_TIR_EDGE_SLOTS], *bias[3];
   for (unsigned e = 0; e < 3; ++e) {
      nir_def *dx = nir_isub(b, ox[(e + 1) % 3], ox[e]), *dy = nir_isub(b, oy[(e + 1) % 3], oy[e]);
      nir_def *top_left = nir_ior(b, nir_ilt_imm(b, dy, 0), nir_iand(b, nir_ieq_imm(b, dy, 0), nir_igt_imm(b, dx, 0)));
      bias[e] = nir_bcsel(b, top_left, nir_imm_int(b, 0), nir_imm_int(b, -1));
      edge[e] = nir_vec4(b, ox[e], oy[e], nir_ineg(b, dy), dx);
   }
   edge[3] = nir_vec4(b, bias[0], bias[1], bias[2], nir_imm_int(b, 0));
   /* unexpanded (w <= 0): edge functions 0 cover every sample, and a
    * bounding box over the whole plane keeps every pixel conservative */
   nir_def *all = nir_imm_ivec4(b, 0, 0, 0, 0);
   nir_def *unexpanded[DXBC_TIR_EDGE_SLOTS] = {nir_imm_ivec4(b, -(1 << 30), -(1 << 30), 0, 0),
                                               nir_imm_ivec4(b, 1 << 30, 1 << 30, 0, 0), all, all};

   /* expanded oriented vertices: each the intersection of its two edges
    * moved outward by h = (1/2 + margin) (|dx| + |dy|) in edge-function units */
   nir_def *qx[3], *qy[3];
   for (unsigned j = 0; j < 3; ++j) {
      const unsigned ia = (j + 2) % 3, ib = j; /* the edges into and out of u_j */
      nir_def *dxa = nir_fsub(b, fx[j], fx[ia]), *dya = nir_fsub(b, fy[j], fy[ia]);
      nir_def *dxb = nir_fsub(b, fx[(j + 1) % 3], fx[j]), *dyb = nir_fsub(b, fy[(j + 1) % 3], fy[j]);
      nir_def *ha = nir_fmul_imm(b, nir_fadd(b, nir_fabs(b, dxa), nir_fabs(b, dya)), 0.5f + DXBC_TIR_MARGIN);
      nir_def *hb = nir_fmul_imm(b, nir_fadd(b, nir_fabs(b, dxb), nir_fabs(b, dyb)), 0.5f + DXBC_TIR_MARGIN);
      nir_def *rdet = nir_frcp(b, cross2(b, dxa, dya, dxb, dyb));
      qx[j] = nir_fadd(b, fx[j], nir_fmul(b, nir_fsub(b, nir_fmul(b, dxa, hb), nir_fmul(b, ha, dxb)), rdet));
      qy[j] = nir_fadd(b, fy[j], nir_fmul(b, nir_fsub(b, nir_fmul(b, dya, hb), nir_fmul(b, ha, dyb)), rdet));
   }
   /* back to the original order (the emitted winding stays the original's) */
   nir_def *ex[3] = {qx[0], nir_bcsel(b, positive, qx[1], qx[2]), nir_bcsel(b, positive, qx[2], qx[1])};
   nir_def *ey[3] = {qy[0], nir_bcsel(b, positive, qy[1], qy[2]), nir_bcsel(b, positive, qy[2], qy[1])};

   nir_def *rden = nir_frcp(b, cross2(b, nir_fsub(b, sx[1], sx[0]), nir_fsub(b, sy[1], sy[0]),
                                      nir_fsub(b, sx[2], sx[0]), nir_fsub(b, sy[2], sy[0])));
   nir_def *primitive_id = key->primitive_id ? nir_load_primitive_id(b) : NULL;

   nir_push_if(b, nir_ine_imm(b, area, 0));
   {
      for (unsigned k = 0; k < 3; ++k) {
         /* the barycentrics of the emitted vertex; the original vertex
          * itself without expansion (the w <= 0 fallback) */
         nir_def *px = nir_bcsel(b, ok, ex[k], sx[k]), *py = nir_bcsel(b, ok, ey[k], sy[k]);
         nir_def *dx0 = nir_fsub(b, px, sx[0]), *dy0 = nir_fsub(b, py, sy[0]);
         nir_def *l1 = nir_fmul(b, cross2(b, dx0, dy0, nir_fsub(b, sx[2], sx[0]), nir_fsub(b, sy[2], sy[0])), rden);
         nir_def *l2 = nir_fmul(b, cross2(b, nir_fsub(b, sx[1], sx[0]), nir_fsub(b, sy[1], sy[0]), dx0, dy0), rden);
         nir_def *l[3] = {nir_fsub(b, nir_fsub(b, nir_imm_float(b, 1.0f), l1), l2), l1, l2};
         nir_def *pl[3], *invw = nir_imm_float(b, 0.0f);
         for (unsigned i = 0; i < 3; ++i) {
            pl[i] = nir_fdiv(b, l[i], w[i]);
            invw = nir_fadd(b, invw, pl[i]);
         }
         nir_def *wn = nir_frcp(b, invw);
         for (unsigned v = 0; v < key->count; ++v) {
            nir_def *value;
            if ((int)v == pos) {
               nir_def *zn = nir_imm_float(b, 0.0f);
               for (unsigned i = 0; i < 3; ++i)
                  zn = nir_fadd(b, zn, nir_fmul(b, l[i], nir_fdiv(b, nir_channel(b, p[i], 2), w[i])));
               nir_def *nx = nir_fdiv(b, nir_fsub(b, px, nir_channel(b, vp, 2)), nir_channel(b, vp, 0));
               nir_def *ny = nir_fdiv(b, nir_fsub(b, py, nir_channel(b, vp, 3)), nir_channel(b, vp, 1));
               value = nir_vec4(b, nir_fmul(b, nx, wn), nir_fmul(b, ny, wn), nir_fmul(b, zn, wn), wn);
               /* unexpanded: the original position exactly */
               value = nir_bcsel(b, ok, value, p[k]);
            } else if (key->v[v].interp == INTERP_MODE_FLAT) {
               value = nir_load_array_var_imm(b, in[v], 0);
            } else {
               const bool linear = key->v[v].interp == INTERP_MODE_NOPERSPECTIVE;
               value = NULL;
               for (unsigned i = 0; i < 3; ++i) {
                  nir_def *term = nir_fmul(b, nir_load_array_var_imm(b, in[v], i), linear ? l[i] : pl[i]);
                  value = value ? nir_fadd(b, value, term) : term;
               }
               if (!linear)
                  value = nir_fmul(b, value, wn);
               value = nir_bcsel(b, ok, value, nir_load_array_var_imm(b, in[v], k));
            }
            nir_store_var(b, out[v], value, nir_component_mask(key->v[v].components));
         }
         for (unsigned i = 0; i < DXBC_TIR_EDGE_SLOTS; ++i)
            nir_store_var(b, edge_out[i], nir_bcsel(b, ok, edge[i], unexpanded[i]), 0xf);
         if (primitive_out)
            nir_store_var(b, primitive_out, primitive_id, 1);
         nir_emit_vertex(b, .stream_id = 0);
      }
      nir_end_primitive(b, .stream_id = 0);
   }
   nir_pop_if(b, NULL);

   nir_validate_shader(nir, "dxbc_tir_gs");
   return nir;
}
