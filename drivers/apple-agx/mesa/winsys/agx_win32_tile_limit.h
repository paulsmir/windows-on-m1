/* SPDX-License-Identifier: MIT
 * rc110: the tile-buffer byte limit that picks the utile size.
 * Included by the projected src/asahi/lib/agx_tilebuffer.c
 * (mesa/scripts/build-native-asahi-state.py).
 *
 * Asahi's MAX_BYTES_PER_TILE is 32767 ("the maximum number of bytes per tile
 * on G13G"), so a single-sampled layout of 32 bytes per pixel -- GravityMark's
 * G-buffer, four RGBA16F targets -- is one byte over the 32x32 utile
 * (32 x 1024 = 32768) and renders in 32x16 utiles: twice the tiles of the
 * main pass (EXP2048a: 8.35 ms of fragment time at FHD for 4050 tiles). The
 * USC Shared word encodes the allocation in 256-byte groups (cmdbuf.xml:
 * 8 bits), the KMD's tile-buffer blocks are 2048-byte units
 * (render_dynamic_overlay.c), and a compute threadgroup may allocate the full
 * 32 KiB, so 32768 is representable; whether the render path accepts it is
 * the hardware question.
 *
 * APPLE_AGX_TILE_BUFFER_BYTES=N (read once per process):
 *   N in 8192..32768 (decimal, or 0x-prefixed hex): the limit;
 *   unset, empty, out of range or not a number: Asahi's 32767, the control
 *   stream is byte-identical to the unprojected producer.
 * The KMD and the UMD submit check accept up to APPLE_AGX_G4_TILE_BUFFER_BYTES
 * (shared/include/apple_agx_g4_submit.h, 32768), so a larger value is never
 * valid here.
 */
#ifndef AGX_WIN32_TILE_LIMIT_H
#define AGX_WIN32_TILE_LIMIT_H

#include <stdlib.h>

#define AGX_WIN32_TILE_BYTES_ASAHI 32767u
#define AGX_WIN32_TILE_BYTES_MIN 8192u
#define AGX_WIN32_TILE_BYTES_MAX 32768u

/* The limit a variable value selects: Asahi's for anything invalid. */
static inline unsigned
agx_win32_tile_limit_parse(const char *value)
{
   if (!value || !value[0])
      return AGX_WIN32_TILE_BYTES_ASAHI;
   char *end = NULL;
   unsigned long bytes = strtoul(value, &end, 0);
   if (!end || end == value || *end || bytes < AGX_WIN32_TILE_BYTES_MIN ||
       bytes > AGX_WIN32_TILE_BYTES_MAX)
      return AGX_WIN32_TILE_BYTES_ASAHI;
   return (unsigned)bytes;
}

static inline unsigned
agx_win32_max_bytes_per_tile(void)
{
   static unsigned limit;
   if (!limit)
      limit = agx_win32_tile_limit_parse(getenv("APPLE_AGX_TILE_BUFFER_BYTES"));
   return limit;
}

#endif
