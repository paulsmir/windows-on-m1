/* Translate a DXIL container (DXC, SM 6.x) with agx_dxil_to_nir and the
 * D3D12 owner's slot limits, then run what the owner and Asahi do with a
 * DXBC translation: the SRV/UAV dimension scans, Asahi's I/O lowering and
 * agx_preprocess_nir, validating after each step.
 * usage: dxil_to_nir_fixture <object.dxil> [--print-final] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "agx_dxil_to_nir.h"
#include "dxbc_to_nir.h"

#include "nir.h"
#include "asahi/compiler/agx_compile.h"

static unsigned
type_size_vec4(const struct glsl_type *type, bool bindless)
{
   (void)bindless;
   return (unsigned)glsl_count_attribute_slots(type, false);
}

int
main(int argc, char **argv)
{
   bool print_final = false;
   for (int a = 2; a < argc; ++a)
      print_final |= !strcmp(argv[a], "--print-final");
   FILE *f = argc > 1 ? fopen(argv[1], "rb") : NULL;
   if (!f)
      return 2;
   fseek(f, 0, SEEK_END);
   const long size = ftell(f);
   fseek(f, 0, SEEK_SET);
   void *bytes = malloc((size_t)size);
   if (!bytes || fread(bytes, 1, (size_t)size, f) != (size_t)size)
      return 2;
   fclose(f);

   glsl_type_singleton_init_or_ref();
   /* the owner's capacities (agx_d3d12_windows.cpp) */
   static const uint32_t limits[DXBC_BINDING_KIND_COUNT] = {14, 128, 8, 16};
   struct agx_dxil_nir_result r;
   const enum agx_dxil_nir_status status = agx_dxil_to_nir(bytes, (size_t)size, limits, &agx_nir_options, &r);
   if (status != AGX_DXIL_NIR_OK) {
      printf("REFUSED status %d %s\n", (int)status, r.error);
      return 0;
   }
   nir_shader *nir = r.nir;
   for (uint32_t i = 0; i < r.bindings.count; ++i)
      printf("BINDING kind %u id %u space %u reg %u slot %u flags %u\n", r.bindings.bindings[i].kind,
             r.bindings.bindings[i].id, r.bindings.bindings[i].space, r.bindings.bindings[i].reg,
             r.bindings.bindings[i].slot, r.bindings.bindings[i].flags);
   uint8_t srv[DXBC_RESOURCE_SLOTS], uav[DXBC_UAV_DIMENSION_SLOTS];
   const bool srv_ok = dxbc_nir_srv_dimensions(nir, srv), uav_ok = dxbc_nir_uav_dimensions(nir, uav);
   printf("DIMENSIONS srv %d uav %d", srv_ok, uav_ok);
   for (unsigned i = 0; i < DXBC_RESOURCE_SLOTS; ++i)
      if (srv[i])
         printf(" t%u=%u", i, srv[i]);
   for (unsigned i = 0; i < DXBC_UAV_DIMENSION_SLOTS; ++i)
      if (uav[i])
         printf(" u%u=%u", i, uav[i]);
   printf("\n");
   printf("INFO stage %s ubos %u ssbos %u images %u textures 0x%llx textures_txf 0x%llx samplers 0x%llx images_used 0x%llx "
          "block %ux%ux%u shared %u inputs 0x%llx outputs 0x%llx\n",
          mesa_shader_stage_name(nir->info.stage), nir->info.num_ubos, nir->info.num_ssbos, nir->info.num_images,
          (unsigned long long)nir->info.textures_used[0], (unsigned long long)nir->info.textures_used_by_txf[0],
          (unsigned long long)nir->info.samplers_used[0], (unsigned long long)nir->info.images_used[0],
          nir->info.workgroup_size[0], nir->info.workgroup_size[1], nir->info.workgroup_size[2], nir->info.shared_size,
          (unsigned long long)nir->info.inputs_read, (unsigned long long)nir->info.outputs_written);
   nir_foreach_variable_with_modes(v, nir, nir_var_shader_in | nir_var_shader_out)
      printf("IO %s %s location %d component %u\n", v->data.mode == nir_var_shader_in ? "in" : "out", v->name,
             v->data.location, v->data.location_frac);

   const unsigned ssbos = nir->info.num_ssbos, images = nir->info.num_images;
   const unsigned gathered_images = nir->info.num_images, image_span = BITSET_LAST_BIT(nir->info.images_used);
   /* agx_shader_initialize: I/O lowering, then agx_preprocess_nir */
   if (nir->info.stage == MESA_SHADER_FRAGMENT)
      NIR_PASS(_, nir, nir_lower_wpos_center);
   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out, type_size_vec4,
            nir_lower_io_lower_64bit_to_32 | nir_lower_io_use_interpolated_input_intrinsics);
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   agx_preprocess_nir(nir);
   nir_validate_shader(nir, "agx_preprocess_nir");
   if (print_final)
      nir_print_shader(nir, stdout);
   /* the same summary as dxbc_to_nir_fixture, for parity checks */
   unsigned block[3], shared, shared_derefs = 0, early_stores = 0, instrs = 0, vs_input_bases = 0;
   unsigned agx_unlowered_bitfield = 0;
   dxbc_nir_compute_info(nir, block, &shared);
   nir_foreach_function_impl(impl, nir) {
      nir_block *last = nir_impl_last_block(impl);
      nir_foreach_block(blk, impl)
         nir_foreach_instr(instr, blk) {
            ++instrs;
            if (instr->type == nir_instr_type_alu) {
               const nir_op op = nir_instr_as_alu(instr)->op;
               if (op == nir_op_bfi || op == nir_op_bfm || op == nir_op_ubfe || op == nir_op_ibfe)
                  ++agx_unlowered_bitfield;
            }
            if (instr->type == nir_instr_type_deref && nir_deref_mode_is(nir_instr_as_deref(instr), nir_var_mem_shared))
               ++shared_derefs;
            if (instr->type == nir_instr_type_intrinsic && blk != last && nir->info.stage == MESA_SHADER_FRAGMENT &&
                nir_instr_as_intrinsic(instr)->intrinsic == nir_intrinsic_store_output)
               ++early_stores;
            if (instr->type == nir_instr_type_intrinsic && nir->info.stage == MESA_SHADER_VERTEX &&
                nir_instr_as_intrinsic(instr)->intrinsic == nir_intrinsic_load_input)
               vs_input_bases |= 1u << (nir_intrinsic_base(nir_instr_as_intrinsic(instr)) & 31u);
         }
   }
   printf("TRANSLATED stage %s instructions %u inputs 0x%llx outputs 0x%llx textures %u "
          "ssbos %u images %u block %ux%ux%u shared %u shared-derefs %u early-output-stores %u "
          "vs-input-bases 0x%x agx-unlowered-bitfield %u gathered-images %u image-span %u\n",
          mesa_shader_stage_name(nir->info.stage), instrs, (unsigned long long)nir->info.inputs_read,
          (unsigned long long)nir->info.outputs_written, BITSET_LAST_BIT(nir->info.textures_used), ssbos, images,
          block[0], block[1], block[2], shared, shared_derefs, early_stores, vs_input_bases, agx_unlowered_bitfield,
          gathered_images, image_span);
   ralloc_free(nir);
   glsl_type_singleton_decref();
   free(bytes);
   return 0;
}
