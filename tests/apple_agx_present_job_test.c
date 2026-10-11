/* G2 HW2: the KMD present job, executed by a CPU model of the CDM/USC path.
 * The model walks the CDM stream the job wrote, follows each launch's
 * pipeline offset to its USC words, reads the argument block that the first
 * Uniform word maps at u0, and runs the kernel semantics documented in
 * agx_present_kernels.c over the launched grid. The result must equal a
 * reference blt computed directly from the rects. */
#include "apple_agx_present_job.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define USC_BASE 0x1100000000ULL
#define ARENA_VA 0x11fe000000ULL
#define SRC_VA 0x0020000000ULL
#define DST_VA 0x0030000000ULL

static unsigned char arena_mem[APPLE_AGX_PRESENT_ARENA_BYTES];
static unsigned int *src_mem, *dst_mem, *ref_mem;
static APPLE_AGX_PRESENT_SURFACE src = {SRC_VA, 100 * 4 + 16, 100, 60};
static APPLE_AGX_PRESENT_SURFACE dst = {DST_VA, 256 * 4, 256, 128};

static unsigned int get32(const unsigned char *p) {
  return p[0] | (p[1] << 8) | (p[2] << 16) | ((unsigned int)p[3] << 24);
}
static unsigned char *cpu_of(unsigned long long va) {
  assert(va >= ARENA_VA && va < ARENA_VA + sizeof(arena_mem));
  return arena_mem + (va - ARENA_VA);
}
static unsigned int *pixel(unsigned long long base, unsigned int pitch,
                           unsigned int x, unsigned int y) {
  unsigned long long va = base + (unsigned long long)y * pitch + x * 4ULL;
  if (va >= SRC_VA && va < SRC_VA + (unsigned long long)src.Pitch * src.Height)
    return src_mem + (va - SRC_VA) / 4u;
  assert(va >= DST_VA && va < DST_VA + (unsigned long long)dst.Pitch * dst.Height);
  return dst_mem + (va - DST_VA) / 4u;
}

/* Kernel semantics (agx_present_kernels.c). */
static void run_kernel(int fill, const unsigned char *a, unsigned int gx,
                       unsigned int gy) {
#define ARG(n) get32(a + 2u * APPLE_AGX_PRESENT_ARG_##n)
  unsigned long long s = ARG(SRC) | ((unsigned long long)get32(a + 2u * APPLE_AGX_PRESENT_ARG_SRC + 4u) << 32);
  unsigned long long d = ARG(DST) | ((unsigned long long)get32(a + 2u * APPLE_AGX_PRESENT_ARG_DST + 4u) << 32);
  for (unsigned int y = 0; y < gy; ++y)
    for (unsigned int x = 0; x < gx; ++x) {
      if (x >= ARG(WIDTH) || y >= ARG(HEIGHT)) continue;
      unsigned int dx = ARG(ORIGIN_X) + x, dy = ARG(ORIGIN_Y) + y, v;
      if (fill) {
        v = ARG(FILL);
      } else {
        unsigned int sx = ARG(SRC_X) + (((dx - ARG(MAP_X)) * ARG(STEP_X) + (ARG(STEP_X) >> 1)) >> 16);
        unsigned int sy = ARG(SRC_Y) + (((dy - ARG(MAP_Y)) * ARG(STEP_Y) + (ARG(STEP_Y) >> 1)) >> 16);
        if (sx > ARG(SRC_MAX_X)) sx = ARG(SRC_MAX_X);
        if (sy > ARG(SRC_MAX_Y)) sy = ARG(SRC_MAX_Y);
        v = *pixel(s, ARG(SRC_PITCH), sx, sy);
      }
      *pixel(d, ARG(DST_PITCH), dx, dy) = v;
    }
#undef ARG
}

/* Walk the stream; returns launches executed. */
static unsigned int execute(const APPLE_AGX_PRESENT_JOB *job, int fill) {
  const APPLE_AGX_PRESENT_KERNEL *k = fill ? &AppleAgxPresentFill : &AppleAgxPresentCopy;
  unsigned long long kernel_va = ARENA_VA + (fill ? APPLE_AGX_PRESENT_FILL_OFFSET : APPLE_AGX_PRESENT_COPY_OFFSET);
  const unsigned char *w = cpu_of(job->CdmStreamBase);
  unsigned int launches = 0, barriers = 0;
  for (;;) {
    unsigned int word = get32(w), type = word >> 29;
    if (type == 2u) { assert(get32(w + 4) == 0u); break; }
    if (type == 3u) { assert(word == 0x600fffffu); ++barriers; w += 4; continue; }
    assert(type == 0u && barriers == launches);
    unsigned int pipeline = get32(w + 4);
    unsigned int global[3] = {get32(w + 8), get32(w + 12), get32(w + 16)};
    unsigned int local[3] = {get32(w + 20), get32(w + 24), get32(w + 28)};
    assert(local[0] == APPLE_AGX_PRESENT_LOCAL_X && local[1] == APPLE_AGX_PRESENT_LOCAL_Y && local[2] == 1);
    assert(global[0] % local[0] == 0 && global[1] % local[1] == 0 && global[2] == 1);
    assert(!(pipeline & 63u));
    const unsigned char *u = cpu_of(USC_BASE + pipeline), *args = NULL;
    int saw_rodata = 0, saw_shader = 0, saw_regs = 0, saw_pre = 0;
    for (int done = 0; !done;) {
      unsigned long long q;
      switch (u[0]) {
      case 0x1d:
        memcpy(&q, u, 8);
        if (((q >> 8) & 0xff) == 0) {
          assert(((q >> 20) & 63) == APPLE_AGX_PRESENT_ARG_HALFS);
          args = cpu_of((q >> 26) << 2);
        } else {
          assert(((q >> 8) & 0xff) == k->RodataBaseUniform &&
                 ((q >> 26) << 2) == kernel_va + k->RodataOffset);
          saw_rodata = 1;
        }
        u += 8; break;
      case 0x4d: u += 4; break;
      case 0x0d:
        assert(get32(u + 2) == kernel_va + k->MainOffset - USC_BASE);
        saw_shader = 1; u += 6; break;
      case 0x8d: saw_regs = 1; u += 4; break;
      case 0x38:
        assert(get32(u + 4) == kernel_va + k->PreambleOffset - USC_BASE);
        saw_pre = 1; u += 8; done = 1; break;
      case 0x88: u += 2; done = 1; break;
      default: assert(0 && "unknown USC word");
      }
    }
    assert(args && saw_shader && saw_regs && saw_rodata == (k->RodataHalfs != 0));
    assert(saw_pre == (k->PreambleGprs != 0));
    run_kernel(fill, args, global[0], global[1]);
    ++launches; w += 32;
  }
  assert(barriers == launches);
  return launches;
}

static void reference(int fill, unsigned int colour, const APPLE_AGX_PRESENT_RECT *sr,
                      const APPLE_AGX_PRESENT_RECT *dr, const APPLE_AGX_PRESENT_RECT *rects,
                      unsigned int count) {
  unsigned int sw = sr->Right - sr->Left, sh = sr->Bottom - sr->Top;
  unsigned int dw = dr->Right - dr->Left, dh = dr->Bottom - dr->Top;
  unsigned int stepx = (sw << 16) / dw, stepy = (sh << 16) / dh;
  for (unsigned int i = 0; i < count; ++i)
    for (int y = rects[i].Top; y < rects[i].Bottom; ++y)
      for (int x = rects[i].Left; x < rects[i].Right; ++x) {
        unsigned int v = colour;
        if (!fill) {
          unsigned int sx = sr->Left + (((x - dr->Left) * stepx + stepx / 2) >> 16);
          unsigned int sy = sr->Top + (((y - dr->Top) * stepy + stepy / 2) >> 16);
          v = src_mem[(sy * src.Pitch) / 4 + sx];
        }
        ref_mem[(y * dst.Pitch) / 4 + x] = v;
      }
}

static void reset_dst(void) {
  for (unsigned int i = 0; i < dst.Pitch * dst.Height / 4u; ++i)
    dst_mem[i] = ref_mem[i] = 0xdead0000u ^ i;
}

int main(void) {
  APPLE_AGX_PRESENT_ARENA arena = {arena_mem, ARENA_VA, sizeof(arena_mem), USC_BASE};
  APPLE_AGX_PRESENT_JOB job;
  src_mem = malloc(src.Pitch * src.Height); dst_mem = malloc(dst.Pitch * dst.Height);
  ref_mem = malloc(dst.Pitch * dst.Height);
  for (unsigned int i = 0; i < src.Pitch * src.Height / 4u; ++i) src_mem[i] = 0x01000193u * i;
  assert(AppleAgxPresentArenaInstall(&arena));
  assert(!memcmp(arena_mem, AppleAgxPresentCopy.Binary, AppleAgxPresentCopy.BinaryBytes));
  assert(!memcmp(arena_mem + APPLE_AGX_PRESENT_FILL_OFFSET, AppleAgxPresentFill.Binary,
                 AppleAgxPresentFill.BinaryBytes));
  /* 1:1 copy over two sub-rects, one of them not a multiple of the group. */
  {
    APPLE_AGX_PRESENT_RECT sr = {10, 5, 90, 55}, dr = {100, 40, 180, 90};
    APPLE_AGX_PRESENT_RECT rects[2] = {{100, 40, 180, 60}, {133, 60, 171, 89}};
    reset_dst();
    assert(AppleAgxPresentBuild(&arena, 0, 0, &src, &sr, &dst, &dr, rects, 2, &job));
    assert(job.Launches == 2 && execute(&job, 0) == 2);
    assert(job.PreemptionVa == ARENA_VA + APPLE_AGX_PRESENT_PREEMPT_OFFSET &&
           job.PreemptionBytes == APPLE_AGX_PRESENT_PREEMPT_BYTES &&
           job.CdmStreamBase < job.CdmStreamEnd);
    reference(0, 0, &sr, &dr, rects, 2);
    assert(!memcmp(dst_mem, ref_mem, dst.Pitch * dst.Height));
    /* identity map: a 1:1 copy is exact */
    assert(dst_mem[(41 * dst.Pitch) / 4 + 101] == src_mem[(6 * src.Pitch) / 4 + 11]);
  }
  /* stretch: 40x25 source to 120x100 destination, and a shrink */
  {
    APPLE_AGX_PRESENT_RECT sr = {3, 7, 43, 32}, dr = {0, 0, 120, 100};
    APPLE_AGX_PRESENT_RECT rect = {0, 0, 120, 100};
    reset_dst();
    assert(AppleAgxPresentBuild(&arena, 0, 0, &src, &sr, &dst, &dr, &rect, 1, &job));
    assert(execute(&job, 0) == 1);
    reference(0, 0, &sr, &dr, &rect, 1);
    assert(!memcmp(dst_mem, ref_mem, dst.Pitch * dst.Height));
    APPLE_AGX_PRESENT_RECT big = {0, 0, 100, 60}, small = {200, 100, 230, 120};
    reset_dst();
    assert(AppleAgxPresentBuild(&arena, 0, 0, &src, &big, &dst, &small, &small, 1, &job));
    assert(execute(&job, 0) == 1);
    reference(0, 0, &big, &small, &small, 1);
    assert(!memcmp(dst_mem, ref_mem, dst.Pitch * dst.Height));
  }
  /* colour fill */
  {
    APPLE_AGX_PRESENT_RECT dr = {0, 0, 256, 128}, rect = {5, 6, 77, 99};
    reset_dst();
    assert(AppleAgxPresentBuild(&arena, 1, 0xff336699u, NULL, NULL, &dst, &dr, &rect, 1, &job));
    assert(execute(&job, 1) == 1);
    reference(1, 0xff336699u, &dr, &dr, &rect, 1);
    assert(!memcmp(dst_mem, ref_mem, dst.Pitch * dst.Height));
  }
  /* refusals: nothing outside a surface or rect, no empty rect, bounded count */
  {
    APPLE_AGX_PRESENT_RECT sr = {0, 0, 100, 60}, dr = {0, 0, 256, 128};
    APPLE_AGX_PRESENT_RECT out = {250, 0, 257, 10}, empty = {5, 5, 5, 9}, outside_dr = {0, 0, 10, 10};
    APPLE_AGX_PRESENT_RECT many[APPLE_AGX_PRESENT_MAX_RECTS + 1];
    APPLE_AGX_PRESENT_RECT small_dr = {20, 20, 40, 40}, bad_sr = {0, 0, 101, 60};
    APPLE_AGX_PRESENT_ARENA unaligned = arena;
    unsigned char before[64];
    memcpy(before, arena_mem + APPLE_AGX_PRESENT_JOB_OFFSET, sizeof(before));
    assert(!AppleAgxPresentBuild(&arena, 0, 0, &src, &sr, &dst, &dr, &out, 1, &job));
    assert(!AppleAgxPresentBuild(&arena, 0, 0, &src, &sr, &dst, &dr, &empty, 1, &job));
    assert(!AppleAgxPresentBuild(&arena, 0, 0, &src, &sr, &dst, &small_dr, &outside_dr, 1, &job));
    assert(!AppleAgxPresentBuild(&arena, 0, 0, &src, &bad_sr, &dst, &dr, &outside_dr, 1, &job));
    for (unsigned int i = 0; i <= APPLE_AGX_PRESENT_MAX_RECTS; ++i) many[i] = outside_dr;
    assert(!AppleAgxPresentBuild(&arena, 0, 0, &src, &sr, &dst, &dr, many,
                                 APPLE_AGX_PRESENT_MAX_RECTS + 1, &job));
    assert(AppleAgxPresentBuild(&arena, 0, 0, &src, &sr, &dst, &dr, many,
                                APPLE_AGX_PRESENT_MAX_RECTS, &job));
    unaligned.Va += 0x1000;
    assert(!AppleAgxPresentBuild(&unaligned, 0, 0, &src, &sr, &dst, &dr, &outside_dr, 1, &job));
    APPLE_AGX_PRESENT_SURFACE bad = dst; bad.Pitch = 255 * 4;
    assert(!AppleAgxPresentBuild(&arena, 0, 0, &src, &sr, &bad, &dr, &outside_dr, 1, &job));
    (void)before;
  }
  puts("present job: PASS");
  free(src_mem); free(dst_mem); free(ref_mem);
  return 0;
}
