/* Translate a DXBC fixture to NIR with the AGX compiler's NIR options, run
 * the tgsi_to_nir finalization sequence, Asahi's I/O lowering and
 * agx_preprocess_nir, validating the shader after each step.
 * usage: dxbc_to_nir_fixture <object.dxbc> [--print] */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dxbc_parse.h"
#include "dxbc_to_nir.h"

#include "nir.h"
#include "nir_builder.h"
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
   FILE *f;
   long size;
   void *bytes;
   const void *chunk;
   uint32_t chunk_bytes;
   struct dxbc_program program;
   struct dxbc_error perr;
   struct dxbc_nir_error terr;
   nir_shader *nir;
   bool print = argc > 2 && !strcmp(argv[2], "--print");

   if (argc < 2 || !(f = fopen(argv[1], "rb")))
      return 2;
   fseek(f, 0, SEEK_END);
   size = ftell(f);
   fseek(f, 0, SEEK_SET);
   bytes = malloc((size_t)size);
   if (!bytes || fread(bytes, 1, (size_t)size, f) != (size_t)size)
      return 2;
   fclose(f);
   if (!dxbc_container_find_chunk(bytes, (size_t)size, DXBC_FOURCC('S', 'H', 'E', 'X'), &chunk, &chunk_bytes) &&
       !dxbc_container_find_chunk(bytes, (size_t)size, DXBC_FOURCC('S', 'H', 'D', 'R'), &chunk, &chunk_bytes))
      return 2;
   if (dxbc_parse_program(chunk, chunk_bytes / 4, &program, &perr) != DXBC_OK) {
      printf("PARSE_ERROR %d\n", perr.status);
      return 1;
   }

   glsl_type_singleton_init_or_ref();
   if (program.type == DXBC_PROGRAM_HULL) {
      /* the output signature (OSGN: count, 8, then 24-byte entries with
       * the system value at +8 and the register at +16) names the control
       * point registers a pass-through phase copies, as the runtime's
       * D3D11DDIARG_TESSELLATION_IO_SIGNATURES do for the UMD */
      const void *sig;
      uint32_t sig_bytes, regs = 0, positions = 0;
      if (dxbc_container_find_chunk(bytes, (size_t)size, DXBC_FOURCC('O', 'S', 'G', 'N'), &sig,
                                    &sig_bytes) && sig_bytes >= 8) {
         const uint8_t *s = sig;
         uint32_t count;
         memcpy(&count, s, 4);
         for (uint32_t i = 0; i < count && 8 + 24 * (i + 1) <= sig_bytes; ++i) {
            uint32_t sv, reg;
            memcpy(&sv, s + 8 + 24 * i + 8, 4);
            memcpy(&reg, s + 8 + 24 * i + 16, 4);
            if (reg < 32) {
               regs |= 1u << reg;
               if (sv == 1)
                  positions |= 1u << reg;
            }
         }
      }
      nir = dxbc_to_nir_hs(&program, &agx_nir_options, regs, positions, &terr);
   } else {
      nir = dxbc_to_nir(&program, &agx_nir_options, &terr);
   }
   if (!nir) {
      printf("REFUSED %s (%s at %u)\n", terr.message, dxbc_opcode_name(terr.opcode) ?
             dxbc_opcode_name(terr.opcode) : "-", terr.offset);
      dxbc_program_free(&program);
      return 0;
   }
   nir_validate_shader(nir, "dxbc_to_nir");
   /* the binding ranges the frontend sizes its set_shader_buffers /
    * set_shader_images calls from */
   unsigned ssbos = nir->info.num_ssbos, images = nir->info.num_images;
   if (print)
      nir_print_shader(nir, stdout);

   /* the frontend's finalization (tgsi_to_nir's sequence) */
   dxbc_nir_finalize(nir);
   nir_validate_shader(nir, "finalize");

   /* agx_shader_initialize: I/O lowering, then agx_preprocess_nir */
   NIR_PASS(_, nir, nir_lower_io, nir_var_shader_in | nir_var_shader_out,
            type_size_vec4, nir_lower_io_lower_64bit_to_32 |
            nir_lower_io_use_interpolated_input_intrinsics);
   nir_shader_gather_info(nir, nir_shader_get_entrypoint(nir));
   agx_preprocess_nir(nir);
   nir_validate_shader(nir, "agx_preprocess_nir");

   /* the block size Dispatch launches, the groupshared bytes the AGX
    * backend allocates, and groupshared accesses left as derefs (the
    * backend has no deref lowering for them) */
   unsigned block[3], shared, shared_derefs = 0;
   dxbc_nir_compute_info(nir, block, &shared);
   /* fragment outputs stored outside the last block: AGX turns them into
    * exports, which must end the shader (agx_validate block form, EXP1294) */
   unsigned early_stores = 0;
   unsigned instrs = 0;
   nir_foreach_function_impl(impl, nir) {
      nir_block *last = nir_impl_last_block(impl);
      nir_foreach_block(blk, impl)
         nir_foreach_instr(instr, blk) {
            ++instrs;
            if (instr->type == nir_instr_type_deref &&
                nir_deref_mode_is(nir_instr_as_deref(instr), nir_var_mem_shared))
               ++shared_derefs;
            if (instr->type == nir_instr_type_intrinsic && blk != last &&
                nir->info.stage == MESA_SHADER_FRAGMENT &&
                nir_instr_as_intrinsic(instr)->intrinsic == nir_intrinsic_store_output)
               ++early_stores;
         }
   }
   printf("TRANSLATED stage %s instructions %u inputs 0x%llx outputs 0x%llx textures %u "
          "ssbos %u images %u block %ux%ux%u shared %u shared-derefs %u early-output-stores %u\n",
          mesa_shader_stage_name(nir->info.stage), instrs,
          (unsigned long long)nir->info.inputs_read,
          (unsigned long long)nir->info.outputs_written, BITSET_LAST_BIT(nir->info.textures_used),
          ssbos, images, block[0], block[1], block[2], shared, shared_derefs, early_stores);
   ralloc_free(nir);
   glsl_type_singleton_decref();
   dxbc_program_free(&program);
   free(bytes);
   return 0;
}
