/* D3D-H6: D3D10/11 out-of-bounds semantics for constant-buffer and UAV
 * buffer accesses of the Asahi Gallium driver
 * (investigation/analysis/D3D-H6-buffer-robustness.md).
 *
 * Microsoft's rules (D3D11.3 functional spec 7.5; Shader Model 5 assembly
 * reference: ld_raw, ld_structured, store_raw, store_structured, atomics):
 *  - a constant-buffer read past the bound size, or from a slot with
 *    nothing bound, returns 0 in all components;
 *  - ld_raw / ld_structured on a UAV return 0 for each out-of-bounds 32-bit
 *    component and read the in-bounds ones;
 *  - store_raw / store_structured write nothing out of bounds and write the
 *    in-bounds components;
 *  - an out-of-bounds atomic writes nothing (its result is undefined).
 * Asahi has only GL robustness (nir_lower_robust_access): an out-of-bounds
 * load reads offset 0, i.e. the buffer's first element, and a store whose
 * last component is out of bounds is dropped whole.  For a D3D context this
 * pass lowers UBO/SSBO loads and SSBO stores instead; atomics and buffer
 * images keep Asahi's lowering (nothing written, result 0).
 *
 * Runs on the shader's early NIR, before agx_nir_lower_bindings; the bound
 * sizes come from get_ubo_size / get_ssbo_size (Asahi sysvals).  A clamped
 * access still reads its slot's base: unbound constant buffers point at the
 * device zero page (agx_set_cbuf_uniforms), unbound shader buffers at
 * Asahi's sink (agx_set_ssbo_uniforms).
 */
#ifndef AGX_WIN32_D3D_ROBUSTNESS_H
#define AGX_WIN32_D3D_ROBUSTNESS_H

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"

/* offset + bytes <= size, without wrapping */
static inline nir_def *
agx_win32_d3d_in_bounds(nir_builder *b, nir_def *offset, unsigned bytes,
                        nir_def *size)
{
   return nir_iand(b, nir_uge_imm(b, size, bytes),
                   nir_uge(b, nir_isub(b, size, nir_imm_int(b, (int)bytes)), offset));
}

static inline nir_def *
agx_win32_d3d_byte_offset(nir_builder *b, nir_intrinsic_instr *intr)
{
   nir_def *offset = nir_get_io_offset_src(intr)->ssa;
   unsigned shift = nir_intrinsic_offset_shift(intr);
   return shift ? nir_ishl_imm(b, offset, shift) : offset;
}

/* D3D constant buffers are bound in 16-byte registers and read a register
 * (or part of one) at a time, so an access is either wholly in bounds or
 * wholly out: test the whole access and return 0 for all of it. The clamp
 * and select stay ALU on uniform values, which Asahi still hoists into the
 * preamble with the load. */
static inline bool
agx_win32_d3d_lower_load_ubo(nir_builder *b, nir_intrinsic_instr *intr)
{
   unsigned bytes = intr->def.num_components * (intr->def.bit_size / 8);
   b->cursor = nir_before_instr(&intr->instr);
   nir_def *offset = intr->src[1].ssa;
   nir_def *valid = agx_win32_d3d_in_bounds(
      b, offset, bytes, nir_get_ubo_size(b, 32, intr->src[0].ssa));
   nir_src_rewrite(&intr->src[1], nir_bcsel(b, valid, offset, nir_imm_int(b, 0)));
   /* offset 0 must satisfy the alignment the load claims */
   nir_intrinsic_set_align(intr, nir_intrinsic_align(intr), 0);
   b->cursor = nir_after_instr(&intr->instr);
   nir_def *zero = nir_imm_zero(b, intr->def.num_components, intr->def.bit_size);
   nir_def_rewrite_uses_after(&intr->def, nir_bcsel(b, valid, &intr->def, zero));
   return true;
}

/* Wholly in bounds: the original vector load. Otherwise one load per
 * component, each in bounds or replaced by 0. */
static inline bool
agx_win32_d3d_lower_load_ssbo(nir_builder *b, nir_intrinsic_instr *intr)
{
   unsigned n = intr->def.num_components, bits = intr->def.bit_size, elem = bits / 8;
   unsigned align = MIN2(nir_intrinsic_align(intr), elem);
   nir_def *comps[NIR_MAX_VEC_COMPONENTS];
   b->cursor = nir_before_instr(&intr->instr);
   nir_def *index = intr->src[0].ssa;
   nir_def *offset = agx_win32_d3d_byte_offset(b, intr);
   nir_def *size = nir_get_ssbo_size(b, 32, index);
   nir_push_if(b, agx_win32_d3d_in_bounds(b, offset, n * elem, size));
   nir_intrinsic_instr *whole =
      nir_instr_as_intrinsic(nir_instr_clone(b->shader, &intr->instr));
   nir_builder_instr_insert(b, &whole->instr);
   nir_push_else(b, NULL);
   for (unsigned i = 0; i < n; ++i) {
      nir_def *valid = agx_win32_d3d_in_bounds(b, offset, (i + 1) * elem, size);
      nir_def *at = nir_bcsel(b, valid, nir_iadd_imm(b, offset, i * elem), nir_imm_int(b, 0));
      nir_def *v = nir_load_ssbo(b, 1, bits, index, at, .access = nir_intrinsic_access(intr),
                                 .align_mul = align, .align_offset = 0);
      comps[i] = nir_bcsel(b, valid, v, nir_imm_zero(b, 1, bits));
   }
   nir_def *split = nir_vec(b, comps, n);
   nir_pop_if(b, NULL);
   nir_def_replace(&intr->def, nir_if_phi(b, &whole->def, split));
   return true;
}

/* Wholly in bounds: the original store. Otherwise each written component
 * that is in bounds. */
static inline bool
agx_win32_d3d_lower_store_ssbo(nir_builder *b, nir_intrinsic_instr *intr)
{
   nir_def *value = intr->src[0].ssa, *index = intr->src[1].ssa;
   unsigned elem = value->bit_size / 8, mask = nir_intrinsic_write_mask(intr);
   unsigned align = MIN2(nir_intrinsic_align(intr), elem);
   b->cursor = nir_before_instr(&intr->instr);
   nir_def *offset = agx_win32_d3d_byte_offset(b, intr);
   nir_def *size = nir_get_ssbo_size(b, 32, index);
   nir_push_if(b, agx_win32_d3d_in_bounds(b, offset, util_last_bit(mask) * elem, size));
   nir_builder_instr_insert(b, nir_instr_clone(b->shader, &intr->instr));
   nir_push_else(b, NULL);
   u_foreach_bit(i, mask) {
      nir_push_if(b, agx_win32_d3d_in_bounds(b, offset, (i + 1) * elem, size));
      nir_store_ssbo(b, nir_channel(b, value, i), index, nir_iadd_imm(b, offset, i * elem),
                     .write_mask = 1, .access = nir_intrinsic_access(intr),
                     .align_mul = align, .align_offset = 0);
      nir_pop_if(b, NULL);
   }
   nir_pop_if(b, NULL);
   nir_instr_remove(&intr->instr);
   return true;
}

static inline bool
agx_win32_d3d_lower_buffer_access_instr(nir_builder *b, nir_intrinsic_instr *intr,
                                        void *data)
{
   (void)data;
   switch (intr->intrinsic) {
   case nir_intrinsic_load_ubo:
      return agx_win32_d3d_lower_load_ubo(b, intr);
   case nir_intrinsic_load_ssbo:
      return agx_win32_d3d_lower_load_ssbo(b, intr);
   case nir_intrinsic_store_ssbo:
      return agx_win32_d3d_lower_store_ssbo(b, intr);
   default:
      return false;
   }
}

static inline bool
agx_win32_lower_d3d_buffer_access(nir_shader *nir)
{
   return nir_shader_intrinsics_pass(nir, agx_win32_d3d_lower_buffer_access_instr,
                                     nir_metadata_none, NULL);
}

#endif
