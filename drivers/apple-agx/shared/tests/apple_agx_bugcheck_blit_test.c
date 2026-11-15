#include "apple_agx_bugcheck_blit.h"

#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* A2: the bugcheck-time CPU blit into the POST surface. WHAT REAL BUG: an
 * out-of-bounds write into reserved memory at HIGH_LEVEL (a nested bugcheck and
 * a lost dump), pitch padding or rows outside the surface overwritten, a source
 * with stride < width*4 read past its end, the alpha byte left 0. */

#define SENTINEL 0xA5u
#define GUARD 64u /* bytes after the surface: must stay SENTINEL */

typedef struct {
  unsigned char *mem;
  unsigned W, H, Pitch;
  size_t bytes;
} surface_t;

static surface_t surface(unsigned W, unsigned H, unsigned Pitch) {
  surface_t s;
  s.W = W; s.H = H; s.Pitch = Pitch;
  s.bytes = (size_t)Pitch * H;
  s.mem = malloc(s.bytes + GUARD);
  assert(s.mem != NULL);
  memset(s.mem, SENTINEL, s.bytes + GUARD);
  return s;
}

static unsigned px(const surface_t *s, unsigned x, unsigned y) {
  const unsigned char *p = s->mem + (size_t)y * s->Pitch + (size_t)x * 4u;
  return (unsigned)p[0] | ((unsigned)p[1] << 8) | ((unsigned)p[2] << 16) |
         ((unsigned)p[3] << 24);
}

static int untouched(const surface_t *s, unsigned x, unsigned y) {
  return px(s, x, y) == 0xA5A5A5A5u;
}

static void guard_intact(const surface_t *s) {
  size_t i;
  for (i = 0; i < GUARD; ++i) assert(s->mem[s->bytes + i] == SENTINEL);
}

/* Source pixels: B=x G=y R=0x40 A=0x00 (alpha must be forced to 0xFF). */
static unsigned char *source(unsigned W, unsigned H, unsigned Stride) {
  unsigned char *src = malloc((size_t)Stride * H);
  unsigned x, y;
  assert(src != NULL);
  memset(src, 0xEE, (size_t)Stride * H);
  for (y = 0; y < H; ++y)
    for (x = 0; x < W; ++x) {
      unsigned char *p = src + (size_t)y * Stride + (size_t)x * 4u;
      p[0] = (unsigned char)x; p[1] = (unsigned char)y; p[2] = 0x40u; p[3] = 0x00u;
    }
  return src;
}

static unsigned expect(unsigned sx, unsigned sy) {
  return (sx & 0xFFu) | ((sy & 0xFFu) << 8) | (0x40u << 16) | 0xFF000000u;
}

static APPLE_AGX_BUGCHECK_BLIT_RESULT blit(surface_t *s, const unsigned char *src,
                                           unsigned Stride, unsigned W, unsigned H,
                                           unsigned X, unsigned Y) {
  return AppleAgxBugcheckBlitRect(s->mem, s->Pitch, s->W, s->H, src, Stride, W, H, X, Y);
}

static void test_inside_alpha_forced_padding_untouched(void) {
  /* Pitch padded by 3 pixels: padding must never be written. */
  surface_t s = surface(16u, 8u, 16u * 4u + 12u);
  unsigned char *src = source(16u, 8u, 16u * 4u);
  unsigned x, y;
  assert(blit(&s, src, 16u * 4u, 16u, 8u, 0u, 0u) == AppleAgxBugcheckBlitOk);
  for (y = 0; y < 8u; ++y) {
    for (x = 0; x < 16u; ++x) assert(px(&s, x, y) == expect(x, y));
    for (x = 16u * 4u; x < s.Pitch; ++x) assert(s.mem[(size_t)y * s.Pitch + x] == SENTINEL);
  }
  guard_intact(&s);
  free(src); free(s.mem);
}

static void test_offset_and_source_stride(void) {
  /* Source stride padded; rect at (3,2) of size 4x3. */
  surface_t s = surface(10u, 6u, 40u);
  unsigned char *src = source(4u, 3u, 4u * 4u + 8u);
  unsigned x, y;
  assert(blit(&s, src, 4u * 4u + 8u, 4u, 3u, 3u, 2u) == AppleAgxBugcheckBlitOk);
  for (y = 0; y < 6u; ++y)
    for (x = 0; x < 10u; ++x) {
      if (x >= 3u && x < 7u && y >= 2u && y < 5u)
        assert(px(&s, x, y) == expect(x - 3u, y - 2u));
      else
        assert(untouched(&s, x, y));
    }
  guard_intact(&s);
  free(src); free(s.mem);
}

static void test_right_and_bottom_clipping(void) {
  surface_t s = surface(8u, 4u, 32u);
  unsigned char *src = source(6u, 5u, 24u);
  unsigned x, y;
  /* 6x5 at (5,2): only 3x2 fits. */
  assert(blit(&s, src, 24u, 6u, 5u, 5u, 2u) == AppleAgxBugcheckBlitClipped);
  for (y = 0; y < 4u; ++y)
    for (x = 0; x < 8u; ++x) {
      if (x >= 5u && y >= 2u)
        assert(px(&s, x, y) == expect(x - 5u, y - 2u));
      else
        assert(untouched(&s, x, y));
    }
  guard_intact(&s);
  free(src); free(s.mem);
}

static void test_fully_outside_and_zero_size(void) {
  surface_t s = surface(8u, 4u, 32u);
  unsigned char *src = source(4u, 4u, 16u);
  size_t i;
  assert(blit(&s, src, 16u, 4u, 4u, 8u, 0u) == AppleAgxBugcheckBlitClipped);   /* X == W */
  assert(blit(&s, src, 16u, 4u, 4u, 0xFFFFFFFFu, 0u) == AppleAgxBugcheckBlitClipped);
  assert(blit(&s, src, 16u, 4u, 4u, 0u, 4u) == AppleAgxBugcheckBlitClipped);   /* Y == H */
  assert(blit(&s, src, 16u, 4u, 4u, 0u, 0xFFFFFFFFu) == AppleAgxBugcheckBlitClipped);
  assert(blit(&s, src, 16u, 0u, 4u, 0u, 0u) == AppleAgxBugcheckBlitEmpty);
  assert(blit(&s, src, 16u, 4u, 0u, 0u, 0u) == AppleAgxBugcheckBlitEmpty);
  /* X + width wraps 32 bits: must clip, not wrap to column 0. */
  assert(blit(&s, src, 16u, 4u, 4u, 0xFFFFFFFEu, 0u) == AppleAgxBugcheckBlitClipped);
  for (i = 0; i < s.bytes + GUARD; ++i) assert(s.mem[i] == SENTINEL);
  free(src); free(s.mem);
}

static void test_rejected(void) {
  surface_t s = surface(8u, 4u, 32u);
  unsigned char *src = source(4u, 4u, 16u);
  size_t i;
  assert(blit(&s, src, 15u, 4u, 4u, 0u, 0u) == AppleAgxBugcheckBlitRejected);  /* stride < W*4 */
  assert(blit(&s, src, 0u, 4u, 4u, 0u, 0u) == AppleAgxBugcheckBlitRejected);
  /* SrcW*4 overflows 32 bits: stride can never cover the row. */
  assert(blit(&s, src, 0xFFFFFFFFu, 0x40000000u, 1u, 0u, 0u) == AppleAgxBugcheckBlitRejected);
  assert(blit(&s, NULL, 16u, 4u, 4u, 0u, 0u) == AppleAgxBugcheckBlitRejected);
  assert(AppleAgxBugcheckBlitRect(NULL, 32u, 8u, 4u, src, 16u, 4u, 4u, 0u, 0u) ==
         AppleAgxBugcheckBlitRejected);
  /* Destination pitch below W*4 or not a multiple of 4. */
  assert(AppleAgxBugcheckBlitRect(s.mem, 28u, 8u, 4u, src, 16u, 4u, 4u, 0u, 0u) ==
         AppleAgxBugcheckBlitRejected);
  assert(AppleAgxBugcheckBlitRect(s.mem, 34u, 8u, 3u, src, 16u, 4u, 4u, 0u, 0u) ==
         AppleAgxBugcheckBlitRejected);
  /* Destination W*4 overflows 32 bits. */
  assert(AppleAgxBugcheckBlitRect(s.mem, 0xFFFFFFFCu, 0x40000001u, 1u, src, 16u, 4u, 4u, 0u, 0u) ==
         AppleAgxBugcheckBlitRejected);
  for (i = 0; i < s.bytes + GUARD; ++i) assert(s.mem[i] == SENTINEL);
  free(src); free(s.mem);
}

int main(void) {
  test_inside_alpha_forced_padding_untouched();
  test_offset_and_source_stride();
  test_right_and_bottom_clipping();
  test_fully_outside_and_zero_size();
  test_rejected();
  puts("apple_agx_bugcheck_blit_test: OK");
  return 0;
}
