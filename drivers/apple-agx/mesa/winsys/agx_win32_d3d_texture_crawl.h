/* RC78: texture descriptor crawls of a D3D shader may run speculatively.
 *
 * Asahi reads a texture's descriptor from the shader for a size query, for
 * the layer clamp of every array-texture sample (agx_nir_lower_texture.c
 * lower_regular_texture: the layer is clamped to [0, layers - 1], as D3D
 * and GL require), for txf robustness and for image sizes: libagx loads
 * (load_global_constant) at load_from_texture_handle_agx(handle) plus a
 * byte offset inside the descriptor.  For a texture or image the shader
 * binds, Gallium's agx_nir_lower_bindings makes the handle
 * load_texture_handle_agx(index), an index into the per-draw descriptor
 * table batch->stage_uniforms[stage].texture_base (agx_upload_textures):
 * nr_bindful_textures + 2 * nr_bindful_images descriptors of
 * AGX_WIN32_TEXTURE_DESCRIPTOR_BYTES each, every one written (a null
 * descriptor for an unbound slot), in batch pool memory that stays mapped
 * while the batch runs and that nothing writes during it.
 *
 * Asahi lets these loads move only when the device has soft faults
 * (agx_optimize_nir set_speculate); the Windows backend enables none
 * (agx_win32_asahi_scene.c: params.features = 0), so nir_opt_preamble
 * leaves every crawl under divergent control flow in the shader, where each
 * one costs device loads, waits and ~20 ALU instructions per sample:
 * GravityMark's lighting kernel 806ee7b1bdb1944a samples its shadow-map
 * array in 30 divergent branches (EXP2008: 84% of the frame's compute).
 *
 * A crawl load whose descriptor index is provably below the table size and
 * whose bytes lie inside one descriptor reads mapped memory that is constant
 * for the dispatch, so running it in the preamble, or on a path the shader
 * would not take, returns the value the shader would read and cannot fault:
 * mark it ACCESS_CAN_SPECULATE.  Descriptor-heap handles (the D3D12 heap
 * mirror, DXBC_HEAP_HANDLE_MARK, a bindless_image_agx of an app index) and
 * crawls whose index bound is not proven are left alone.
 * APPLE_AGX_D3D_CRAWL_SPECULATE=0 (read when a shader is compiled) keeps
 * Asahi's placement, for a hardware comparison.
 */
#ifndef AGX_WIN32_D3D_TEXTURE_CRAWL_H
#define AGX_WIN32_D3D_TEXTURE_CRAWL_H

#include <stdlib.h>

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"
#include "util/hash_table.h"

/* AGX_TEXTURE_LENGTH, Gallium's AGX_TEXTURE_DESC_STRIDE */
#define AGX_WIN32_TEXTURE_DESCRIPTOR_BYTES 24u

struct agx_win32_crawl_state {
   unsigned descriptors;
   struct hash_table *range;
};

static inline bool
agx_win32_speculate_texture_crawl_instr(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   struct agx_win32_crawl_state *state = (struct agx_win32_crawl_state *)data;
   if (intr->intrinsic != nir_intrinsic_load_global_constant ||
       (nir_intrinsic_access(intr) & ACCESS_CAN_SPECULATE))
      return false;
   /* descriptor address + constant byte offset */
   nir_scalar address = nir_scalar_resolved(intr->src[0].ssa, 0);
   uint64_t offset = 0;
   while (nir_scalar_is_alu(address) && nir_scalar_alu_op(address) == nir_op_iadd) {
      nir_scalar a = nir_scalar_chase_alu_src(address, 0);
      nir_scalar c = nir_scalar_chase_alu_src(address, 1);
      if (nir_scalar_is_const(a)) {
         nir_scalar t = a;
         a = c;
         c = t;
      }
      if (!nir_scalar_is_const(c) || nir_scalar_as_uint(c) >= AGX_WIN32_TEXTURE_DESCRIPTOR_BYTES)
         return false;
      offset += nir_scalar_as_uint(c);
      address = a;
   }
   const uint64_t bytes = (uint64_t)intr->def.num_components * intr->def.bit_size / 8u;
   if (!nir_scalar_is_intrinsic(address) ||
       nir_scalar_intrinsic_op(address) != nir_intrinsic_load_from_texture_handle_agx ||
       offset + bytes > AGX_WIN32_TEXTURE_DESCRIPTOR_BYTES)
      return false;
   /* a handle into the per-draw descriptor table, at a bounded index */
   nir_intrinsic_instr *from = nir_def_as_intrinsic(address.def);
   nir_intrinsic_instr *handle = nir_src_as_intrinsic(from->src[0]);
   if (!handle || handle->intrinsic != nir_intrinsic_load_texture_handle_agx)
      return false;
   const uint32_t bound = nir_unsigned_upper_bound(b->shader, state->range,
                                                   nir_scalar_resolved(handle->src[0].ssa, 0));
   if (bound >= state->descriptors)
      return false;
   nir_intrinsic_set_access(intr, (enum gl_access_qualifier)(nir_intrinsic_access(intr) | ACCESS_CAN_SPECULATE));
   return true;
}

/* after agx_nir_lower_texture (which emits the crawls) in
 * agx_shader_initialize; descriptors = nr_bindful_textures +
 * 2 * nr_bindful_images, the table agx_upload_textures fills */
static inline bool
agx_win32_speculate_texture_crawls(nir_shader *nir, unsigned descriptors)
{
   const char *env = getenv("APPLE_AGX_D3D_CRAWL_SPECULATE");
   if (!descriptors || (env && env[0] == '0' && !env[1]))
      return false;
   struct agx_win32_crawl_state state = {descriptors, _mesa_pointer_hash_table_create(NULL)};
   const bool progress = nir_shader_intrinsics_pass(nir, agx_win32_speculate_texture_crawl_instr,
                                                    nir_metadata_all, &state);
   _mesa_hash_table_destroy(state.range, NULL);
   return progress;
}

/* T1: the array-layer clamp of a D3D sample without libagx_txs.
 *
 * Asahi clamps the layer of every array sample (agx_nir_lower_texture.c
 * lower_regular_texture, not txf, not lod) to nr_layers - 1, nr_layers being
 * the layer component of nir_get_texture_size, which lower_tex_crawl turns
 * into libagx_txs (libagx/texture.cl): four descriptor loads and ~15 integer
 * operations per sample (null check on the 64-bit address, linear-2D-array
 * select, + 1, then - 1), GravityMark G-buffer PS 9fa121805cf5ea58 runs it
 * for each of its three heap-indexed samples.  For the descriptor d (genxml
 * Texture: Depth bits 110..123, layers - 1; Layout bits 4..5, Linear = 0;
 * Depth (linear) bits 128..138) libagx_txs's layer count is, for
 * d.address != AGX_ZERO_PAGE_ADDRESS,
 *    (2D sampler && d.layout == Linear ? d.depth_linear : d.depth)
 * (1D arrays read size.z, cube arrays component 2 = depth; "do not adjust
 * array size" for the LOD), so max_layer = that field's encoded value: the
 * same number, with no + 1 / - 1 and no address test.
 *
 * Null descriptors (address == AGX_ZERO_PAGE_ADDRESS): libagx returns 0
 * layers, Asahi's clamp becomes umin(layer, ~0u), no clamp; here the layer
 * is clamped to the encoded field.  The only writer of that address into a
 * texture descriptor is agx_set_null_texture (agx_helpers.h; Gallium's
 * unbound slots, the D3D12 heap's null and invalid SRVs through
 * AgxWin32AsahiNullTextureDescriptor), whose Depth field encodes 1 layer and
 * whose four swizzles are AGX_CHANNEL_0: the sample returns (0,0,0,0) for
 * every layer, the D3D null-SRV result, and the clamped read stays at the
 * first layer of the zero page.  On Windows AGX_ZERO_PAGE_ADDRESS is the
 * device's zero page VA (EXP1069) while the libagx NIR compares the address
 * with 1 << 32, so Asahi's null branch does not fire there and both forms
 * already clamp a null descriptor to layer 0.
 *
 * Linear layouts are kept exact rather than excluded: D3D11 present-bindable
 * textures (PIPE_BIND_DISPLAY_TARGET, d3d10umd translate_resource_flags) and
 * the D3D12 imported presentation surface (AgxWin32AsahiImportLinearColor32)
 * are linear, and agx_pack_texture packs a linear 1D/2D array view with
 * depth_linear (its Depth bits are the Stride field).
 *
 * The layer is computed as Asahi computes it, umin(f2u32(fmax(rne(l), 0)),
 * max_layer), and passed back as u2f32 of it with AGX_TEXTURE_FLAG_NO_CLAMP:
 * lower_regular_texture then applies rne/fmax/f2u32 to an integral,
 * non-negative value <= 16383 (exact in fp32), which returns it unchanged.
 * Only samples with a divergent descriptor handle (a D3D12 heap index from
 * per-thread data) are rewritten: with a uniform handle the whole clamp is
 * uniform and Asahi's form already runs once in the preamble; rewriting it
 * there only perturbed register allocation (CS 144a46de +53 instructions).
 * APPLE_AGX_D3D_MAX_LAYER=1 (read when a shader is compiled) enables it;
 * default off (Asahi's libagx_txs clamp). */
#include "asahi/compiler/agx_nir_texture.h"

static inline bool
agx_win32_d3d_max_layer_enabled(void)
{
   const char *env = getenv("APPLE_AGX_D3D_MAX_LAYER");
   return env && env[0] == '1' && !env[1];
}

static inline nir_def *
agx_win32_descriptor_word(nir_builder *b, nir_def *descriptor, unsigned word)
{
   return nir_load_global_constant(b, 1, 32, nir_iadd_imm(b, descriptor, 4 * word), .align_mul = 4);
}

/* the encoded layers - 1 of the descriptor of tex (see above) */
static inline nir_def *
agx_win32_d3d_max_layer(nir_builder *b, nir_tex_instr *tex, nir_def *handle)
{
   nir_def *descriptor = nir_load_from_texture_handle_agx(b, handle);
   nir_def *depth = nir_ubitfield_extract_imm(b, agx_win32_descriptor_word(b, descriptor, 3), 14, 14);
   if (tex->sampler_dim != GLSL_SAMPLER_DIM_2D)
      return depth;
   nir_def *linear = nir_ieq_imm(b, nir_iand_imm(b, agx_win32_descriptor_word(b, descriptor, 0), 0x30), 0);
   nir_def *depth_linear = nir_iand_imm(b, agx_win32_descriptor_word(b, descriptor, 4), 0x7ff);
   return nir_bcsel(b, linear, depth_linear, depth);
}

static inline bool
agx_win32_lower_d3d_layer_clamp_instr(nir_builder *b, nir_instr *instr, void *data)
{
   (void)data;
   if (instr->type != nir_instr_type_tex)
      return false;
   nir_tex_instr *tex = nir_instr_as_tex(instr);
   /* the samples lower_regular_texture clamps with nir_get_texture_size */
   if (!tex->is_array || nir_tex_instr_is_query(tex) || tex->op == nir_texop_lod ||
       tex->op == nir_texop_txf || tex->op == nir_texop_txf_ms ||
       tex->sampler_dim == GLSL_SAMPLER_DIM_BUF || (tex->backend_flags & AGX_TEXTURE_FLAG_NO_CLAMP) ||
       nir_tex_instr_src_index(tex, nir_tex_src_backend1) >= 0)
      return false;
   const int coord_idx = nir_tex_instr_src_index(tex, nir_tex_src_coord);
   const int handle_idx = nir_tex_instr_src_index(tex, nir_tex_src_texture_handle);
   if (coord_idx < 0 || handle_idx < 0)
      return false;
   /* a uniform handle's clamp is uniform: Asahi's form already runs once,
    * in the preamble (nir_opt_preamble) */
   if (!tex->src[handle_idx].src.ssa->divergent)
      return false;
   nir_def *coord = tex->src[coord_idx].src.ssa;
   if (coord->bit_size != 32 || nir_tex_instr_src_type(tex, coord_idx) != nir_type_float)
      return false;
   b->cursor = nir_before_instr(instr);
   const unsigned lidx = coord->num_components - 1;
   nir_def *layer = nir_f2u32(b, nir_fmax(b, nir_fround_even(b, nir_channel(b, coord, lidx)), nir_imm_float(b, 0.0f)));
   layer = nir_umin(b, layer, agx_win32_d3d_max_layer(b, tex, tex->src[handle_idx].src.ssa));
   nir_src_rewrite(&tex->src[coord_idx].src, nir_vector_insert_imm(b, coord, nir_u2f32(b, layer), lidx));
   tex->backend_flags |= AGX_TEXTURE_FLAG_NO_CLAMP;
   return true;
}

/* right before agx_nir_lower_texture (D3D contexts) */
static inline bool
agx_win32_lower_d3d_layer_clamp(nir_shader *nir)
{
   if (!agx_win32_d3d_max_layer_enabled())
      return false;
   nir_divergence_analysis(nir);
   return nir_shader_instructions_pass(nir, agx_win32_lower_d3d_layer_clamp_instr, nir_metadata_control_flow, NULL);
}

/* T5b step A: a crawl does not need the bindless form of its sample.
 *
 * Gallium's agx_nir_lower_bindings keeps a texture in its texture state
 * register (register i == descriptor-table entry i, loaded by the stage's
 * USC TEXTURE word for i < 16) unless the sample needs a descriptor crawl
 * (agx_nir_needs_texture_crawl: every array sample's layer clamp, txf
 * robustness), and then gives it the handle load_texture_handle_agx(i), an
 * index into the same table, because agx_nir_lower_texture builds the crawl
 * from that handle.  Once agx_nir_lower_texture has run, the crawl loads
 * hold their own copy of the handle; the sample itself can take the
 * register again: for i < 16 and i < the shader's texture count (the
 * table's texture entries, all loaded into registers 0..15) register i
 * holds exactly the descriptor the handle addresses.  Images, D3D12 heap
 * handles (bindless_image_agx, DXBC_HEAP_HANDLE_MARK), dynamic indices,
 * entries >= 16 and queries keep the bindless form.  rewritten counts the
 * samples moved (compile receipt).  APPLE_AGX_D3D12_CRAWL_BINDFUL=1 (the
 * D3D12 owner's backend flag D3dCrawlBindful) runs it right after
 * agx_nir_lower_texture; default off. */
struct agx_win32_crawl_bindful_state {
   unsigned textures;
   unsigned *rewritten;
};

/* the constant table index of a binding-pass handle: c, or the clamped
 * umin(c, n - 1) of agx_nir_lower_bindings when it is not folded yet */
static inline bool
agx_win32_crawl_bindful_index(nir_def *index, unsigned *out)
{
   nir_scalar s = nir_scalar_resolved(index, 0);
   if (nir_scalar_is_const(s)) {
      const uint64_t c = nir_scalar_as_uint(s);
      if (c > 0xffffu)
         return false;
      *out = (unsigned)c;
      return true;
   }
   if (!nir_scalar_is_alu(s) || nir_scalar_alu_op(s) != nir_op_umin)
      return false;
   nir_scalar a = nir_scalar_chase_alu_src(s, 0), c = nir_scalar_chase_alu_src(s, 1);
   if (!nir_scalar_is_const(a) || !nir_scalar_is_const(c))
      return false;
   const uint64_t x = nir_scalar_as_uint(a), y = nir_scalar_as_uint(c);
   const uint64_t m = x < y ? x : y;
   if (m > 0xffffu)
      return false;
   *out = (unsigned)m;
   return true;
}

static inline bool
agx_win32_crawl_bindful_instr(nir_builder *b, nir_tex_instr *tex, void *data)
{
   (void)b;
   struct agx_win32_crawl_bindful_state *state = (struct agx_win32_crawl_bindful_state *)data;
   const int h = nir_tex_instr_src_index(tex, nir_tex_src_texture_handle);
   if (h < 0 || nir_tex_instr_is_query(tex))
      return false;
   nir_intrinsic_instr *handle = nir_src_as_intrinsic(tex->src[h].src);
   unsigned index = 0;
   if (!handle || handle->intrinsic != nir_intrinsic_load_texture_handle_agx ||
       !agx_win32_crawl_bindful_index(handle->src[0].ssa, &index) || index >= 16u ||
       index >= state->textures)
      return false;
   nir_tex_instr_remove_src(tex, h);
   tex->texture_index = index;
   if (state->rewritten)
      ++*state->rewritten;
   return true;
}

/* after agx_nir_lower_texture; textures = so->info.nr_bindful_textures
 * (BITSET_LAST_BIT(textures_used) as the binding pass saw it) */
static inline bool
agx_win32_d3d_crawl_bindful(nir_shader *nir, unsigned textures, unsigned *rewritten)
{
   struct agx_win32_crawl_bindful_state state = {textures, rewritten};
   return nir_shader_tex_pass(nir, agx_win32_crawl_bindful_instr, nir_metadata_control_flow, &state);
}

/* M3: Asahi never runs nir_opt_licm, so a texture descriptor crawl with a
 * loop-invariant handle (load_global_constant, CAN_REORDER by definition)
 * runs on every iteration of the loop around its sample (GravityMark planet
 * PS 428487da: the layer-clamp crawl inside its ray-march loops, 2 loads and
 * 3 waits of a 67-instruction body; the macOS compile has none in the loop).
 * Non-speculative LICM moves only instructions of blocks that dominate the
 * loop exit, whose sources are defined before the loop.
 * APPLE_AGX_D3D_LICM=0 keeps Asahi's placement (measurement control). */
static inline bool
agx_win32_hoist_loop_invariants(nir_shader *nir)
{
   const char *env = getenv("APPLE_AGX_D3D_LICM");
   if (env && env[0] == '0' && !env[1])
      return false;
   return nir_opt_licm(nir, NULL);
}

#endif
