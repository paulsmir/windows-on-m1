#ifndef APPLE_AGX_RENDER_DYNAMIC_OUTPUT_H
#define APPLE_AGX_RENDER_DYNAMIC_OUTPUT_H

#include "apple_agx_state.h"

typedef int (*ADMISSION_DYNAMIC_OUTPUT_PROGRESS)(void *Context);

typedef struct _ADMISSION_DYNAMIC_OUTPUT_EXPECTATION {
  APPLE_AGX_U32 Width;
  APPLE_AGX_U32 Height;
  APPLE_AGX_U32 Pitch;
  APPLE_AGX_U32 BackgroundColor;
  APPLE_AGX_U32 InteriorX;
  APPLE_AGX_U32 InteriorY;
  APPLE_AGX_U32 MinX;
  APPLE_AGX_U32 MinY;
  APPLE_AGX_U32 MaxX;
  APPLE_AGX_U32 MaxY;
  APPLE_AGX_U32 MinimumForegroundPixels;
  APPLE_AGX_U32 MaximumForegroundPixels;
  APPLE_AGX_U32 PoisonByte;
} ADMISSION_DYNAMIC_OUTPUT_EXPECTATION;

typedef struct _ADMISSION_DYNAMIC_OUTPUT_RESULT {
  APPLE_AGX_U32 Valid;
  APPLE_AGX_U32 ForegroundColor;
  APPLE_AGX_U32 BackgroundPixels;
  APPLE_AGX_U32 ForegroundPixels;
  APPLE_AGX_U32 PoisonPixels;
  APPLE_AGX_U32 FirstInvalidPixel;
  APPLE_AGX_U32 FirstInvalidValue;
  APPLE_AGX_U32 BytesExamined;
  APPLE_AGX_U64 Fnv1a;
} ADMISSION_DYNAMIC_OUTPUT_RESULT;

int AdmissionDynamicOutputDescribeExpectation(
    APPLE_AGX_U32 Width, APPLE_AGX_U32 Height, APPLE_AGX_U32 Pitch,
    APPLE_AGX_U32 BackgroundColor,
    ADMISSION_DYNAMIC_OUTPUT_EXPECTATION *Expectation);

int AdmissionDynamicOutputVerify(
    const unsigned char *Bytes, APPLE_AGX_U32 ByteCount,
    const ADMISSION_DYNAMIC_OUTPUT_EXPECTATION *Expectation,
    APPLE_AGX_U32 ChunkBytes,
    ADMISSION_DYNAMIC_OUTPUT_PROGRESS Progress, void *ProgressContext,
    ADMISSION_DYNAMIC_OUTPUT_RESULT *Result);

#endif
