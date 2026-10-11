#ifndef APPLE_AGX_PRESENT_JOB_H
#define APPLE_AGX_PRESENT_JOB_H

/* G2 HW2: a present blt (DxgkDdiPresent Blt / ColorFill) as one AGX compute
 * job that the KMD writes into its own arena in the presenting process's
 * VM. The arena lies inside the USC window (the kernels execute from it) and
 * is CPU-visible to the KMD:
 *   [0x00000) copy kernel binary        [0x01000) fill kernel binary
 *   [0x02000) job area: CDM stream, then per sub-rect argument block and
 *             USC words              [0x10000) CDM context-store buffer
 * Surfaces are linear, 32 bits per pixel. One launch per destination
 * sub-rect (DXGKARG_PRESENT pDstSubRects), each followed by the CDM barrier;
 * the copy maps destination pixels to the nearest source pixel of SrcRect
 * (stretch blt), the fill writes one colour. */

#include "apple_agx_cdm.h"
#include "apple_agx_present_kernels.generated.h"

#define APPLE_AGX_PRESENT_COPY_OFFSET 0x00000u
#define APPLE_AGX_PRESENT_FILL_OFFSET 0x01000u
#define APPLE_AGX_PRESENT_JOB_OFFSET 0x02000u
#define APPLE_AGX_PRESENT_PREEMPT_OFFSET 0x10000u
#define APPLE_AGX_PRESENT_PREEMPT_BYTES 0x10000u
#define APPLE_AGX_PRESENT_ARENA_BYTES 0x20000u
#define APPLE_AGX_PRESENT_MAX_RECTS 64u

typedef struct {
  unsigned char *Cpu;            /* KMD mapping of the arena */
  unsigned long long Va;         /* the same bytes in the job's VM */
  unsigned int Bytes;
  unsigned long long UscBase;    /* USC execution base of that VM */
} APPLE_AGX_PRESENT_ARENA;

typedef struct {
  unsigned long long Va;
  unsigned int Pitch, Width, Height;  /* bytes, pixels */
} APPLE_AGX_PRESENT_SURFACE;

typedef struct { int Left, Top, Right, Bottom; } APPLE_AGX_PRESENT_RECT;

typedef struct {
  unsigned long long CdmStreamBase, CdmStreamEnd;
  unsigned long long PreemptionVa;
  unsigned int PreemptionBytes;
  unsigned int Launches;
} APPLE_AGX_PRESENT_JOB;

/* Exactly the refusals of AppleAgxPresentBuild, without writing: a job
 * validated at submission builds in the worker without a failure path. */
int AppleAgxPresentValidate(const APPLE_AGX_PRESENT_ARENA *Arena, int Fill,
                            const APPLE_AGX_PRESENT_SURFACE *Source,
                            const APPLE_AGX_PRESENT_RECT *SourceRect,
                            const APPLE_AGX_PRESENT_SURFACE *Destination,
                            const APPLE_AGX_PRESENT_RECT *DestinationRect,
                            const APPLE_AGX_PRESENT_RECT *SubRects,
                            unsigned int SubRectCount);

/* Copy both kernel binaries to their arena offsets (once per arena). */
int AppleAgxPresentArenaInstall(const APPLE_AGX_PRESENT_ARENA *Arena);

/* Build the job. Fill == 0: copy Source/SourceRect into DestinationRect;
 * Fill != 0: write Colour (Source and SourceRect are ignored). Every
 * sub-rect must be non-empty and lie inside DestinationRect and the
 * destination surface; SourceRect must lie inside the source surface.
 * Returns 0 (and writes nothing the GPU would run) on any violation. */
int AppleAgxPresentBuild(const APPLE_AGX_PRESENT_ARENA *Arena, int Fill,
                         unsigned int Colour,
                         const APPLE_AGX_PRESENT_SURFACE *Source,
                         const APPLE_AGX_PRESENT_RECT *SourceRect,
                         const APPLE_AGX_PRESENT_SURFACE *Destination,
                         const APPLE_AGX_PRESENT_RECT *DestinationRect,
                         const APPLE_AGX_PRESENT_RECT *SubRects,
                         unsigned int SubRectCount, APPLE_AGX_PRESENT_JOB *Job);

#endif
