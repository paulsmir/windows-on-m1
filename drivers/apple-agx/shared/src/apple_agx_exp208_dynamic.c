#include "apple_agx_exp208_dynamic.h"

#define EXP208_TA_STAMP_BASE 0x7a000000u
#define EXP208_D3_STAMP_BASE 0x3d000000u
#define EXP208_STAMP_STEP 0x100u

/* Standalone stamp counters.  They contain the previous completed stamp. */
#define EXP208_TA_STAMP2_OFFSET 0x48000u
#define EXP208_D3_STAMP2_OFFSET 0x50000u
#define EXP208_EVENT_COUNT_OFFSET 0x60000u
#define EXP208_TA_STAMP1_OFFSET 0xd0000u
#define EXP208_D3_STAMP1_OFFSET 0xd8000u

/* WorkCommandBarrier. */
#define EXP208_BARRIER_WAIT_VALUE_OFFSET 0x7000cu
#define EXP208_BARRIER_EVENT_OFFSET 0x70010u
#define EXP208_BARRIER_SELF_STAMP_OFFSET 0x70014u

/*
 * Accepted EXP208 GPUMicroSequence is packed at arena +0x78000.
 * m1n1 G13/V13_5 Start3DCmd.queue_cmd_count is its +0x60 field, hence
 * arena +0x78060.  The field is little-endian U64 (not naturally aligned by
 * the arena API contract, so it is still emitted byte-wise below).
 * WorkCommand3D.Start3DStruct7 follows in the object at arena +0x90000.
 */
#define EXP208_START3D_QUEUE_COUNT_OFFSET 0x78060u
#define EXP208_FINALIZE3D_STAMP_OFFSET 0x7823cu
#define EXP208_3D_STRUCT7_STAMP_OFFSET 0x908e4u
#define EXP208_3D_STRUCT7_EVENT_OFFSET 0x908e8u

/* First-job-only WorkCommandInitBM. */
#define EXP208_INITBM_STAMP_OFFSET 0x8001cu

/* TA microsequence and WorkCommandTA/StartTACmdStruct3. */
#define EXP208_FINALIZETA_STAMP_OFFSET 0x8824cu
#define EXP208_TA_D3_EVENT_OFFSET 0x98480u
#define EXP208_TA_WORK_STAMP_OFFSET 0x98484u
#define EXP208_TA_STRUCT3_STAMP_OFFSET 0x98580u
#define EXP208_TA_STRUCT3_EVENT_OFFSET 0x98584u

static void exp208_put_u32(unsigned char *destination, APPLE_AGX_U32 value) {
  destination[0] = (unsigned char)(value & 0xffu);
  destination[1] = (unsigned char)((value >> 8u) & 0xffu);
  destination[2] = (unsigned char)((value >> 16u) & 0xffu);
  destination[3] = (unsigned char)((value >> 24u) & 0xffu);
}

static void exp208_put_u64(unsigned char *destination, APPLE_AGX_U64 value) {
  APPLE_AGX_U32 index;
  for (index = 0u; index < 8u; ++index)
    destination[index] =
        (unsigned char)((value >> (index * 8u)) & 0xffULL);
}

APPLE_AGX_BOOL AppleAgxExp208DeriveDynamic(
    const APPLE_AGX_EXP208_DYNAMIC_INPUT *Input,
    APPLE_AGX_EXP208_DYNAMIC_RESULT *Result) {
  APPLE_AGX_U32 sequence;
  APPLE_AGX_U32 previous_sequence;
  APPLE_AGX_EXP208_DYNAMIC_RESULT candidate;
  const APPLE_AGX_U32 maximum = ~(APPLE_AGX_U32)0;

  if (Input == (const void *)0 || Result == (void *)0)
    return APPLE_AGX_FALSE;
  if (Input->IncludeInitBm != APPLE_AGX_FALSE &&
      Input->IncludeInitBm != APPLE_AGX_TRUE)
    return APPLE_AGX_FALSE;

  sequence = Input->Sequence;
  if (sequence == 0u ||
      Input->TaEventNumber >= APPLE_AGX_EXP208_EVENT_COUNT ||
      Input->D3EventNumber >= APPLE_AGX_EXP208_EVENT_COUNT ||
      Input->TaEventNumber == Input->D3EventNumber ||
      (Input->IncludeInitBm && sequence != 1u))
    return APPLE_AGX_FALSE;

  /* Validate every derived scalar before mutating the caller's image. */
  if (sequence > maximum / 2u ||
      sequence > (maximum - EXP208_TA_STAMP_BASE) / EXP208_STAMP_STEP ||
      sequence > (maximum - EXP208_D3_STAMP_BASE) / EXP208_STAMP_STEP)
    return APPLE_AGX_FALSE;

  previous_sequence = sequence - 1u;
  candidate.TaPreviousStamp =
      EXP208_TA_STAMP_BASE + previous_sequence * EXP208_STAMP_STEP;
  candidate.D3PreviousStamp =
      EXP208_D3_STAMP_BASE + previous_sequence * EXP208_STAMP_STEP;
  candidate.TaCurrentStamp =
      EXP208_TA_STAMP_BASE + sequence * EXP208_STAMP_STEP;
  candidate.D3CurrentStamp =
      EXP208_D3_STAMP_BASE + sequence * EXP208_STAMP_STEP;
  candidate.EventCount = sequence * 2u;
  candidate.Start3dQueueCommandCount = candidate.D3PreviousStamp >> 8u;
  *Result = candidate;
  return APPLE_AGX_TRUE;
}

APPLE_AGX_BOOL AppleAgxExp208PatchDynamic(
    void *Image, APPLE_AGX_U32 ImageCapacity,
    const APPLE_AGX_EXP208_DYNAMIC_INPUT *Input) {
  unsigned char *image = (unsigned char *)Image;
  APPLE_AGX_EXP208_DYNAMIC_RESULT derived;

  if (image == (void *)0 || ImageCapacity < APPLE_AGX_EXP208_IMAGE_BYTES ||
      !AppleAgxExp208DeriveDynamic(Input, &derived))
    return APPLE_AGX_FALSE;

  exp208_put_u32(image + EXP208_TA_STAMP2_OFFSET, derived.TaPreviousStamp);
  exp208_put_u32(image + EXP208_TA_STAMP1_OFFSET, derived.TaPreviousStamp);
  exp208_put_u32(image + EXP208_D3_STAMP2_OFFSET, derived.D3PreviousStamp);
  exp208_put_u32(image + EXP208_D3_STAMP1_OFFSET, derived.D3PreviousStamp);
  exp208_put_u32(image + EXP208_EVENT_COUNT_OFFSET, derived.EventCount);

  exp208_put_u32(image + EXP208_BARRIER_WAIT_VALUE_OFFSET,
                 derived.TaCurrentStamp);
  exp208_put_u32(image + EXP208_BARRIER_EVENT_OFFSET,
                 Input->TaEventNumber);
  exp208_put_u32(image + EXP208_BARRIER_SELF_STAMP_OFFSET,
                 derived.D3CurrentStamp);

  /* Start3DCmd.queue_cmd_count is an unaligned 64-bit little-endian field. */
  exp208_put_u64(image + EXP208_START3D_QUEUE_COUNT_OFFSET,
                 (APPLE_AGX_U64)derived.Start3dQueueCommandCount);
  exp208_put_u32(image + EXP208_FINALIZE3D_STAMP_OFFSET,
                 derived.D3CurrentStamp);
  exp208_put_u32(image + EXP208_3D_STRUCT7_STAMP_OFFSET,
                 derived.D3CurrentStamp);
  exp208_put_u32(image + EXP208_3D_STRUCT7_EVENT_OFFSET,
                 Input->D3EventNumber);

  if (Input->IncludeInitBm)
    exp208_put_u32(image + EXP208_INITBM_STAMP_OFFSET,
                   derived.TaCurrentStamp);

  exp208_put_u32(image + EXP208_FINALIZETA_STAMP_OFFSET,
                 derived.TaCurrentStamp);
  exp208_put_u32(image + EXP208_TA_D3_EVENT_OFFSET,
                 Input->D3EventNumber);
  exp208_put_u32(image + EXP208_TA_WORK_STAMP_OFFSET,
                 derived.TaCurrentStamp);
  exp208_put_u32(image + EXP208_TA_STRUCT3_STAMP_OFFSET,
                 derived.TaCurrentStamp);
  exp208_put_u32(image + EXP208_TA_STRUCT3_EVENT_OFFSET,
                 Input->TaEventNumber);

  return APPLE_AGX_TRUE;
}
