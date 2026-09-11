#ifndef APPLE_AGX_EXP208_DYNAMIC_H
#define APPLE_AGX_EXP208_DYNAMIC_H

#include "apple_agx_state.h"

/* Exact packed arena size generated from the accepted EXP208 template. */
#define APPLE_AGX_EXP208_IMAGE_BYTES 0x588000u
#define APPLE_AGX_EXP208_EVENT_COUNT 128u

typedef struct _APPLE_AGX_EXP208_DYNAMIC_INPUT {
  /* One-based render sequence.  Sequence 1 is the captured first job. */
  APPLE_AGX_U32 Sequence;
  APPLE_AGX_U32 TaEventNumber;
  APPLE_AGX_U32 D3EventNumber;
  /* InitBM is valid only for the first submission of a buffer manager. */
  APPLE_AGX_BOOL IncludeInitBm;
} APPLE_AGX_EXP208_DYNAMIC_INPUT;

typedef struct _APPLE_AGX_EXP208_DYNAMIC_RESULT {
  APPLE_AGX_U32 TaPreviousStamp;
  APPLE_AGX_U32 D3PreviousStamp;
  APPLE_AGX_U32 TaCurrentStamp;
  APPLE_AGX_U32 D3CurrentStamp;
  APPLE_AGX_U32 EventCount;
  APPLE_AGX_U32 Start3dQueueCommandCount;
} APPLE_AGX_EXP208_DYNAMIC_RESULT;

APPLE_AGX_BOOL AppleAgxExp208DeriveDynamic(
    const APPLE_AGX_EXP208_DYNAMIC_INPUT *Input,
    APPLE_AGX_EXP208_DYNAMIC_RESULT *Result);

/*
 * Rewrite only the proven dynamic scalar fields in a materialized EXP208
 * G13/V13_5 arena.  Address relocation must have completed separately.
 * Validation is performed before the first write, so rejection is atomic.
 */
APPLE_AGX_BOOL AppleAgxExp208PatchDynamic(
    void *Image, APPLE_AGX_U32 ImageCapacity,
    const APPLE_AGX_EXP208_DYNAMIC_INPUT *Input);

#endif /* APPLE_AGX_EXP208_DYNAMIC_H */
