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
