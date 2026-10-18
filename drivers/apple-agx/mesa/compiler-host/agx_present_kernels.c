/* SPDX-License-Identifier: MIT
 * Project-owned generator of the KMD's present-blt kernels (G2 HW2), built
 * with nir_builder and compiled by the pinned Mesa AGX compiler, the way
 * asahi_clc compiles precompiled kernels (agx_preprocess_nir, then
 * agx_compile_shader_nir with promoted constants and the argument block
 * reserved at the start of the uniform file).
 *
 * Arguments are 16-bit uniform registers from u0, mapped by the KMD with a
 * USC Uniform word (agx_usc_words_precomp layout). Offsets are in halfs and
 * mirrored by APPLE_AGX_PRESENT_ARG_* in the generated header:
 *   0  src address (u64)   4  dst address (u64)
 *   8  src pitch bytes     10 dst pitch bytes
 *   12 sub-rect x0         14 sub-rect y0
 *   16 sub-rect width      18 sub-rect height
 *   20 dst rect x0         22 dst rect y0
 *   24 src rect x0         26 src rect y0
 *   28 step x (16.16)      30 step y (16.16)
 *   32 src max x           34 src max y
 *   36 fill colour
 * One invocation writes one 32-bit pixel. "copy" maps the destination pixel
 * to the nearest source pixel (step = src extent / dst extent in 16.16,
 * sampled at pixel centres, clamped to the source rect); "fill" stores the
 * colour. */
#include "asahi/compiler/agx_compile.h"
#include "compiler/glsl_types.h"
#include "nir_builder.h"
#include "shader_enums.h"
#include "util/macros.h"

#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

enum {
   ARG_SRC = 0, ARG_DST = 4, ARG_SRC_PITCH = 8, ARG_DST_PITCH = 10,
   ARG_ORIGIN_X = 12, ARG_ORIGIN_Y = 14, ARG_WIDTH = 16, ARG_HEIGHT = 18,
   ARG_MAP_X = 20, ARG_MAP_Y = 22, ARG_SRC_X = 24, ARG_SRC_Y = 26,
   ARG_STEP_X = 28, ARG_STEP_Y = 30, ARG_SRC_MAX_X = 32, ARG_SRC_MAX_Y = 34,
   ARG_FILL = 36, ARG_HALFS = 38,
};
enum { LOCAL_X = 32, LOCAL_Y = 8 };

static nir_def *
arg32(nir_builder *b, unsigned half)
{
   return nir_load_preamble(b, 1, 32, .base = half);
}

static nir_def *
arg64(nir_builder *b, unsigned half)
{
   return nir_load_preamble(b, 1, 64, .base = half);
}

static nir_def *
pixel_address(nir_builder *b, nir_def *base, nir_def *pitch, nir_def *x,
              nir_def *y)
{
   nir_def *offset = nir_iadd(b, nir_imul(b, y, pitch), nir_ishl_imm(b, x, 2));
   return nir_iadd(b, base, nir_u2u64(b, offset));
}

static nir_def *
nearest(nir_builder *b, nir_def *dst, unsigned map, unsigned src,
        unsigned step, unsigned max)
{
   nir_def *rel = nir_isub(b, dst, arg32(b, map));
   nir_def *scaled = nir_iadd(b, nir_imul(b, rel, arg32(b, step)),
                              nir_ushr_imm(b, arg32(b, step), 1));
   nir_def *coord = nir_iadd(b, arg32(b, src), nir_ushr_imm(b, scaled, 16));
   return nir_umin(b, coord, arg32(b, max));
}

static nir_shader *
build(bool fill)
{
   nir_builder b = nir_builder_init_simple_shader(
      MESA_SHADER_COMPUTE, &agx_nir_options,
      fill ? "apple_agx_present_fill" : "apple_agx_present_copy");
   b.shader->info.workgroup_size[0] = LOCAL_X;
   b.shader->info.workgroup_size[1] = LOCAL_Y;
   b.shader->info.workgroup_size[2] = 1;
   nir_def *gid = nir_load_global_invocation_id(&b, 32);
   nir_def *x = nir_channel(&b, gid, 0), *y = nir_channel(&b, gid, 1);
   nir_def *inside = nir_iand(&b, nir_ult(&b, x, arg32(&b, ARG_WIDTH)),
                              nir_ult(&b, y, arg32(&b, ARG_HEIGHT)));
   nir_push_if(&b, inside);
   {
      nir_def *dx = nir_iadd(&b, arg32(&b, ARG_ORIGIN_X), x);
      nir_def *dy = nir_iadd(&b, arg32(&b, ARG_ORIGIN_Y), y);
      nir_def *value;
      if (fill) {
         value = arg32(&b, ARG_FILL);
      } else {
         nir_def *sx = nearest(&b, dx, ARG_MAP_X, ARG_SRC_X, ARG_STEP_X,
                               ARG_SRC_MAX_X);
         nir_def *sy = nearest(&b, dy, ARG_MAP_Y, ARG_SRC_Y, ARG_STEP_Y,
                               ARG_SRC_MAX_Y);
         value = nir_load_global(&b, 1, 32,
                                 pixel_address(&b, arg64(&b, ARG_SRC),
                                               arg32(&b, ARG_SRC_PITCH), sx, sy),
                                 .align_mul = 4);
      }
      nir_store_global(&b, value,
                       pixel_address(&b, arg64(&b, ARG_DST),
                                     arg32(&b, ARG_DST_PITCH), dx, dy),
                       .align_mul = 4);
   }
   nir_pop_if(&b, NULL);
   agx_preprocess_nir(b.shader);
   return b.shader;
}

static uint64_t
fnv1a64(const unsigned char *data, size_t bytes)
{
   uint64_t hash = UINT64_C(0xcbf29ce484222325);
   for (size_t i = 0; i < bytes; ++i) {
      hash ^= data[i];
      hash *= UINT64_C(0x100000001b3);
   }
   return hash;
}

static int
emit(const char *directory, const char *name, bool fill, int first)
{
   char path[1024];
   nir_shader *nir = build(fill);
   struct agx_shader_key key = {
      .promote_constants = true,
      .promote_textures = true,
      .reserved_preamble = ARG_HALFS,
   };
   struct agx_shader_part part = {0};
   agx_compile_shader_nir(nir, &key, &part);
   if (!part.binary || !part.info.binary_size || part.info.scratch_size ||
       part.info.preamble_scratch_size || part.info.uses_txf)
      return 0;
   if (snprintf(path, sizeof(path), "%s/%s.bin", directory, name) < 0)
      return 0;
   FILE *file = fopen(path, "wb");
   if (!file ||
       fwrite(part.binary, 1, part.info.binary_size, file) !=
          part.info.binary_size ||
       fclose(file) != 0)
      return 0;
   printf("%s\"%s\":{\"binary_bytes\":%u,\"main_offset\":%u,"
          "\"preamble_offset\":%u,\"gprs\":%u,\"preamble_gprs\":%u,"
          "\"push_count\":%u,\"rodata_offset\":%u,\"rodata_base_uniform\":%u,"
          "\"rodata_halfs\":%u,\"workgroup\":[%u,%u,%u],\"local_size\":%u,"
          "\"fnv1a64\":\"0x%016llx\"}",
          first ? "" : ",", name, part.info.binary_size, part.info.main_offset,
          part.info.preamble_offset, part.info.nr_gprs,
          part.info.nr_preamble_gprs, part.info.push_count,
          part.info.rodata.offset, part.info.rodata.base_uniform,
          part.info.rodata.size_16, part.info.workgroup_size[0],
          part.info.workgroup_size[1], part.info.workgroup_size[2],
          part.info.local_size,
          (unsigned long long)fnv1a64(part.binary, part.info.binary_size));
   free(part.binary);
   ralloc_free(nir);
   return 1;
}

int
main(int argc, char **argv)
{
   if (argc != 2)
      return 2;
   glsl_type_singleton_init_or_ref();
   printf("{\"schema\":1,\"arg_halfs\":%u,\"workgroup\":[%u,%u,1],"
          "\"args\":{\"SRC\":%u,\"DST\":%u,\"SRC_PITCH\":%u,"
          "\"DST_PITCH\":%u,\"ORIGIN_X\":%u,\"ORIGIN_Y\":%u,\"WIDTH\":%u,"
          "\"HEIGHT\":%u,\"MAP_X\":%u,\"MAP_Y\":%u,\"SRC_X\":%u,"
          "\"SRC_Y\":%u,\"STEP_X\":%u,\"STEP_Y\":%u,\"SRC_MAX_X\":%u,"
          "\"SRC_MAX_Y\":%u,\"FILL\":%u},\"kernels\":{",
          ARG_HALFS, LOCAL_X, LOCAL_Y, ARG_SRC, ARG_DST, ARG_SRC_PITCH,
          ARG_DST_PITCH, ARG_ORIGIN_X, ARG_ORIGIN_Y, ARG_WIDTH, ARG_HEIGHT,
          ARG_MAP_X, ARG_MAP_Y, ARG_SRC_X, ARG_SRC_Y, ARG_STEP_X, ARG_STEP_Y,
          ARG_SRC_MAX_X, ARG_SRC_MAX_Y, ARG_FILL);
   int ok = emit(argv[1], "copy", false, 1) && emit(argv[1], "fill", true, 0);
   printf("}}\n");
   glsl_type_singleton_decref();
   return ok ? 0 : 1;
}
