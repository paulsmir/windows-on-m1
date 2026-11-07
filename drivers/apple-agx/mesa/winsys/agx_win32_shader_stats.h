/* RC74: compiled AGX shader statistics as receipts (receipt-only).
 *
 * Asahi reports every compiled shader part through the context's debug
 * callback (agx_state.c agx_compile_nir -> agx2_stats_util_debug, a
 * UTIL_DEBUG_TYPE_SHADER_INFO message whose format Mesa generates from
 * src/util/shader_stats.xml, isa AGX2):
 *   "<stage> shader: <instrs> instrs, <alu> alu, <fscib> fscib, <ic> ic,
 *    <code> code size, <gprs> gprs, <uniforms> uniforms, <scratch> scratch,
 *    <threads> threads, <loops> loops, <spills>:<fills> spills:fills,
 *    <preamble> preamble inst"
 * The D3D12 owner installs such a callback (only while UMD diagnostics are
 * enabled) and tags each message with the D3D shader it compiles: the FNV-1a
 * 64 hash of the shader's tokens (the same hash APPLE_AGX_D3D12_SHADER_DUMP_DIR
 * names its captures with), its stage and size. No compiled code changes.
 * SPDX-License-Identifier: MIT */
#ifndef AGX_WIN32_SHADER_STATS_H
#define AGX_WIN32_SHADER_STATS_H

#include <stdint.h>
#include <stdio.h>
#include <string.h>

/* receipt "measure-d3d12-shader-stats" values (16, the diagnostic maximum) */
enum {
   AGX_WIN32_STATS_HASH_HI,
   AGX_WIN32_STATS_HASH_LO,
   AGX_WIN32_STATS_STAGE,  /* mesa_shader_stage of the part; | 0x100: no D3D shader tag (internal part) */
   AGX_WIN32_STATS_BYTES,  /* D3D shader size (DXBC tokens or DXIL program part) */
   AGX_WIN32_STATS_INSTRS,
   AGX_WIN32_STATS_ALU,    /* Asahi's estimated ALU cycles */
   AGX_WIN32_STATS_GPRS,   /* 16-bit registers */
   AGX_WIN32_STATS_THREADS, /* maximum threads in flight per core (occupancy) */
   AGX_WIN32_STATS_SCRATCH, /* bytes per thread */
   AGX_WIN32_STATS_SPILLS,
   AGX_WIN32_STATS_FILLS,
   AGX_WIN32_STATS_LOOPS,
   AGX_WIN32_STATS_CODE,   /* binary bytes */
   AGX_WIN32_STATS_PREAMBLE, /* preamble instructions */
   AGX_WIN32_STATS_BLOCK,  /* compute: x | y << 11 | z << 22; else 0 */
   AGX_WIN32_STATS_SHARED, /* compute: threadgroup memory bytes; else 0 */
   AGX_WIN32_STATS_COUNT
};

struct agx_win32_shader_stats {
   char stage[8];
   unsigned instrs, alu, fscib, ic, code, gprs, uniforms, scratch, threads, loops, spills, fills, preamble;
};

/* Parses one formatted Asahi statistics message; 0 when it is not one. */
static inline int
agx_win32_parse_shader_stats(const char *message, struct agx_win32_shader_stats *out)
{
   memset(out, 0, sizeof(*out));
   int consumed = 0;
#define AGX_WIN32_STATS_FORMAT \
   "%7s shader: %u instrs, %u alu, %u fscib, %u ic, %u code size, %u gprs, %u uniforms, " \
   "%u scratch, %u threads, %u loops, %u:%u spills:fills, %u preamble inst%n"
   /* The UMD builds with /sdl /WX, which rejects sscanf (C4996). */
#ifdef _MSC_VER
   const int fields =
      sscanf_s(message, AGX_WIN32_STATS_FORMAT, out->stage, (unsigned)sizeof(out->stage), &out->instrs, &out->alu, &out->fscib, &out->ic, &out->code, &out->gprs, &out->uniforms,
             &out->scratch, &out->threads, &out->loops, &out->spills, &out->fills, &out->preamble, &consumed);
#else
   const int fields =
      sscanf(message, AGX_WIN32_STATS_FORMAT, out->stage, &out->instrs, &out->alu, &out->fscib, &out->ic, &out->code,
             &out->gprs, &out->uniforms, &out->scratch, &out->threads, &out->loops, &out->spills, &out->fills,
             &out->preamble, &consumed);
#endif
#undef AGX_WIN32_STATS_FORMAT
   return fields == 14 && consumed > 0;
}

/* _mesa_shader_stage_to_abbrev's names of the stages Asahi compiles, as
 * mesa_shader_stage values; -1 for another name */
static inline int
agx_win32_shader_stats_stage(const char *abbrev)
{
   static const char *const names[] = {"VS", "TCS", "TES", "GS", "FS", "CS"};
   for (int i = 0; i < 6; ++i)
      if (!strcmp(abbrev, names[i]))
         return i; /* MESA_SHADER_VERTEX .. MESA_SHADER_COMPUTE */
   return -1;
}

/* the FNV-1a 64 hash of a shader's bytes (dump_shader_input's file name) */
static inline uint64_t
agx_win32_shader_hash(const void *data, size_t bytes)
{
   uint64_t hash = 0xcbf29ce484222325ull;
   for (size_t i = 0; i < bytes; ++i)
      hash = (hash ^ ((const unsigned char *)data)[i]) * 0x100000001b3ull;
   return hash;
}

#endif
