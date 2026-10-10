/* EXP1170: copying the shown image out of its write-combined CPU mapping
 * costs ~19 ms per 2560x1600 present on one thread (uncached loads); the
 * present splits it into horizontal slices copied concurrently. Each slice
 * owns rows [first, end) and every row belongs to exactly one slice for any
 * height and slice count. Plain C so the host suite can test it. */
#ifndef AGX_WGL_PRESENT_COPY_H
#define AGX_WGL_PRESENT_COPY_H

#include <stddef.h>
#include <string.h>

#define AGX_WGL_COPY_SLICES 4u

static inline void agx_wgl_slice_rows(unsigned height, unsigned slices,
                                      unsigned index, unsigned *first,
                                      unsigned *end) {
  *first = (unsigned)((unsigned long long)height * index / slices);
  *end = (unsigned)((unsigned long long)height * (index + 1u) / slices);
}

static inline void agx_wgl_copy_rows(unsigned char *dst, size_t dst_stride,
                                     const unsigned char *src,
                                     size_t src_stride, size_t row_bytes,
                                     unsigned first, unsigned end) {
  for (unsigned y = first; y < end; ++y)
    memcpy(dst + (size_t)y * dst_stride, src + (size_t)y * src_stride,
           row_bytes);
}

#endif
