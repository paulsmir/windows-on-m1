/* C65: min/max reduction filtering in the shader (dxbc_minmax.h).
 *
 * D3D11.3 functional spec, Minimum/Maximum filtering: the filter "fetches
 * the same set of texels that a normal texture filter would fetch" and
 * returns their component-wise min or max; texels with zero weight do not
 * contribute. Each participating texel is sampled on its own: an explicit-
 * LOD sample (txl) at the texel's centre and an integer level returns that
 * texel exactly (bilinear weight 1, mip weight 0), with the sampler's own
 * wrap modes and border colour -- so no addressing is emulated here. */
#include "dxbc_minmax.h"
#include "util/u_dynarray.h"

static bool
is_handle(nir_tex_src_type type)
{
   return type == nir_tex_src_texture_deref || type == nir_tex_src_sampler_deref ||
          type == nir_tex_src_texture_offset || type == nir_tex_src_sampler_offset ||
          type == nir_tex_src_texture_handle || type == nir_tex_src_sampler_handle;
}

/* a texture instruction on tex's texture and sampler with extra sources
 * still to be filled from index *first */
static nir_tex_instr *
derived(nir_builder *b, const nir_tex_instr *tex, nir_texop op, unsigned extra, unsigned *first)
{
   unsigned handles = 0, n = 0;
   for (unsigned i = 0; i < tex->num_srcs; ++i)
      handles += is_handle(tex->src[i].src_type);
   nir_tex_instr *t = nir_tex_instr_create(b->shader, handles + extra);
   t->op = op;
   t->sampler_dim = tex->sampler_dim;
   t->is_array = tex->is_array;
   t->texture_index = tex->texture_index;
   t->sampler_index = tex->sampler_index;
   t->dest_type = tex->dest_type;
   for (unsigned i = 0; i < tex->num_srcs; ++i)
      if (is_handle(tex->src[i].src_type))
         t->src[n++] = nir_tex_src_for_ssa(tex->src[i].src_type, tex->src[i].src.ssa);
   *first = n;
   return t;
}

static nir_def *
finish(nir_builder *b, nir_tex_instr *t, unsigned components)
{
   nir_def_init(&t->instr, &t->def, components, t->op == nir_texop_lod ? 32 : nir_alu_type_get_type_size(t->dest_type));
   nir_builder_instr_insert(b, &t->instr);
   return &t->def;
}

/* the space the footprint is taken in: normalized (u, v[, w]) of a 2D or 3D
 * texture, or for a cube the (s, t) of the face its direction selects,
 * which texel directions are rebuilt from */
struct space {
   unsigned dims;     /* footprint axes: 2, or 3 for a volume */
   nir_def *uv;       /* dims components */
   nir_def *layer;    /* array layer, or NULL */
   bool cube;
   nir_def *is_x, *is_y, *sign; /* cube: the major axis and its sign */
};

/* the level's size in texels along the footprint axes, as floats */
static nir_def *
level_size(nir_builder *b, const nir_tex_instr *tex, unsigned dims, nir_def *level)
{
   unsigned n;
   nir_tex_instr *t = derived(b, tex, nir_texop_txs, 1, &n);
   t->dest_type = nir_type_int32;
   t->coord_components = 0;
   t->src[n] = nir_tex_src_for_ssa(nir_tex_src_lod, nir_f2i32(b, level));
   const unsigned components = tex->sampler_dim == GLSL_SAMPLER_DIM_3D ? 3 : 2 + (tex->is_array ? 1 : 0);
   nir_def *size = finish(b, t, components);
   return nir_i2f32(b, nir_trim_vector(b, size, dims));
}

static nir_def *
level_count(nir_builder *b, const nir_tex_instr *tex)
{
   unsigned n;
   nir_tex_instr *t = derived(b, tex, nir_texop_query_levels, 0, &n);
   t->dest_type = nir_type_int32;
   t->coord_components = 0;
   return finish(b, t, 1);
}

/* D3D/GL cube face selection: the major axis, and (s, t) on its face */
static nir_def *
cube_face(nir_builder *b, nir_def *dir, struct space *sp)
{
   nir_def *x = nir_channel(b, dir, 0), *y = nir_channel(b, dir, 1), *z = nir_channel(b, dir, 2);
   nir_def *ax = nir_fabs(b, x), *ay = nir_fabs(b, y), *az = nir_fabs(b, z);
   sp->is_x = nir_iand(b, nir_fge(b, ax, ay), nir_fge(b, ax, az));
   sp->is_y = nir_iand(b, nir_inot(b, sp->is_x), nir_fge(b, ay, az));
   nir_def *major = nir_bcsel(b, sp->is_x, x, nir_bcsel(b, sp->is_y, y, z));
   sp->sign = nir_bcsel(b, nir_flt_imm(b, major, 0.0f), nir_imm_float(b, -1.0f), nir_imm_float(b, 1.0f));
   /* +X: (-z, -y), -X: (z, -y); +Y: (x, z), -Y: (x, -z); +Z: (x, -y), -Z: (-x, -y) */
   nir_def *sc = nir_bcsel(b, sp->is_x, nir_fmul(b, nir_fneg(b, sp->sign), z),
                           nir_bcsel(b, sp->is_y, x, nir_fmul(b, sp->sign, x)));
   nir_def *tc = nir_bcsel(b, sp->is_y, nir_fmul(b, sp->sign, z), nir_fneg(b, y));
   nir_def *inv = nir_frcp(b, nir_fabs(b, major));
   return nir_fadd_imm(b, nir_fmul_imm(b, nir_fmul(b, nir_vec2(b, sc, tc), inv), 0.5f), 0.5f);
}

/* the sample coordinate of a point of the footprint space; for a cube the
 * direction to (s, t) of the selected face -- one past an edge points into
 * the adjacent face, whose texel D3D's seamless filtering takes */
static nir_def *
coord_of(nir_builder *b, const struct space *sp, nir_def *uv)
{
   nir_def *c = uv;
   if (sp->cube) {
      nir_def *sc = nir_fadd_imm(b, nir_fmul_imm(b, nir_channel(b, uv, 0), 2.0f), -1.0f);
      nir_def *tc = nir_fadd_imm(b, nir_fmul_imm(b, nir_channel(b, uv, 1), 2.0f), -1.0f);
      nir_def *one = nir_imm_float(b, 1.0f);
      nir_def *fx = nir_vec3(b, sp->sign, nir_fneg(b, tc), nir_fneg(b, nir_fmul(b, sp->sign, sc)));
      nir_def *fy = nir_vec3(b, sc, sp->sign, nir_fmul(b, sp->sign, tc));
      nir_def *fz = nir_vec3(b, nir_fmul(b, sp->sign, sc), nir_fneg(b, tc), nir_fmul(b, sp->sign, one));
      c = nir_bcsel(b, sp->is_x, fx, nir_bcsel(b, sp->is_y, fy, fz));
   }
   if (!sp->layer)
      return c;
   nir_def *comps[4];
   for (unsigned i = 0; i < c->num_components; ++i)
      comps[i] = nir_channel(b, c, i);
   comps[c->num_components] = sp->layer;
   return nir_vec(b, comps, c->num_components + 1);
}

/* the texel nearest coord at an integer level (an explicit-LOD sample) */
static nir_def *
sample_at(nir_builder *b, const nir_tex_instr *tex, nir_def *coord, nir_def *level)
{
   unsigned n;
   nir_tex_instr *t = derived(b, tex, nir_texop_txl, 2, &n);
   t->coord_components = tex->coord_components;
   t->src[n] = nir_tex_src_for_ssa(nir_tex_src_coord, coord);
   t->src[n + 1] = nir_tex_src_for_ssa(nir_tex_src_lod, level);
   return finish(b, t, tex->def.num_components);
}

static nir_def *
reduce(nir_builder *b, nir_def *minimum, nir_def *a, nir_def *c)
{
   return nir_bcsel(b, minimum, nir_fmin(b, a, c), nir_fmax(b, a, c));
}

/* one level's participating texels (2x2, or 2x2x2 for a volume), reduced */
static nir_def *
footprint(nir_builder *b, const nir_tex_instr *tex, const struct space *sp, nir_def *coord, nir_def *level,
          nir_def *linear, nir_def *minimum)
{
   nir_def *size = level_size(b, tex, sp->dims, level);
   nir_def *p = nir_fadd_imm(b, nir_fmul(b, sp->uv, size), -0.5f);
   nir_def *i0 = nir_ffloor(b, p);
   nir_def *f = nir_fsub(b, p, i0);
   nir_def *inv = nir_frcp(b, size);
   nir_def *c0 = nir_fmul(b, nir_fadd_imm(b, i0, 0.5f), inv);
   nir_def *s0 = sample_at(b, tex, coord_of(b, sp, c0), level);
   nir_def *v = s0;
   for (unsigned corner = 1; corner < (1u << sp->dims); ++corner) {
      nir_def *at[3], *take = nir_imm_true(b);
      for (unsigned a = 0; a < sp->dims; ++a) {
         at[a] = nir_channel(b, c0, a);
         if (corner & (1u << a)) {
            at[a] = nir_fadd(b, at[a], nir_channel(b, inv, a));
            /* a texel whose weight along this axis is 0 does not take part */
            take = nir_iand(b, take, nir_flt(b, nir_imm_float(b, 0.0f), nir_channel(b, f, a)));
         }
      }
      nir_def *s = sample_at(b, tex, coord_of(b, sp, nir_vec(b, at, sp->dims)), level);
      v = reduce(b, minimum, v, nir_bcsel(b, take, s, s0));
   }
   /* point filtering: the one nearest texel, which the hardware picks */
   return nir_bcsel(b, linear, v, sample_at(b, tex, coord, level));
}

nir_def *
dxbc_minmax_wrap(nir_builder *b, nir_tex_instr *tex, nir_def *minmax)
{
   const int ci = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   const bool cube = tex->sampler_dim == GLSL_SAMPLER_DIM_CUBE, volume = tex->sampler_dim == GLSL_SAMPLER_DIM_3D;
   if ((tex->sampler_dim != GLSL_SAMPLER_DIM_2D && !cube && !volume) || tex->is_shadow || tex->is_sparse || ci < 0 ||
       nir_alu_type_get_base_type(tex->dest_type) != nir_type_float ||
       (tex->op != nir_texop_tex && tex->op != nir_texop_txb && tex->op != nir_texop_txl &&
        tex->op != nir_texop_txd) ||
       (cube && tex->op == nir_texop_txd) ||
       nir_tex_instr_src_index(tex, nir_tex_src_offset) >= 0 ||
       nir_tex_instr_src_index(tex, nir_tex_src_min_lod) >= 0)
      return NULL;
   b->cursor = nir_after_instr(&tex->instr);
   nir_def *coord = tex->src[ci].src.ssa;
   nir_def *reduction = nir_iand_imm(b, minmax, 3);
   nir_if *nif = nir_push_if(b, nir_uge_imm(b, reduction, 2));
   nir_def *minimum = nir_ieq_imm(b, reduction, 2);
   /* the coordinates without the array layer */
   const unsigned spatial = cube || volume ? 3 : 2;
   nir_def *where = nir_trim_vector(b, coord, spatial);
   struct space sp = {volume ? 3u : 2u, where, tex->is_array ? nir_channel(b, coord, spatial) : NULL, cube};
   if (cube)
      sp.uv = cube_face(b, where, &sp);
   nir_def *lod;
   if (tex->op == nir_texop_txl) {
      lod = tex->src[nir_tex_instr_src_index(tex, nir_tex_src_lod)].src.ssa;
   } else if (tex->op == nir_texop_txd) {
      /* the D3D LOD of the gradients (scaled by the bias already) */
      nir_def *size = level_size(b, tex, sp.dims, nir_imm_float(b, 0.0f));
      nir_def *ddx = nir_fmul(b, tex->src[nir_tex_instr_src_index(tex, nir_tex_src_ddx)].src.ssa, size);
      nir_def *ddy = nir_fmul(b, tex->src[nir_tex_instr_src_index(tex, nir_tex_src_ddy)].src.ssa, size);
      lod = nir_fmul_imm(b, nir_flog2(b, nir_fmax(b, nir_fdot(b, ddx, ddx), nir_fdot(b, ddy, ddy))), 0.5f);
   } else {
      /* the hardware LOD (sampler clamps applied), with the sample's bias */
      const int bi = nir_tex_instr_src_index(tex, nir_tex_src_bias);
      unsigned n;
      nir_tex_instr *q = derived(b, tex, nir_texop_lod, bi >= 0 ? 2 : 1, &n);
      q->dest_type = nir_type_float32;
      q->is_array = false; /* the lod query ignores the array index */
      q->coord_components = spatial;
      q->src[n] = nir_tex_src_for_ssa(nir_tex_src_coord, where);
      if (bi >= 0) q->src[n + 1] = nir_tex_src_for_ssa(nir_tex_src_bias, tex->src[bi].src.ssa);
      lod = nir_channel(b, finish(b, q, 2), 0);
   }
   nir_def *max_level = nir_i2f32(b, nir_iadd_imm(b, level_count(b, tex), -1));
   nir_def *mip_linear = nir_ine_imm(b, nir_iand_imm(b, minmax, 16), 0);
   nir_def *linear = nir_bcsel(b, nir_fle_imm(b, lod, 0.0f), nir_ine_imm(b, nir_iand_imm(b, minmax, 8), 0),
                               nir_ine_imm(b, nir_iand_imm(b, minmax, 4), 0));
   nir_def *clamped = nir_fmin(b, nir_fmax(b, lod, nir_imm_float(b, 0.0f)), max_level);
   /* point mip: the nearest level; linear mip: the two around it, the
    * second only with a nonzero weight */
   nir_def *l0 = nir_bcsel(b, mip_linear, nir_ffloor(b, clamped),
                           nir_fmin(b, nir_ffloor(b, nir_fadd_imm(b, clamped, 0.5f)), max_level));
   nir_def *l1 = nir_fmin(b, nir_fadd_imm(b, l0, 1.0f), max_level);
   nir_def *second = nir_iand(b, mip_linear, nir_flt(b, nir_imm_float(b, 0.0f), nir_fsub(b, clamped, l0)));
   nir_def *v0 = footprint(b, tex, &sp, coord, l0, linear, minimum);
   nir_def *v1 = footprint(b, tex, &sp, coord, l1, linear, minimum);
   nir_def *value = nir_bcsel(b, second, reduce(b, minimum, v0, v1), v0);
   nir_push_else(b, nif);
   nir_pop_if(b, nif);
   nir_def *phi = nir_if_phi(b, value, &tex->def);
   /* every use but the phi's own reads the choice (the phi is in the block
    * after the if, so nir_def_rewrite_uses_after does not apply) */
   nir_foreach_use_including_if_safe(src, &tex->def) {
      if (!nir_src_is_if(src) && nir_src_use_instr(src) == nir_def_instr(phi)) continue;
      nir_src_rewrite(src, phi);
   }
   return phi;
}

static bool
slot_sampled(const nir_tex_instr *tex, const unsigned char *bits)
{
   for (unsigned i = 0; i < tex->num_srcs; ++i)
      if (tex->src[i].src_type == nir_tex_src_sampler_handle || tex->src[i].src_type == nir_tex_src_sampler_deref ||
          tex->src[i].src_type == nir_tex_src_sampler_offset)
         return false;
   return tex->sampler_index < DXBC_MINMAX_SLOTS && (bits[tex->sampler_index] & 3u) >= 2u;
}

bool
dxbc_minmax_wrap_slots(nir_shader *shader, const unsigned char bits[DXBC_MINMAX_SLOTS])
{
   bool any = false;
   for (unsigned s = 0; s < DXBC_MINMAX_SLOTS; ++s)
      any |= (bits[s] & 3u) >= 2u;
   if (!any)
      return false;
   bool wrapped = false;
   nir_foreach_function_impl(impl, shader) {
      /* collected first: wrapping adds blocks */
      struct util_dynarray texs;
      util_dynarray_init(&texs, NULL);
      nir_foreach_block(block, impl)
         nir_foreach_instr(instr, block)
            if (instr->type == nir_instr_type_tex && slot_sampled(nir_instr_as_tex(instr), bits))
               util_dynarray_append(&texs, nir_instr_as_tex(instr));
      bool progress = false;
      nir_builder b = nir_builder_create(impl);
      util_dynarray_foreach(&texs, nir_tex_instr *, t) {
         b.cursor = nir_before_instr(&(*t)->instr);
         nir_def *minmax = nir_imm_int(&b, bits[(*t)->sampler_index]);
         progress |= dxbc_minmax_wrap(&b, *t, minmax) != NULL;
      }
      util_dynarray_fini(&texs);
      nir_progress(progress, impl, nir_metadata_none);
      wrapped |= progress;
   }
   return wrapped;
}
