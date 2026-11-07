/* D3D-H6: behaviour of the D3D buffer-access lowering
 * (drivers/apple-agx/mesa/winsys/agx_win32_d3d_robustness.h) on host NIR.
 *
 * Each case builds one access at a constant offset, runs the lowering,
 * replaces get_ubo_size / get_ssbo_size by the case's bound size, folds
 * constants and dead control flow, and prints what is left:
 *   load:  "<case> <c0> ... <cN-1>", each component "0" (folded to zero) or
 *          "<intrinsic>@<byte offset>" (the load that supplies it)
 *   store: "<case> stores <offset>x<components> ...", every store_ssbo left
 *   other: "<case> <intrinsic> lowered|kept"
 * Results are written with store_global, which the lowering leaves alone.
 * usage: d3d_buffer_robustness_fixture */
#include <stdio.h>
#include <string.h>

#include "nir.h"
#include "nir_builder.h"
#include "asahi/compiler/agx_compile.h"

#include "agx_win32_d3d_robustness.h"

static nir_builder
begin(void)
{
   return nir_builder_init_simple_shader(MESA_SHADER_COMPUTE, &agx_nir_options,
                                         "d3d-robustness");
}

static void
sink(nir_builder *b, nir_def *value)
{
   nir_store_global(b, value, nir_imm_int64(b, 0x1000),
                    .write_mask = BITFIELD_MASK(value->num_components),
                    .align_mul = 4);
}

static bool
size_to_constant(nir_builder *b, nir_intrinsic_instr *intr, void *data)
{
   if (intr->intrinsic != nir_intrinsic_get_ubo_size &&
       intr->intrinsic != nir_intrinsic_get_ssbo_size)
      return false;
   b->cursor = nir_before_instr(&intr->instr);
   nir_def_replace(&intr->def, nir_imm_int(b, (int)*(const uint32_t *)data));
   return true;
}

static void
finish(nir_shader *s, uint32_t bound)
{
   bool progress;
   nir_validate_shader(s, "d3d lowering");
   nir_shader_intrinsics_pass(s, size_to_constant, nir_metadata_control_flow, &bound);
   do {
      progress = false;
      NIR_PASS(progress, s, nir_opt_copy_prop);
      NIR_PASS(progress, s, nir_opt_constant_folding);
      NIR_PASS(progress, s, nir_opt_algebraic);
      NIR_PASS(progress, s, nir_opt_dead_cf);
      NIR_PASS(progress, s, nir_opt_remove_phis);
      NIR_PASS(progress, s, nir_opt_dce);
      NIR_PASS(progress, s, nir_opt_cse);
   } while (progress);
   nir_validate_shader(s, "folded");
}

static void
print_scalar(nir_scalar s)
{
   s = nir_scalar_chase_movs(s);
   if (nir_scalar_is_const(s)) {
      printf(" %llu", (unsigned long long)nir_scalar_as_uint(s));
      return;
   }
   nir_instr *instr = nir_def_instr(s.def);
   if (instr->type == nir_instr_type_intrinsic) {
      nir_intrinsic_instr *load = nir_instr_as_intrinsic(instr);
      nir_src *offset = nir_get_io_offset_src(load);
      if (offset && nir_src_is_const(*offset)) {
         printf(" %s@%llu", nir_intrinsic_infos[load->intrinsic].name,
                (unsigned long long)(nir_src_as_uint(*offset) +
                                     s.comp * (load->def.bit_size / 8)));
         return;
      }
   }
   printf(" ?");
}

static void
print_result(const char *name, nir_shader *s)
{
   printf("%s", name);
   nir_foreach_function_impl(impl, s) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic != nir_intrinsic_store_global)
               continue;
            for (unsigned c = 0; c < intr->src[0].ssa->num_components; ++c)
               print_scalar(nir_get_scalar(intr->src[0].ssa, c));
         }
      }
   }
   printf("\n");
}

static void
print_stores(const char *name, nir_shader *s)
{
   printf("%s stores", name);
   nir_foreach_function_impl(impl, s) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            if (intr->intrinsic != nir_intrinsic_store_ssbo)
               continue;
            if (!nir_src_is_const(intr->src[2])) {
               printf(" ?");
               continue;
            }
            printf(" %llux%u", (unsigned long long)nir_src_as_uint(intr->src[2]),
                   util_bitcount(nir_intrinsic_write_mask(intr)));
         }
      }
   }
   printf("\n");
}

static void
ubo_case(const char *name, uint32_t offset, uint32_t bound)
{
   nir_builder b = begin();
   sink(&b, nir_load_ubo(&b, 4, 32, nir_imm_int(&b, 1), nir_imm_int(&b, (int)offset),
                         .align_mul = 16, .align_offset = 0, .range_base = 0,
                         .range = ~0u));
   agx_win32_lower_d3d_buffer_access(b.shader);
   finish(b.shader, bound);
   print_result(name, b.shader);
   ralloc_free(b.shader);
}

static void
ssbo_load_case(const char *name, uint32_t offset, unsigned components, uint32_t bound)
{
   nir_builder b = begin();
   sink(&b, nir_load_ssbo(&b, components, 32, nir_imm_int(&b, 2),
                          nir_imm_int(&b, (int)offset), .align_mul = 4,
                          .align_offset = 0));
   agx_win32_lower_d3d_buffer_access(b.shader);
   finish(b.shader, bound);
   print_result(name, b.shader);
   ralloc_free(b.shader);
}

static void
ssbo_store_case(const char *name, uint32_t offset, unsigned mask, uint32_t bound)
{
   nir_builder b = begin();
   nir_def *value = nir_imm_ivec4(&b, 11, 12, 13, 14);
   nir_store_ssbo(&b, value, nir_imm_int(&b, 2), nir_imm_int(&b, (int)offset),
                  .write_mask = mask, .align_mul = 4, .align_offset = 0);
   agx_win32_lower_d3d_buffer_access(b.shader);
   finish(b.shader, bound);
   print_stores(name, b.shader);
   ralloc_free(b.shader);
}

/* RC74: control flow the lowering leaves (after folding) */
static unsigned
count_ifs(nir_shader *s)
{
   unsigned ifs = 0;
   nir_foreach_function_impl(impl, s)
      nir_foreach_block(block, impl)
         ifs += nir_block_get_following_if(block) != NULL;
   return ifs;
}

/* RC74: every component of a load at a constant offset, against the D3D
 * rule computed here, for each size; the bound size stays a runtime value
 * until finish() folds it, so the straight-line form is what is checked */
static unsigned
ssbo_load_sweep(unsigned *ifs)
{
   unsigned wrong = 0;
   *ifs = 0;
   for (unsigned n = 1; n <= 4; ++n) {
      for (uint32_t offset = 0; offset <= 24; offset += 4) {
         for (uint32_t bound = 0; bound <= 28; bound += 4) {
            nir_builder b = begin();
            sink(&b, nir_load_ssbo(&b, n, 32, nir_imm_int(&b, 2), nir_imm_int(&b, (int)offset),
                                   .align_mul = 4, .align_offset = 0));
            agx_win32_lower_d3d_buffer_access(b.shader);
            *ifs += count_ifs(b.shader);
            finish(b.shader, bound);
            nir_foreach_function_impl(impl, b.shader) {
               nir_foreach_block(block, impl) {
                  nir_foreach_instr(instr, block) {
                     if (instr->type != nir_instr_type_intrinsic ||
                         nir_instr_as_intrinsic(instr)->intrinsic != nir_intrinsic_store_global)
                        continue;
                     nir_intrinsic_instr *st = nir_instr_as_intrinsic(instr);
                     for (unsigned c = 0; c < n; ++c) {
                        nir_scalar v = nir_scalar_chase_movs(nir_get_scalar(st->src[0].ssa, c));
                        const bool in = offset + 4 * (c + 1) <= bound;
                        if (nir_scalar_is_const(v)) {
                           wrong += in || nir_scalar_as_uint(v) != 0;
                           continue;
                        }
                        nir_instr *load = nir_def_instr(v.def);
                        nir_src *at = load->type == nir_instr_type_intrinsic ?
                           nir_get_io_offset_src(nir_instr_as_intrinsic(load)) : NULL;
                        wrong += !in || !at || !nir_src_is_const(*at) ||
                                 nir_instr_as_intrinsic(load)->intrinsic != nir_intrinsic_load_ssbo ||
                                 nir_src_as_uint(*at) + 4 * v.comp != offset + 4 * c;
                     }
                  }
               }
            }
            ralloc_free(b.shader);
         }
      }
   }
   return wrong;
}

/* RC74: two consecutive stores (a run tested together) write exactly the
 * in-bounds components of each, for each size */
static unsigned
ssbo_store_run_sweep(void)
{
   unsigned wrong = 0;
   for (uint32_t bound = 0; bound <= 36; bound += 4) {
      nir_builder b = begin();
      nir_store_ssbo(&b, nir_imm_ivec4(&b, 1, 2, 3, 4), nir_imm_int(&b, 2), nir_imm_int(&b, 0),
                     .write_mask = 0xf, .align_mul = 4, .align_offset = 0);
      nir_store_ssbo(&b, nir_imm_ivec2(&b, 5, 6), nir_imm_int(&b, 2), nir_imm_int(&b, 24),
                     .write_mask = 0x3, .align_mul = 4, .align_offset = 0);
      agx_win32_lower_d3d_buffer_access(b.shader);
      finish(b.shader, bound);
      /* written bytes, by the dwords the folded stores cover */
      unsigned written = 0, expected = 0;
      for (unsigned d = 0; d < 4; ++d)
         expected |= (4 * (d + 1) <= bound) << d;
      for (unsigned d = 0; d < 2; ++d)
         expected |= (24 + 4 * (d + 1) <= bound) << (6 + d);
      nir_foreach_function_impl(impl, b.shader) {
         nir_foreach_block(block, impl) {
            nir_foreach_instr(instr, block) {
               if (instr->type != nir_instr_type_intrinsic ||
                   nir_instr_as_intrinsic(instr)->intrinsic != nir_intrinsic_store_ssbo)
                  continue;
               nir_intrinsic_instr *st = nir_instr_as_intrinsic(instr);
               if (!nir_src_is_const(st->src[2])) {
                  wrong++;
                  continue;
               }
               const unsigned first = nir_src_as_uint(st->src[2]) / 4;
               u_foreach_bit(c, nir_intrinsic_write_mask(st))
                  written |= 1u << (first + c);
            }
         }
      }
      wrong += written != expected;
      ralloc_free(b.shader);
   }
   return wrong;
}

/* the components a store_global sinks, against the D3D rule for a load of
 * n dwords at offset with the bound: in-bounds components read their own
 * dword of a load_ssbo, out-of-bounds ones are 0 */
static unsigned
check_sunk_loads(nir_shader *s, const uint32_t *offsets, const unsigned *comps, unsigned loads,
                 uint32_t bound)
{
   unsigned wrong = 0, seen = 0;
   nir_foreach_function_impl(impl, s) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic ||
                nir_instr_as_intrinsic(instr)->intrinsic != nir_intrinsic_store_global)
               continue;
            nir_intrinsic_instr *st = nir_instr_as_intrinsic(instr);
            if (seen >= loads) {
               wrong++;
               continue;
            }
            const uint32_t offset = offsets[seen];
            const unsigned n = comps[seen++];
            for (unsigned c = 0; c < n; ++c) {
               nir_scalar v = nir_scalar_chase_movs(nir_get_scalar(st->src[0].ssa, c));
               const bool in = offset + 4 * (c + 1) <= bound;
               if (nir_scalar_is_const(v)) {
                  wrong += in || nir_scalar_as_uint(v) != 0;
                  continue;
               }
               nir_instr *load = nir_def_instr(v.def);
               nir_src *at = load->type == nir_instr_type_intrinsic ?
                  nir_get_io_offset_src(nir_instr_as_intrinsic(load)) : NULL;
               wrong += !in || !at || !nir_src_is_const(*at) ||
                        nir_instr_as_intrinsic(load)->intrinsic != nir_intrinsic_load_ssbo ||
                        nir_src_as_uint(*at) + 4 * v.comp != offset + 4 * c;
            }
         }
      }
   }
   return wrong + (seen != loads);
}

/* RC109: a run of loads (a vec4 at 0, a vec2 at 24, a vec4 at 8 of another
 * buffer) tested together reads exactly the in-bounds components of each
 * for every size: the whole form when all are in bounds, the
 * per-component form otherwise. The sizes of both buffers are the bound. */
static unsigned
ssbo_load_run_sweep(unsigned *ifs)
{
   static const uint32_t offsets[3] = {0, 24, 8};
   static const unsigned comps[3] = {4, 2, 4};
   unsigned wrong = 0;
   *ifs = 0;
   for (uint32_t bound = 0; bound <= 36; bound += 4) {
      nir_builder b = begin();
      nir_def *v[3];
      for (unsigned i = 0; i < 3; ++i)
         v[i] = nir_load_ssbo(&b, comps[i], 32, nir_imm_int(&b, i == 2 ? 3 : 2),
                              nir_imm_int(&b, (int)offsets[i]), .align_mul = 4, .align_offset = 0);
      for (unsigned i = 0; i < 3; ++i)
         sink(&b, v[i]);
      agx_win32_lower_d3d_buffer_access(b.shader);
      *ifs += count_ifs(b.shader);
      finish(b.shader, bound);
      wrong += check_sunk_loads(b.shader, offsets, comps, 3, bound);
      ralloc_free(b.shader);
   }
   return wrong;
}

/* RC109: what bounds a run. Loads A (vec4 at 0) and B (vec2 at 16), an
 * ALU on them, a load C whose address is that ALU (it starts the next run),
 * a load D (vec4 at 32) independent of C (it joins C's run), a store, then
 * a load E (vec4 at 48) after the store (its own run). Printed:
 * "run_bounds <ifs before folding> <ifs after folding with a 64-byte bound>
 * <A B D E components as the load cases>"; C's address is not constant and
 * is not printed. With every constant access in bounds, the runs of A, B
 * and E fold to their whole loads; C's run keeps its test. */
static void
run_bounds_case(void)
{
   nir_builder b = begin();
   nir_def *a = nir_load_ssbo(&b, 4, 32, nir_imm_int(&b, 2), nir_imm_int(&b, 0),
                              .align_mul = 4, .align_offset = 0);
   nir_def *bb = nir_load_ssbo(&b, 2, 32, nir_imm_int(&b, 2), nir_imm_int(&b, 16),
                               .align_mul = 4, .align_offset = 0);
   nir_def *t = nir_iand_imm(&b, nir_iadd(&b, nir_channel(&b, a, 0), nir_channel(&b, bb, 1)), 0xff0);
   nir_def *c = nir_load_ssbo(&b, 4, 32, nir_imm_int(&b, 2), t, .align_mul = 4, .align_offset = 0);
   nir_def *d = nir_load_ssbo(&b, 4, 32, nir_imm_int(&b, 2), nir_imm_int(&b, 32),
                              .align_mul = 4, .align_offset = 0);
   nir_store_ssbo(&b, nir_imm_ivec4(&b, 1, 2, 3, 4), nir_imm_int(&b, 2), nir_imm_int(&b, 64),
                  .write_mask = 0xf, .align_mul = 4, .align_offset = 0);
   nir_def *e = nir_load_ssbo(&b, 4, 32, nir_imm_int(&b, 2), nir_imm_int(&b, 48),
                              .align_mul = 4, .align_offset = 0);
   sink(&b, a);
   sink(&b, bb);
   sink(&b, d);
   sink(&b, e);
   sink(&b, nir_channel(&b, c, 0));
   agx_win32_lower_d3d_buffer_access(b.shader);
   nir_validate_shader(b.shader, "run bounds");
   const unsigned before = count_ifs(b.shader);
   finish(b.shader, 64);
   printf("run_bounds %u %u", before, count_ifs(b.shader));
   unsigned seen = 0;
   nir_foreach_function_impl(impl, b.shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic ||
                nir_instr_as_intrinsic(instr)->intrinsic != nir_intrinsic_store_global)
               continue;
            nir_intrinsic_instr *st = nir_instr_as_intrinsic(instr);
            if (seen++ == 4)
               break;
            for (unsigned i = 0; i < st->src[0].ssa->num_components; ++i)
               print_scalar(nir_get_scalar(st->src[0].ssa, i));
         }
      }
   }
   printf("\n");
   ralloc_free(b.shader);
}

/* RC75: accesses marked ACCESS_IN_BOUNDS (D3D12 root descriptors,
 * dxbc_nir_mark_root_views) are left whole: a float4 load of a root UAV,
 * a float4 store between two checked stores, a root constant-buffer read;
 * printed as "<case> <kept vector loads> <kept vector stores> <ifs>
 * <checked stores left> <order ok>" */
static void
root_view_case(void)
{
   nir_builder b = begin();
   const enum gl_access_qualifier root = ACCESS_IN_BOUNDS;
   sink(&b, nir_load_ssbo(&b, 4, 32, nir_imm_int(&b, 1), nir_imm_int(&b, 32), .access = root,
                          .align_mul = 16, .align_offset = 0));
   sink(&b, nir_load_ubo(&b, 4, 32, nir_imm_int(&b, 3), nir_imm_int(&b, 64), .access = root,
                         .align_mul = 16, .align_offset = 0, .range_base = 0, .range = ~0u));
   nir_store_ssbo(&b, nir_imm_ivec4(&b, 1, 2, 3, 4), nir_imm_int(&b, 2), nir_imm_int(&b, 0),
                  .write_mask = 0xf, .align_mul = 4, .align_offset = 0);
   nir_store_ssbo(&b, nir_imm_ivec4(&b, 5, 6, 7, 8), nir_imm_int(&b, 1), nir_imm_int(&b, 0),
                  .write_mask = 0xf, .access = root, .align_mul = 16, .align_offset = 0);
   nir_store_ssbo(&b, nir_imm_ivec4(&b, 9, 10, 11, 12), nir_imm_int(&b, 2), nir_imm_int(&b, 16),
                  .write_mask = 0xf, .align_mul = 4, .align_offset = 0);
   agx_win32_lower_d3d_buffer_access(b.shader);
   nir_validate_shader(b.shader, "root views");
   unsigned loads = 0, stores = 0, checked = 0, order = 0, seen = 0;
   nir_foreach_function_impl(impl, b.shader) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            nir_intrinsic_instr *intr = nir_instr_as_intrinsic(instr);
            const bool unchecked = nir_intrinsic_has_access(intr) && (nir_intrinsic_access(intr) & ACCESS_IN_BOUNDS);
            if ((intr->intrinsic == nir_intrinsic_load_ssbo || intr->intrinsic == nir_intrinsic_load_ubo) && unchecked)
               loads += intr->def.num_components == 4;
            if (intr->intrinsic != nir_intrinsic_store_ssbo)
               continue;
            if (unchecked) {
               stores += util_bitcount(nir_intrinsic_write_mask(intr)) == 4;
               /* the root store follows every copy of the first checked store */
               order += seen == 0 ? 0 : 1;
               seen |= 2;
            } else {
               checked++;
               /* a checked copy of the second store must not precede the root store */
               if (nir_src_is_const(intr->src[2]) && nir_src_as_uint(intr->src[2]) == 16 && !(seen & 2))
                  order = 100;
               seen |= 1;
            }
         }
      }
   }
   printf("root_views %u %u %u %u %s\n", loads, stores, count_ifs(b.shader), checked,
          order == 1 ? "ordered" : "reordered");
   ralloc_free(b.shader);
}

static void
atomic_case(void)
{
   nir_builder b = begin();
   sink(&b, nir_ssbo_atomic(&b, 32, nir_imm_int(&b, 2), nir_imm_int(&b, 64),
                            nir_imm_int(&b, 1), .atomic_op = nir_atomic_op_iadd));
   bool progress = agx_win32_lower_d3d_buffer_access(b.shader);
   nir_validate_shader(b.shader, "atomic");
   /* D3D-conformant through nir_lower_robust_access (Asahi's GL filter) */
   printf("ssbo_atomic %s\n", progress ? "lowered" : "kept");
   ralloc_free(b.shader);
}

int
main(void)
{
   glsl_type_singleton_init_or_ref();
   /* constant buffer: 32 bytes bound (two float4 registers) */
   ubo_case("ubo_in_bounds", 16, 32);
   ubo_case("ubo_past_end", 32, 32);
   ubo_case("ubo_unbound", 0, 0);
   ubo_case("ubo_wrapping_offset", 0xfffffff0u, 32);
   /* UAV raw/structured: 16 bytes bound */
   ssbo_load_case("ssbo_load_in_bounds", 0, 4, 16);
   ssbo_load_case("ssbo_load_partial", 8, 4, 16);
   ssbo_load_case("ssbo_load_past_end", 16, 2, 16);
   ssbo_load_case("ssbo_load_unbound", 0, 1, 0);
   ssbo_load_case("ssbo_load_wrapping_offset", 0xfffffffcu, 2, 16);
   ssbo_store_case("ssbo_store_in_bounds", 0, 0xf, 16);
   ssbo_store_case("ssbo_store_partial", 8, 0xf, 16);
   ssbo_store_case("ssbo_store_past_end", 16, 0x3, 16);
   ssbo_store_case("ssbo_store_masked_partial", 4, 0x9, 16);
   ssbo_store_case("ssbo_store_wrapping_offset", 0xfffffffcu, 0x3, 16);
   atomic_case();
   root_view_case();
   unsigned load_ifs;
   const unsigned load_wrong = ssbo_load_sweep(&load_ifs);
   printf("ssbo_load_sweep wrong %u ifs %u\n", load_wrong, load_ifs);
   printf("ssbo_store_run_sweep wrong %u\n", ssbo_store_run_sweep());
   unsigned run_ifs;
   const unsigned run_wrong = ssbo_load_run_sweep(&run_ifs);
   printf("ssbo_load_run_sweep wrong %u ifs %u\n", run_wrong, run_ifs);
   run_bounds_case();
   glsl_type_singleton_decref();
   return 0;
}
