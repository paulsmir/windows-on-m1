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

#include <stdlib.h>
#include <string.h>

#include "compiler/nir/nir.h"
#include "compiler/nir/nir_builder.h"

/* offset + bytes <= size, without wrapping: offset < size - bytes + 1, or
 * never when size < bytes (the limit is then 0). The limit depends on the
 * size alone, so Asahi computes it in the preamble and each access costs one
 * compare (RC74; the earlier and of two compares cost two). bytes >= 1, so
 * size - bytes + 1 does not wrap. */
static inline nir_def *
agx_win32_d3d_in_bounds(nir_builder *b, nir_def *offset, unsigned bytes,
                        nir_def *size)
{
   nir_def *limit = nir_bcsel(b, nir_uge_imm(b, size, bytes),
                              nir_iadd_imm(b, size, 1 - (int64_t)bytes), nir_imm_int(b, 0));
   return nir_ult(b, offset, limit);
}

static inline nir_def *
agx_win32_d3d_byte_offset(nir_builder *b, nir_intrinsic_instr *intr)
{
   nir_def *offset = nir_get_io_offset_src(intr)->ssa;
   unsigned shift = nir_intrinsic_offset_shift(intr);
   return shift ? nir_ishl_imm(b, offset, shift) : offset;
}

/* offset + skip elements when valid, else 0. An offset the access aligns
 * to its element (align_mul) is selected in elements and scaled by an amul,
 * which Asahi folds into the load's scaled index (agx_nir_algebraic.py
 * fuse_lea); a selected byte offset costs a 64-bit address add per load. */
static inline nir_def *
agx_win32_d3d_clamped_offset(nir_builder *b, nir_def *offset, unsigned skip, unsigned elem,
                             bool aligned, nir_def *valid)
{
   if (!aligned || !util_is_power_of_two_nonzero(elem) || elem == 1)
      return nir_bcsel(b, valid, nir_iadd_imm(b, offset, skip * elem), nir_imm_int(b, 0));
   nir_def *element = nir_iadd_imm(b, nir_ushr_imm(b, offset, util_logbase2(elem)), skip);
   return nir_amul_imm(b, nir_bcsel(b, valid, element, nir_imm_int(b, 0)), elem);
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

/* One load per component, each in bounds or replaced by 0, in straight-line
 * code (RC74). The AGX backend waits for every outstanding load at the end
 * of a block (agx_insert_waits.c), so a load inside an if/else is never
 * overlapped with another one: the D3D-H6 form, which branched on the whole
 * access, serialised every UAV read of a shader. The loads are exactly that
 * form's per-component path: an out-of-bounds component reads offset 0, its
 * slot's base (the sink when nothing is bound), and the select drops the
 * value. A vector load at offset 0 would read past a view smaller than the
 * vector, so the in-bounds case is not given one. */
static inline bool
agx_win32_d3d_lower_load_ssbo(nir_builder *b, nir_intrinsic_instr *intr)
{
   unsigned n = intr->def.num_components, bits = intr->def.bit_size, elem = bits / 8;
   unsigned align = MIN2(nir_intrinsic_align(intr), elem);
   const bool aligned = nir_intrinsic_align(intr) >= elem;
   nir_def *comps[NIR_MAX_VEC_COMPONENTS];
   b->cursor = nir_before_instr(&intr->instr);
   nir_def *index = intr->src[0].ssa;
   nir_def *offset = agx_win32_d3d_byte_offset(b, intr);
   nir_def *size = nir_get_ssbo_size(b, 32, index);
   for (unsigned i = 0; i < n; ++i) {
      nir_def *valid = agx_win32_d3d_in_bounds(b, offset, (i + 1) * elem, size);
      nir_def *at = agx_win32_d3d_clamped_offset(b, offset, i, elem, aligned, valid);
      nir_def *v = nir_load_ssbo(b, 1, bits, index, at, .access = nir_intrinsic_access(intr),
                                 .align_mul = align, .align_offset = 0);
      comps[i] = nir_bcsel(b, valid, v, nir_imm_zero(b, 1, bits));
   }
   nir_def_replace(&intr->def, nir_vec(b, comps, n));
   return true;
}

/* APPLE_AGX_D3D_BUFFER_BRANCHES=1 (the D3D-H6 form, for comparison): wholly
 * in bounds the original vector load, otherwise one load per component, each
 * in bounds or replaced by 0, behind a branch. */
static inline bool
agx_win32_d3d_lower_load_ssbo_branched(nir_builder *b, nir_intrinsic_instr *intr)
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
 * that is in bounds. A single written component is wholly in bounds or
 * wholly out (RC74: one if, not an if/else holding another if). */
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
   if (util_bitcount(mask) > 1) {
      nir_push_else(b, NULL);
      u_foreach_bit(i, mask) {
         nir_push_if(b, agx_win32_d3d_in_bounds(b, offset, (i + 1) * elem, size));
         nir_store_ssbo(b, nir_channel(b, value, i), index, nir_iadd_imm(b, offset, i * elem),
                        .write_mask = 1, .access = nir_intrinsic_access(intr),
                        .align_mul = align, .align_offset = 0);
         nir_pop_if(b, NULL);
      }
   }
   nir_pop_if(b, NULL);
   nir_instr_remove(&intr->instr);
   return true;
}

/* nir_opt_load_store_vectorize's limits for AGX (agx_compile.c
 * agx_mem_vectorize_cb): no holes, aligned to the component, at most four
 * 32-bit components */
static inline bool
agx_win32_d3d_vectorize_cb(unsigned align_mul, unsigned align_offset, unsigned bit_size,
                           unsigned num_components, int64_t hole_size,
                           nir_intrinsic_instr *low, nir_intrinsic_instr *high, void *data)
{
   (void)low;
   (void)high;
   (void)data;
   return hole_size <= 0 && bit_size / 8 <= nir_combined_align(align_mul, align_offset) &&
          num_components <= 4 && bit_size <= 32;
}

struct agx_win32_d3d_lowering {
   struct set *checked; /* stores a run test already covers */
   bool branches;       /* APPLE_AGX_D3D_BUFFER_BRANCHES=1 */
};

/* RC75: an access the frontend marked ACCESS_IN_BOUNDS (a D3D12 root
 * descriptor, dxbc_nir_mark_root_views) has no bound to test: D3D12 root
 * CBVs/SRVs/UAVs carry no size, and an access past the allocation is
 * undefined. It stays as written, a whole (vector) access. */
static inline bool
agx_win32_d3d_unchecked(const nir_intrinsic_instr *intr)
{
   return (nir_intrinsic_access(intr) & ACCESS_IN_BOUNDS) != 0;
}

static inline bool
agx_win32_d3d_lower_buffer_access_instr(nir_builder *b, nir_intrinsic_instr *intr,
                                        void *data)
{
   const struct agx_win32_d3d_lowering *state = (const struct agx_win32_d3d_lowering *)data;
   switch (intr->intrinsic) {
   case nir_intrinsic_load_ubo:
   case nir_intrinsic_load_ssbo:
   case nir_intrinsic_store_ssbo:
      if (agx_win32_d3d_unchecked(intr))
         return false;
      break;
   default:
      return false;
   }
   switch (intr->intrinsic) {
   case nir_intrinsic_load_ubo:
      return agx_win32_d3d_lower_load_ubo(b, intr);
   case nir_intrinsic_load_ssbo:
      return state->branches ? agx_win32_d3d_lower_load_ssbo_branched(b, intr)
                             : agx_win32_d3d_lower_load_ssbo(b, intr);
   case nir_intrinsic_store_ssbo:
      if (_mesa_set_search(state->checked, intr))
         return false;
      return agx_win32_d3d_lower_store_ssbo(b, intr);
   default:
      return false;
   }
}

/* RC74: consecutive stores of a block (only pure values between them) are
 * tested together: when every one is wholly in bounds they run in one
 * block, else each takes its own D3D path. A store in its own if costs a
 * wait for its completion (agx_insert_waits.c waits at every block end);
 * the run waits once. The stores keep their order and move only past the
 * pure values that follow them, so memory sees the same writes. */
#define AGX_WIN32_D3D_STORE_RUN_MAX 32

static inline void
agx_win32_d3d_group_store_run(nir_builder *b, nir_intrinsic_instr **run, unsigned count,
                              struct set *checked)
{
   /* every operand is defined before the last store */
   b->cursor = nir_after_instr(&run[count - 1]->instr);
   nir_def *all = nir_imm_true(b);
   for (unsigned i = 0; i < count; ++i) {
      nir_intrinsic_instr *st = run[i];
      unsigned elem = st->src[0].ssa->bit_size / 8;
      nir_def *size = nir_get_ssbo_size(b, 32, st->src[1].ssa);
      nir_def *offset = agx_win32_d3d_byte_offset(b, st);
      all = nir_iand(b, all, agx_win32_d3d_in_bounds(
                                b, offset, util_last_bit(nir_intrinsic_write_mask(st)) * elem, size));
   }
   nir_push_if(b, all);
   for (unsigned i = 0; i < count; ++i) {
      nir_instr *copy = nir_instr_clone(b->shader, &run[i]->instr);
      nir_builder_instr_insert(b, copy);
      _mesa_set_add(checked, copy);
   }
   nir_push_else(b, NULL);
   /* unchecked copies: each takes its own path in the lowering below */
   for (unsigned i = 0; i < count; ++i)
      nir_builder_instr_insert(b, nir_instr_clone(b->shader, &run[i]->instr));
   nir_pop_if(b, NULL);
   for (unsigned i = 0; i < count; ++i)
      nir_instr_remove(&run[i]->instr);
}

static inline bool
agx_win32_d3d_group_stores(nir_function_impl *impl, struct set *checked)
{
   /* find every run first (its stores, then a null): grouping splits blocks */
   struct util_dynarray runs;
   util_dynarray_init(&runs, NULL);
   nir_foreach_block(block, impl) {
      unsigned count = 0;
      nir_foreach_instr(instr, block) {
         /* an unchecked (root-descriptor) store is not tested: it ends a run
          * like any other memory access, so no checked store moves past it */
         const bool store = instr->type == nir_instr_type_intrinsic &&
                            nir_instr_as_intrinsic(instr)->intrinsic == nir_intrinsic_store_ssbo &&
                            !agx_win32_d3d_unchecked(nir_instr_as_intrinsic(instr));
         /* pure values only: moving a store later past them changes no
          * memory order */
         const bool gap = instr->type == nir_instr_type_alu || instr->type == nir_instr_type_load_const ||
                          instr->type == nir_instr_type_undef;
         if (store) {
            nir_intrinsic_instr *st = nir_instr_as_intrinsic(instr);
            util_dynarray_append(&runs, st);
            if (++count < AGX_WIN32_D3D_STORE_RUN_MAX)
               continue;
         } else if (gap) {
            continue;
         }
         /* a run of one is left to the single-store lowering */
         if (count == 1)
            (void)util_dynarray_pop(&runs, nir_intrinsic_instr *);
         else if (count) {
            nir_intrinsic_instr *end = NULL;
            util_dynarray_append(&runs, end);
         }
         count = 0;
      }
      if (count == 1)
         (void)util_dynarray_pop(&runs, nir_intrinsic_instr *);
      else if (count) {
         nir_intrinsic_instr *end = NULL;
         util_dynarray_append(&runs, end);
      }
   }
   nir_builder b = nir_builder_create(impl);
   const bool progress = runs.size != 0;
   nir_intrinsic_instr **all = (nir_intrinsic_instr **)runs.data;
   const unsigned total = runs.size / sizeof(nir_intrinsic_instr *);
   for (unsigned first = 0, i = 0; i < total; ++i) {
      if (all[i])
         continue;
      agx_win32_d3d_group_store_run(&b, all + first, i - first, checked);
      first = i + 1;
   }
   util_dynarray_fini(&runs);
   return nir_progress(progress, impl, nir_metadata_none);
}

/* RC74: adjacent shader-buffer accesses are merged first (Gallium Asahi
 * vectorizes them only after this lowering, when every store sits in its
 * own if), so a vector of in-bounds components costs one test, and runs of
 * stores are tested together. Merging keeps the D3D rule: the lowering
 * tests each merged component. APPLE_AGX_D3D_BUFFER_BRANCHES=1 restores
 * the D3D-H6 form (a branch per load and per store), read when a shader is
 * compiled, for a hardware comparison. */
static inline bool
agx_win32_lower_d3d_buffer_access(nir_shader *nir)
{
   const char *env = getenv("APPLE_AGX_D3D_BUFFER_BRANCHES");
   struct agx_win32_d3d_lowering state = {NULL, env && env[0] == '1' && !env[1]};
   /* only a shader with shader-buffer loads or stores, which the lowering
    * below then rewrites (an atomic-only shader is left alone) */
   bool accesses = false;
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            const nir_intrinsic_op op = nir_instr_as_intrinsic(instr)->intrinsic;
            accesses |= op == nir_intrinsic_load_ssbo || op == nir_intrinsic_store_ssbo;
         }
      }
   }
   bool progress = false;
   state.checked = _mesa_pointer_set_create(NULL);
   if (accesses && !state.branches) {
      nir_load_store_vectorize_options vectorize;
      memset(&vectorize, 0, sizeof(vectorize));
      vectorize.callback = agx_win32_d3d_vectorize_cb;
      vectorize.modes = nir_var_mem_ssbo;
      progress |= nir_opt_load_store_vectorize(nir, &vectorize);
      nir_foreach_function_impl(impl, nir)
         progress |= agx_win32_d3d_group_stores(impl, state.checked);
   }
   progress |= nir_shader_intrinsics_pass(nir, agx_win32_d3d_lower_buffer_access_instr,
                                          nir_metadata_none, &state);
   _mesa_set_destroy(state.checked, NULL);
   return progress;
}

#endif
