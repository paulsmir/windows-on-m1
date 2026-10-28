/* SPDX-License-Identifier: MIT
 * rc101: lean CDM barriers between consecutive application dispatches.
 * Included by the projected agx_state.c after libagx_dgc.h
 * (mesa/scripts/native-asahi-lean-cdm-barrier.py).
 *
 * Asahi emits agx_cdm_barrier (libagx_dgc.h) after every CDM launch: on G13G
 * the word 0x600fffff, Unk 0..19 including the USC cache invalidate, set "to
 * be safe" because blits need the PBE cache flushed and the texture cache
 * invalidated between dispatches and nobody knows which bits do that. The
 * bits that Asahi sets on purpose are the ones it uses at the start of every
 * compute batch (agx_batch_init_state, for GPU-CPU coherency): USC cache
 * invalidate and Unk 5/6/8 (plus Unk 4 on G13X), 0x60000168 on G13G. That
 * is the lean barrier.
 *
 * APPLE_AGX_D3D12_LEAN_CDM_BARRIER (read once per process):
 *   unset, 0 or anything else: Asahi's full barrier after every launch, the
 *        control stream is byte-identical to the unprojected producer;
 *   1    every barrier between two consecutive application dispatches of one
 *        CDM stream is lean (the hardware experiment: D3D12 UAV and SRV
 *        buffers are mostly texel buffers and images on this driver, so this
 *        is what decides whether the lean bits cover the PBE and texture
 *        caches);
 *   2    conservative: lean only when the earlier dispatch writes no image
 *        (no image store or atomic: no PBE write) and the later reads no
 *        texture or image and has a direct grid (its indirect arguments are
 *        not fetched behind a lean barrier).
 * APPLE_AGX_D3D12_LEAN_CDM_BITS=0xNNNNN (optional, with 1 or 2): bits 0..19
 *   of the lean barrier instead of the default, to bisect on hardware which
 *   unknown bits a mismatching image needs; 0xfffff makes the lean word the
 *   full one. Other values (above 0xfffff, empty, not a number) are ignored.
 * Everything else keeps the full barrier: precompiled libagx kernels (buffer
 * copies, queries, decompression, the indirect stream), compute blits and
 * geometry/tessellation launches, on both sides of them, and the last barrier
 * of every CDM stream (batch end, before any render job, copy, CPU access or
 * other submission that depends on it).
 *
 * The barrier after a launch is written lean provisionally; the next launch
 * of the batch keeps it lean or rewrites it in place to the full word (same
 * length), and agx_win32_cdm_finish rewrites it before the stream is
 * terminated.
 */
#ifndef AGX_WIN32_CDM_BARRIER_H
#define AGX_WIN32_CDM_BARRIER_H

#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

enum {
   AGX_WIN32_LEAN_CDM_OFF = 0,
   AGX_WIN32_LEAN_CDM_ALL = 1,
   AGX_WIN32_LEAN_CDM_MEMORY = 2,
};

/* Launch class bits. 0 = not an application dispatch: always full. */
#define AGX_WIN32_CDM_APP              (1u << 0)
#define AGX_WIN32_CDM_NO_TEXTURE_READS (1u << 1) /* no tex instr, no image read */
#define AGX_WIN32_CDM_NO_IMAGE_WRITES  (1u << 2) /* no image store or atomic */
#define AGX_WIN32_CDM_INDIRECT         (1u << 3) /* the grid is fetched from memory */

#define AGX_WIN32_CDM_BARRIER_BITS 0xfffffu /* Unk 0..19, the full barrier's bits */

static inline unsigned
agx_win32_lean_cdm_parse(const char *value)
{
   if (!value || !value[0] || value[1])
      return AGX_WIN32_LEAN_CDM_OFF;
   return value[0] == '1' ? AGX_WIN32_LEAN_CDM_ALL :
          value[0] == '2' ? AGX_WIN32_LEAN_CDM_MEMORY : AGX_WIN32_LEAN_CDM_OFF;
}

/* The bits override, or ~0u when absent or invalid. */
static inline uint32_t
agx_win32_lean_cdm_parse_bits(const char *value)
{
   if (!value || !value[0])
      return ~0u;
   char *end = NULL;
   unsigned long bits = strtoul(value, &end, 0);
   if (!end || *end || bits > AGX_WIN32_CDM_BARRIER_BITS)
      return ~0u;
   return (uint32_t)bits;
}

/* A benign race: every thread computes the same values. */
static inline unsigned
agx_win32_lean_cdm_mode(void)
{
   static int mode = -1;
   if (mode < 0)
      mode = (int)agx_win32_lean_cdm_parse(getenv("APPLE_AGX_D3D12_LEAN_CDM_BARRIER"));
   return (unsigned)mode;
}

static inline uint32_t
agx_win32_lean_cdm_bits(void)
{
   static int parsed;
   static uint32_t bits;
   if (!parsed) {
      bits = agx_win32_lean_cdm_parse_bits(getenv("APPLE_AGX_D3D12_LEAN_CDM_BITS"));
      parsed = 1;
   }
   return bits;
}

/* May the barrier after a launch of class `writer` be lean at all? */
static inline bool
agx_win32_cdm_writer_lean(unsigned mode, unsigned writer)
{
   if (!(writer & AGX_WIN32_CDM_APP))
      return false;
   if (mode == AGX_WIN32_LEAN_CDM_ALL)
      return true;
   return mode == AGX_WIN32_LEAN_CDM_MEMORY && (writer & AGX_WIN32_CDM_NO_IMAGE_WRITES);
}

/* May the barrier between `writer` and the next launch `reader` stay lean? */
static inline bool
agx_win32_cdm_pair_lean(unsigned mode, unsigned writer, unsigned reader)
{
   if (!agx_win32_cdm_writer_lean(mode, writer) || !(reader & AGX_WIN32_CDM_APP))
      return false;
   if (mode == AGX_WIN32_LEAN_CDM_ALL)
      return true;
   return (reader & AGX_WIN32_CDM_NO_TEXTURE_READS) && !(reader & AGX_WIN32_CDM_INDIRECT);
}

#ifndef AGX_WIN32_CDM_BARRIER_NO_STREAM
/* The lean word: the batch-start barrier's bits, or the override. */
static inline uint32_t *
agx_win32_cdm_lean_barrier(uint32_t *out, enum agx_chip chip, uint32_t bits)
{
   if (bits <= AGX_WIN32_CDM_BARRIER_BITS) {
      uint32_t word = ((uint32_t)AGX_CDM_BLOCK_TYPE_BARRIER << 29) | bits;
      memcpy(out, &word, sizeof(word));
      return out + 1;
   }
   agx_push(out, CDM_BARRIER, cfg) {
      cfg.usc_cache_inval = true;
      cfg.unk_5 = true;
      cfg.unk_6 = true;
      cfg.unk_8 = true;
      if (chip == AGX_CHIP_G13X)
         cfg.unk_4 = true;
   }
   return out;
}

/* Before a launch of class `cls`: keep or upgrade the batch's provisional
 * lean barrier. ctx->windows_cdm_barriers: barriers left lean, full ones. */
static inline void
agx_win32_cdm_before_launch(struct agx_batch *batch, enum agx_chip chip,
                            unsigned mode, unsigned cls)
{
   uint32_t *pending = batch->windows_cdm_lean;
   if (!pending)
      return;
   batch->windows_cdm_lean = NULL;
   if (agx_win32_cdm_pair_lean(mode, batch->windows_cdm_lean_writer, cls)) {
      ++batch->ctx->windows_cdm_barriers[0];
      return;
   }
   agx_cdm_barrier(pending, chip);
   ++batch->ctx->windows_cdm_barriers[1];
}

/* After a launch of class `cls`: its barrier, lean only provisionally. */
static inline uint32_t *
agx_win32_cdm_after_launch(struct agx_batch *batch, uint32_t *out,
                           enum agx_chip chip, unsigned mode, unsigned cls)
{
   if (!agx_win32_cdm_writer_lean(mode, cls)) {
      ++batch->ctx->windows_cdm_barriers[1];
      return agx_cdm_barrier(out, chip);
   }
   batch->windows_cdm_lean = out;
   batch->windows_cdm_lean_writer = cls;
   uint32_t *end = agx_win32_cdm_lean_barrier(out, chip, agx_win32_lean_cdm_bits());
   memcpy(&batch->ctx->windows_cdm_barriers[3], out, sizeof(uint32_t));
   return end;
}

/* The last barrier of a CDM stream is always the full one. */
static inline void
agx_win32_cdm_finish(struct agx_batch *batch, enum agx_chip chip)
{
   if (!batch->windows_cdm_lean)
      return;
   agx_cdm_barrier(batch->windows_cdm_lean, chip);
   batch->windows_cdm_lean = NULL;
   ++batch->ctx->windows_cdm_barriers[1];
}
#endif /* !AGX_WIN32_CDM_BARRIER_NO_STREAM */

#ifndef AGX_WIN32_CDM_BARRIER_NO_NIR
/* The class bits of a compute shader, from its NIR after the early texture
 * and binding lowering (agx_shader_initialize): any texture instruction and
 * any image intrinsic (bindful, deref or bindless) other than a plain store
 * reads through the texture unit; an image store or atomic writes an image.
 * The bindless handle producer (bindless_image_agx) accesses nothing. */
static inline unsigned
agx_win32_cdm_shader_class(nir_shader *nir)
{
   bool texture = false, image_write = false;
   nir_foreach_function_impl(impl, nir) {
      nir_foreach_block(block, impl) {
         nir_foreach_instr(instr, block) {
            if (instr->type == nir_instr_type_tex) {
               texture = true;
               continue;
            }
            if (instr->type != nir_instr_type_intrinsic)
               continue;
            const char *name =
               nir_intrinsic_infos[nir_instr_as_intrinsic(instr)->intrinsic].name;
            if (!strstr(name, "image") || !strcmp(name, "bindless_image_agx"))
               continue;
            const bool atomic = strstr(name, "atomic") != NULL;
            if (atomic || strstr(name, "store"))
               image_write = true;
            if (atomic || !strstr(name, "store"))
               texture = true;
         }
      }
   }
   return (texture ? 0u : AGX_WIN32_CDM_NO_TEXTURE_READS) |
          (image_write ? 0u : AGX_WIN32_CDM_NO_IMAGE_WRITES);
}
#endif

#endif
