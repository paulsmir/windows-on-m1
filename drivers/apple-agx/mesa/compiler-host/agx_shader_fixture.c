/* SPDX-License-Identifier: MIT
 * Project-owned deterministic fixture for the pinned Mesa AGX compiler. */
#include "asahi/compiler/agx_compile.h"
#include "compiler/glsl_types.h"
#include "nir_builder.h"
#include "shader_enums.h"
#include "util/macros.h"

#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>

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
build_compute(unsigned variant)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, &agx_nir_options, "windows_ad03_compute");
   nir_store_global(&b, nir_imm_int(&b, (int)variant + 1),
                    nir_imm_int64(&b, UINT64_C(0x100000000)),
                    .align_mul = 4);
   agx_preprocess_nir(b.shader);
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

static void
print_metadata(const char *name, const struct agx_shader_part *part)
{
   printf("\"%s\":{\"binary_bytes\":%u,\"main_offset\":%u,"
          "\"preamble_offset\":%u,\"main_bytes\":%u,\"gprs\":%u,"
          "\"preamble_gprs\":%u,\"scratch_bytes\":%u,"
          "\"rodata_offset\":%u,\"rodata_halfwords\":%u,"
          "\"fnv1a64\":\"0x%016llx\"}", name,
          part->info.binary_size, part->info.main_offset,
          part->info.preamble_offset, part->info.main_size,
          part->info.nr_gprs, part->info.nr_preamble_gprs,
          part->info.scratch_size, part->info.rodata.offset,
          part->info.rodata.size_16,
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
   glsl_type_singleton_init_or_ref();
   nir_shader *cs_nir = build_compute(variant);
   struct agx_shader_key cs_key = {.promote_constants = true};
   struct agx_shader_part cs = {0};
   agx_compile_shader_nir(cs_nir, &cs_key, &cs);
   if (!cs.binary || !cs.info.binary_size ||
       !write_binary(argv[1], "compute", &cs))
      return 1;
   printf("{\"schema\":1,\"variant\":%u,", variant);
   print_metadata("compute", &cs);
   printf("}\n");
   free(cs.binary);
   ralloc_free(cs_nir);
   glsl_type_singleton_decref();
   return 0;
}
