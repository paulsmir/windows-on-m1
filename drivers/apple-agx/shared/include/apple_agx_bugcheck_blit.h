#ifndef APPLE_AGX_BUGCHECK_BLIT_H
#define APPLE_AGX_BUGCHECK_BLIT_H

/* A2: pure, host-testable body of DxgkDdiSystemDisplayWrite. Microsoft: called
 * at any IRQL during a bugcheck; the source is non-paged, 32 bpp
 * (D3DDDIFMT_X8R8G8B8), written with the CPU at PositionX/PositionY. This copies
 * the clipped rectangle into a 32 bpp linear surface with 32-bit stores and the
 * alpha byte forced to 0xFF (whether DCP blends layer 0 by alpha is unknown;
 * forcing it costs nothing for X8). No CRT and no kernel call: the caller owns
 * the final barrier (DSB) on the write-combined mapping. 24 bpp sources and
 * rotation are not supported. */

#include "apple_agx_scanout.h"

typedef enum _APPLE_AGX_BUGCHECK_BLIT_RESULT {
  AppleAgxBugcheckBlitOk = 0,       /* the whole rectangle was written */
  AppleAgxBugcheckBlitClipped = 1,  /* partly or wholly outside: only the inside written */
  AppleAgxBugcheckBlitRejected = 2, /* invalid arguments: nothing written */
  AppleAgxBugcheckBlitEmpty = 3,    /* zero width or height: nothing written */
} APPLE_AGX_BUGCHECK_BLIT_RESULT;

static inline APPLE_AGX_BUGCHECK_BLIT_RESULT AppleAgxBugcheckBlitRect(
    void *Destination, APPLE_AGX_SCANOUT_U32 Pitch, APPLE_AGX_SCANOUT_U32 Width,
    APPLE_AGX_SCANOUT_U32 Height, const void *Source,
    APPLE_AGX_SCANOUT_U32 SourceStride, APPLE_AGX_SCANOUT_U32 SourceWidth,
    APPLE_AGX_SCANOUT_U32 SourceHeight, APPLE_AGX_SCANOUT_U32 PositionX,
    APPLE_AGX_SCANOUT_U32 PositionY) {
  volatile unsigned char *dst_base = (volatile unsigned char *)Destination;
  const unsigned char *src_base = (const unsigned char *)Source;
  APPLE_AGX_SCANOUT_U64 copy_w, copy_h, x, y;
  APPLE_AGX_BUGCHECK_BLIT_RESULT result = AppleAgxBugcheckBlitOk;

  if (Destination == 0 || Source == 0)
    return AppleAgxBugcheckBlitRejected;
  /* Destination: a 32 bpp surface with 4-byte aligned rows. */
  if ((Pitch & 3u) != 0u ||
      (APPLE_AGX_SCANOUT_U64)Pitch < (APPLE_AGX_SCANOUT_U64)Width * 4ull)
    return AppleAgxBugcheckBlitRejected;
  if (SourceWidth == 0u || SourceHeight == 0u)
    return AppleAgxBugcheckBlitEmpty;
  /* Source: every row must lie inside its stride (64-bit math: no wrap). */
  if ((APPLE_AGX_SCANOUT_U64)SourceStride < (APPLE_AGX_SCANOUT_U64)SourceWidth * 4ull)
    return AppleAgxBugcheckBlitRejected;
  if (PositionX >= Width || PositionY >= Height)
    return AppleAgxBugcheckBlitClipped;
  copy_w = SourceWidth;
  copy_h = SourceHeight;
  if ((APPLE_AGX_SCANOUT_U64)PositionX + copy_w > (APPLE_AGX_SCANOUT_U64)Width) {
    copy_w = (APPLE_AGX_SCANOUT_U64)Width - PositionX;
    result = AppleAgxBugcheckBlitClipped;
  }
  if ((APPLE_AGX_SCANOUT_U64)PositionY + copy_h > (APPLE_AGX_SCANOUT_U64)Height) {
    copy_h = (APPLE_AGX_SCANOUT_U64)Height - PositionY;
    result = AppleAgxBugcheckBlitClipped;
  }
  for (y = 0ull; y < copy_h; ++y) {
    const unsigned char *s = src_base + y * SourceStride;
    volatile APPLE_AGX_SCANOUT_U32 *d =
        (volatile APPLE_AGX_SCANOUT_U32 *)(dst_base +
                                           (PositionY + y) * Pitch +
                                           (APPLE_AGX_SCANOUT_U64)PositionX * 4ull);
    for (x = 0ull; x < copy_w; ++x, s += 4) {
      /* Bytes B, G, R of X8R8G8B8 (no unaligned 32-bit source load). */
      d[x] = (APPLE_AGX_SCANOUT_U32)s[0] | ((APPLE_AGX_SCANOUT_U32)s[1] << 8u) |
             ((APPLE_AGX_SCANOUT_U32)s[2] << 16u) | 0xFF000000u;
    }
  }
  return result;
}

#endif
