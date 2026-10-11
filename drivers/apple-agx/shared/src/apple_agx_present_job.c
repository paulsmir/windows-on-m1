#include "apple_agx_present_job.h"

/* Job area: the CDM stream first, then one 256-byte block per sub-rect with
 * the argument block at +0 and the USC words at +128 (64-byte aligned). */
#define STREAM_OFFSET APPLE_AGX_PRESENT_JOB_OFFSET
#define STREAM_BYTES 0x1000u
#define BLOCKS_OFFSET (STREAM_OFFSET + STREAM_BYTES)
#define BLOCK_BYTES 256u
#define USC_IN_BLOCK 128u
#define MAX_EXTENT 16384u

typedef char present_layout_fits[
    (BLOCKS_OFFSET + APPLE_AGX_PRESENT_MAX_RECTS * BLOCK_BYTES <=
     APPLE_AGX_PRESENT_PREEMPT_OFFSET &&
     APPLE_AGX_PRESENT_MAX_RECTS *
         (APPLE_AGX_CDM_LAUNCH_WORDS + APPLE_AGX_CDM_BARRIER_WORDS) * 4u +
         APPLE_AGX_CDM_TERMINATE_WORDS * 4u <= STREAM_BYTES &&
     APPLE_AGX_PRESENT_ARG_HALFS * 2u <= USC_IN_BLOCK) ? 1 : -1];

static void put32(unsigned char *out, unsigned int value) {
  out[0] = (unsigned char)value; out[1] = (unsigned char)(value >> 8);
  out[2] = (unsigned char)(value >> 16); out[3] = (unsigned char)(value >> 24);
}

static void copy_bytes(unsigned char *out, const unsigned char *in,
                       unsigned int bytes) {
  unsigned int i;
  for (i = 0u; i < bytes; ++i) out[i] = in[i];
}

static int arena_valid(const APPLE_AGX_PRESENT_ARENA *a) {
  return a && a->Cpu && a->Va && !(a->Va & 0xffffULL) &&
         a->Bytes >= APPLE_AGX_PRESENT_ARENA_BYTES && a->Va >= a->UscBase &&
         a->Va - a->UscBase <= 0xffffffffULL - APPLE_AGX_PRESENT_ARENA_BYTES;
}

static int surface_valid(const APPLE_AGX_PRESENT_SURFACE *s) {
  return s && s->Va && !(s->Va & 3ULL) && s->Width && s->Height &&
         s->Width <= MAX_EXTENT && s->Height <= MAX_EXTENT && !(s->Pitch & 3u) &&
         s->Pitch >= s->Width * 4u &&
         (unsigned long long)(s->Height - 1u) * s->Pitch + s->Width * 4ULL <=
             0xffffffffULL;
}

static int rect_inside(const APPLE_AGX_PRESENT_RECT *r, int left, int top,
                       int right, int bottom) {
  return r && r->Left < r->Right && r->Top < r->Bottom && r->Left >= left &&
         r->Top >= top && r->Right <= right && r->Bottom <= bottom;
}

int AppleAgxPresentArenaInstall(const APPLE_AGX_PRESENT_ARENA *Arena) {
  if (!arena_valid(Arena) ||
      AppleAgxPresentCopy.BinaryBytes >
          APPLE_AGX_PRESENT_FILL_OFFSET - APPLE_AGX_PRESENT_COPY_OFFSET ||
      AppleAgxPresentFill.BinaryBytes >
          APPLE_AGX_PRESENT_JOB_OFFSET - APPLE_AGX_PRESENT_FILL_OFFSET)
    return 0;
  copy_bytes(Arena->Cpu + APPLE_AGX_PRESENT_COPY_OFFSET,
             AppleAgxPresentCopy.Binary, AppleAgxPresentCopy.BinaryBytes);
  copy_bytes(Arena->Cpu + APPLE_AGX_PRESENT_FILL_OFFSET,
             AppleAgxPresentFill.Binary, AppleAgxPresentFill.BinaryBytes);
  return 1;
}

/* USC words of one launch: the argument block at u0, the kernel's constants
 * (64 halfs per word, agx_usc_immediates), then the program block. */
static unsigned int usc_words(const APPLE_AGX_PRESENT_ARENA *a,
                              const APPLE_AGX_PRESENT_KERNEL *k,
                              unsigned long long kernel_va,
                              unsigned long long args_va, unsigned char *out) {
  APPLE_AGX_USC_PROGRAM program;
  unsigned int used, n, range;
  used = AppleAgxUscUniform(out, USC_IN_BLOCK, 0u, APPLE_AGX_PRESENT_ARG_HALFS,
                            args_va);
  if (!used) return 0u;
  for (range = 0u; range * 64u < k->RodataHalfs; ++range) {
    unsigned int halfs = k->RodataHalfs - range * 64u;
    n = AppleAgxUscUniform(out + used, USC_IN_BLOCK - used,
                           k->RodataBaseUniform + range * 64u,
                           halfs > 64u ? 64u : halfs,
                           kernel_va + k->RodataOffset + range * 128u);
    if (!n) return 0u;
    used += n;
  }
  program.CodeOffset = (unsigned int)(kernel_va + k->MainOffset - a->UscBase);
  program.PreambleOffset = k->PreambleGprs ?
      (unsigned int)(kernel_va + k->PreambleOffset - a->UscBase) : 0u;
  program.Gprs = k->Gprs;
  program.SharedBytes = 0u;
  n = AppleAgxUscProgram(out + used, USC_IN_BLOCK - used, &program);
  return n ? used + n : 0u;
}

/* Every refusal of AppleAgxPresentBuild; what it writes after this check
 * cannot fail (fixed sizes, offsets inside a valid arena). */
static int present_check(const APPLE_AGX_PRESENT_ARENA *Arena, int Fill,
                         const APPLE_AGX_PRESENT_SURFACE *Source,
                         const APPLE_AGX_PRESENT_RECT *SourceRect,
                         const APPLE_AGX_PRESENT_SURFACE *Destination,
                         const APPLE_AGX_PRESENT_RECT *DestinationRect,
                         const APPLE_AGX_PRESENT_RECT *SubRects,
                         unsigned int SubRectCount,
                         unsigned int *StepX, unsigned int *StepY) {
  unsigned int i;
  *StepX = *StepY = 0u;
  if (!arena_valid(Arena) || !surface_valid(Destination) ||
      !SubRects || !SubRectCount || SubRectCount > APPLE_AGX_PRESENT_MAX_RECTS ||
      !rect_inside(DestinationRect, -(int)MAX_EXTENT, -(int)MAX_EXTENT,
                   2 * (int)MAX_EXTENT, 2 * (int)MAX_EXTENT))
    return 0;
  if (!Fill) {
    unsigned int sw, sh, dw, dh;
    if (!surface_valid(Source) ||
        !rect_inside(SourceRect, 0, 0, (int)Source->Width, (int)Source->Height))
      return 0;
    sw = (unsigned int)(SourceRect->Right - SourceRect->Left);
    sh = (unsigned int)(SourceRect->Bottom - SourceRect->Top);
    dw = (unsigned int)(DestinationRect->Right - DestinationRect->Left);
    dh = (unsigned int)(DestinationRect->Bottom - DestinationRect->Top);
    *StepX = (sw << 16) / dw;
    *StepY = (sh << 16) / dh;
    if (!*StepX || !*StepY) return 0;
  }
  for (i = 0u; i < SubRectCount; ++i)
    if (!rect_inside(&SubRects[i], DestinationRect->Left, DestinationRect->Top,
                     DestinationRect->Right, DestinationRect->Bottom) ||
        !rect_inside(&SubRects[i], 0, 0, (int)Destination->Width,
                     (int)Destination->Height))
      return 0;
  return 1;
}

int AppleAgxPresentValidate(const APPLE_AGX_PRESENT_ARENA *Arena, int Fill,
                            const APPLE_AGX_PRESENT_SURFACE *Source,
                            const APPLE_AGX_PRESENT_RECT *SourceRect,
                            const APPLE_AGX_PRESENT_SURFACE *Destination,
                            const APPLE_AGX_PRESENT_RECT *DestinationRect,
                            const APPLE_AGX_PRESENT_RECT *SubRects,
                            unsigned int SubRectCount) {
  unsigned int step_x, step_y;
  return present_check(Arena, Fill, Source, SourceRect, Destination,
                       DestinationRect, SubRects, SubRectCount, &step_x, &step_y);
}

int AppleAgxPresentBuild(const APPLE_AGX_PRESENT_ARENA *Arena, int Fill,
                         unsigned int Colour,
                         const APPLE_AGX_PRESENT_SURFACE *Source,
                         const APPLE_AGX_PRESENT_RECT *SourceRect,
                         const APPLE_AGX_PRESENT_SURFACE *Destination,
                         const APPLE_AGX_PRESENT_RECT *DestinationRect,
                         const APPLE_AGX_PRESENT_RECT *SubRects,
                         unsigned int SubRectCount, APPLE_AGX_PRESENT_JOB *Job) {
  const APPLE_AGX_PRESENT_KERNEL *k = Fill ? &AppleAgxPresentFill : &AppleAgxPresentCopy;
  const unsigned long long kernel_va = Arena ? Arena->Va +
      (Fill ? APPLE_AGX_PRESENT_FILL_OFFSET : APPLE_AGX_PRESENT_COPY_OFFSET) : 0ULL;
  unsigned int stream[APPLE_AGX_PRESENT_MAX_RECTS *
                      (APPLE_AGX_CDM_LAUNCH_WORDS + APPLE_AGX_CDM_BARRIER_WORDS) +
                      APPLE_AGX_CDM_TERMINATE_WORDS];
  unsigned int words = 0u, i, n, step_x = 0u, step_y = 0u;
  if (!Job || !present_check(Arena, Fill, Source, SourceRect, Destination,
                             DestinationRect, SubRects, SubRectCount,
                             &step_x, &step_y))
    return 0;
  for (i = 0u; i < SubRectCount; ++i) {
    const APPLE_AGX_PRESENT_RECT *r = &SubRects[i];
    const unsigned int block = BLOCKS_OFFSET + i * BLOCK_BYTES;
    unsigned char *args = Arena->Cpu + block;
    const unsigned long long args_va = Arena->Va + block;
    const unsigned int width = (unsigned int)(r->Right - r->Left);
    const unsigned int height = (unsigned int)(r->Bottom - r->Top);
    APPLE_AGX_CDM_DISPATCH dispatch;
    unsigned int j;
    for (j = 0u; j < BLOCK_BYTES; ++j) args[j] = 0u;
    put32(args + 2u * APPLE_AGX_PRESENT_ARG_SRC, Fill ? 0u : (unsigned int)Source->Va);
    put32(args + 2u * APPLE_AGX_PRESENT_ARG_SRC + 4u,
          Fill ? 0u : (unsigned int)(Source->Va >> 32));
    put32(args + 2u * APPLE_AGX_PRESENT_ARG_DST, (unsigned int)Destination->Va);
    put32(args + 2u * APPLE_AGX_PRESENT_ARG_DST + 4u,
          (unsigned int)(Destination->Va >> 32));
    put32(args + 2u * APPLE_AGX_PRESENT_ARG_SRC_PITCH, Fill ? 0u : Source->Pitch);
    put32(args + 2u * APPLE_AGX_PRESENT_ARG_DST_PITCH, Destination->Pitch);
    put32(args + 2u * APPLE_AGX_PRESENT_ARG_ORIGIN_X, (unsigned int)r->Left);
    put32(args + 2u * APPLE_AGX_PRESENT_ARG_ORIGIN_Y, (unsigned int)r->Top);
    put32(args + 2u * APPLE_AGX_PRESENT_ARG_WIDTH, width);
    put32(args + 2u * APPLE_AGX_PRESENT_ARG_HEIGHT, height);
    put32(args + 2u * APPLE_AGX_PRESENT_ARG_MAP_X, (unsigned int)DestinationRect->Left);
    put32(args + 2u * APPLE_AGX_PRESENT_ARG_MAP_Y, (unsigned int)DestinationRect->Top);
    put32(args + 2u * APPLE_AGX_PRESENT_ARG_SRC_X, Fill ? 0u : (unsigned int)SourceRect->Left);
    put32(args + 2u * APPLE_AGX_PRESENT_ARG_SRC_Y, Fill ? 0u : (unsigned int)SourceRect->Top);
    put32(args + 2u * APPLE_AGX_PRESENT_ARG_STEP_X, step_x);
    put32(args + 2u * APPLE_AGX_PRESENT_ARG_STEP_Y, step_y);
    put32(args + 2u * APPLE_AGX_PRESENT_ARG_SRC_MAX_X,
          Fill ? 0u : (unsigned int)(SourceRect->Right - 1));
    put32(args + 2u * APPLE_AGX_PRESENT_ARG_SRC_MAX_Y,
          Fill ? 0u : (unsigned int)(SourceRect->Bottom - 1));
    put32(args + 2u * APPLE_AGX_PRESENT_ARG_FILL, Colour);
    if (!usc_words(Arena, k, kernel_va, args_va, args + USC_IN_BLOCK))
      return 0;
    dispatch.UniformHalfs = k->PushCount;
    dispatch.PreshaderGprs = k->PreambleGprs;
    dispatch.TextureStates = 8u;   /* as Mesa's precompiled kernels */
    dispatch.SamplerStates = 1u;
    dispatch.PipelineOffset =
        (unsigned int)(args_va + USC_IN_BLOCK - Arena->UscBase);
    dispatch.Global[0] = (width + APPLE_AGX_PRESENT_LOCAL_X - 1u) /
        APPLE_AGX_PRESENT_LOCAL_X * APPLE_AGX_PRESENT_LOCAL_X;
    dispatch.Global[1] = (height + APPLE_AGX_PRESENT_LOCAL_Y - 1u) /
        APPLE_AGX_PRESENT_LOCAL_Y * APPLE_AGX_PRESENT_LOCAL_Y;
    dispatch.Global[2] = 1u;
    dispatch.Local[0] = APPLE_AGX_PRESENT_LOCAL_X;
    dispatch.Local[1] = APPLE_AGX_PRESENT_LOCAL_Y;
    dispatch.Local[2] = 1u;
    n = AppleAgxCdmLaunch(stream + words, (unsigned int)(sizeof(stream) / 4u) - words,
                          &dispatch);
    if (!n) return 0;
    words += n;
    n = AppleAgxCdmBarrier(stream + words, (unsigned int)(sizeof(stream) / 4u) - words);
    if (!n) return 0;
    words += n;
  }
  /* The end Mesa reports excludes the terminator (agx_flush_compute). */
  Job->CdmStreamEnd = Arena->Va + STREAM_OFFSET + words * 4u;
  n = AppleAgxCdmTerminate(stream + words, (unsigned int)(sizeof(stream) / 4u) - words);
  if (!n) return 0;
  words += n;
  for (i = 0u; i < words; ++i)
    put32(Arena->Cpu + STREAM_OFFSET + 4u * i, stream[i]);
  Job->CdmStreamBase = Arena->Va + STREAM_OFFSET;
  Job->PreemptionVa = Arena->Va + APPLE_AGX_PRESENT_PREEMPT_OFFSET;
  Job->PreemptionBytes = APPLE_AGX_PRESENT_PREEMPT_BYTES;
  Job->Launches = SubRectCount;
  return 1;
}
