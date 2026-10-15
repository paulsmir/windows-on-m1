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
   glsl_type_singleton_decref();
   return 0;
}
