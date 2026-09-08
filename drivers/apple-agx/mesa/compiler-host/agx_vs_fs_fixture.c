/* SPDX-License-Identifier: MIT
 * Project-owned fixture that executes pinned Asahi VS/FS driver lowering. */
#include "asahi/compiler/agx_compile.h"
#include "asahi/lib/agx_linker.h"
#include "asahi/lib/agx_uvs.h"
#include "asahi/isa/disasm.h"
#include "compiler/glsl_types.h"
#include "nir_builder.h"
#include "shader_enums.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

static unsigned
type_size(const struct glsl_type *type, bool bindless)
{
   return glsl_count_attribute_slots(type, false);
}

static uint64_t
hash_bytes(const unsigned char *data, size_t bytes)
{
   uint64_t hash = UINT64_C(0xcbf29ce484222325);
   for (size_t i = 0; i < bytes; ++i) {
      hash ^= data[i];
      hash *= UINT64_C(0x100000001b3);
   }
   return hash;
}

static nir_shader *
build_vertex(unsigned variant, struct agx_unlinked_uvs_layout *uvs)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_VERTEX, &agx_nir_options, "windows_ad03_vertex");
   nir_variable *position = nir_variable_create(
      b.shader, nir_var_shader_out, glsl_vec4_type(), "gl_Position");
   nir_variable *colour = nir_variable_create(
      b.shader, nir_var_shader_out, glsl_vec4_type(), "colour");
   position->data.location = VARYING_SLOT_POS;
   colour->data.location = VARYING_SLOT_VAR0;
   nir_store_var(&b, position,
                 nir_vec4(&b, nir_imm_float(&b, -0.5f),
                          nir_imm_float(&b, -0.5f),
                          nir_imm_float(&b, 0.0f), nir_imm_float(&b, 1.0f)),
                 0xfu);
   nir_store_var(&b, colour,
                 nir_vec4(&b, nir_imm_float(&b, variant ? 0.25f : 1.0f),
                          nir_imm_float(&b, variant ? 1.0f : 0.25f),
                          nir_imm_float(&b, 0.5f), nir_imm_float(&b, 1.0f)),
                 0xfu);
   nir_lower_io(b.shader, nir_var_shader_in | nir_var_shader_out, type_size,
                nir_lower_io_lower_64bit_to_32 |
                   nir_lower_io_use_interpolated_input_intrinsics);
   nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));
   BITSET_DECLARE(attrib_components_read, VERT_ATTRIB_MAX * 4) = {0};
   agx_nir_gather_vs_inputs(b.shader, attrib_components_read);
   agx_nir_lower_vs_input_to_prolog(b.shader);
   nir_lower_io_to_scalar(b.shader, nir_var_shader_out, NULL, NULL);
   agx_nir_lower_uvs(b.shader, uvs);
   agx_preprocess_nir(b.shader);
   return b.shader;
}

static nir_shader *
build_fragment(unsigned variant, struct agx_fs_epilog_link_info *epilog)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_FRAGMENT, &agx_nir_options, "windows_ad03_fragment");
   nir_variable *colour = nir_variable_create(
      b.shader, nir_var_shader_out, glsl_vec4_type(), "colour");
   colour->data.location = FRAG_RESULT_DATA0;
   nir_store_var(&b, colour,
                 nir_vec4(&b, nir_imm_float(&b, variant ? 0.75f : 0.1f),
                          nir_imm_float(&b, variant ? 0.2f : 0.8f),
                          nir_imm_float(&b, 0.4f), nir_imm_float(&b, 1.0f)),
                 0xfu);
   nir_lower_io(b.shader, nir_var_shader_in | nir_var_shader_out, type_size,
                nir_lower_io_lower_64bit_to_32 |
                   nir_lower_io_use_interpolated_input_intrinsics);
   nir_shader_gather_info(b.shader, nir_shader_get_entrypoint(b.shader));
   agx_preprocess_nir(b.shader);
   agx_nir_lower_discard_zs_emit(b.shader);
   agx_nir_lower_fs_output_to_epilog(b.shader, epilog);
   agx_nir_lower_sample_mask(b.shader);
   agx_nir_lower_fs_active_samples_to_register(b.shader);
   return b.shader;
}

static int
write_binary(const char *directory, const char *name,
             const struct agx_shader_part *part)
{
   char path[1024];
   if (snprintf(path, sizeof(path), "%s/%s.bin", directory, name) < 0)
      return 0;
   FILE *file = fopen(path, "wb");
   if (!file)
      return 0;
   size_t written = fwrite(part->binary, 1, part->info.binary_size, file);
   int closed = fclose(file);
   return written == part->info.binary_size && closed == 0;
}

static int
write_disassembly(const char *directory, const char *name,
                  const struct agx_shader_part *part)
{
   char path[1024];
   if (part->info.main_offset > part->info.binary_size ||
       part->info.main_size > part->info.binary_size - part->info.main_offset ||
       snprintf(path, sizeof(path), "%s/%s.asm", directory, name) < 0)
      return 0;
   FILE *file = fopen(path, "w");
   if (!file)
      return 0;
   bool errors = agx2_disassemble(
      part->binary + part->info.main_offset, part->info.main_size, file);
   int closed = fclose(file);
   return !errors && closed == 0;
}

static void
print_part(const char *name, const struct agx_shader_part *part)
{
   printf("\"%s\":{\"binary_bytes\":%u,\"main_offset\":%u,"
          "\"main_bytes\":%u,\"gprs\":%u,\"preamble_gprs\":%u,"
          "\"scratch_bytes\":%u,\"rodata_offset\":%u,"
          "\"rodata_halfwords\":%u,\"fnv1a64\":\"0x%016llx\"}",
          name, part->info.binary_size, part->info.main_offset,
          part->info.main_size, part->info.nr_gprs,
          part->info.nr_preamble_gprs, part->info.scratch_size,
          part->info.rodata.offset, part->info.rodata.size_16,
          (unsigned long long)hash_bytes(part->binary,
                                         part->info.binary_size));
}

int
main(int argc, char **argv)
{
   if (argc != 3)
      return 2;
   char *end = NULL;
   errno = 0;
   unsigned long parsed = strtoul(argv[2], &end, 10);
   if (errno || end == argv[2] || *end != '\0' || parsed > 1)
      return 2;
   unsigned variant = (unsigned)parsed;
   struct agx_unlinked_uvs_layout uvs = {0};
   struct agx_fs_epilog_link_info epilog = {0};
   struct agx_shader_key key = {.promote_constants = true};
   struct agx_shader_part vs = {0};
   struct agx_shader_part fs = {0};
   glsl_type_singleton_init_or_ref();
   nir_shader *vs_nir = build_vertex(variant, &uvs);
   nir_shader *fs_nir = build_fragment(variant, &epilog);
   agx_compile_shader_nir(vs_nir, &key, &vs);
   agx_compile_shader_nir(fs_nir, &key, &fs);
   if (!vs.binary || !vs.info.binary_size || !fs.binary ||
       !fs.info.binary_size || !write_binary(argv[1], "vertex", &vs) ||
       !write_binary(argv[1], "fragment", &fs) ||
       !write_disassembly(argv[1], "vertex", &vs) ||
       !write_disassembly(argv[1], "fragment", &fs))
      return 1;
   printf("{\"schema\":1,\"variant\":%u,\"uvs_size\":%u,"
          "\"uvs_user_size\":%u,\"epilog_loc_written\":%u,",
          variant, uvs.size, uvs.user_size, epilog.loc_written);
   print_part("vertex", &vs);
   printf(",");
   print_part("fragment", &fs);
   printf("}\n");
   free(vs.binary);
   free(fs.binary);
   ralloc_free(vs_nir);
   ralloc_free(fs_nir);
   glsl_type_singleton_decref();
   return 0;
}
