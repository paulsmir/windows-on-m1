#include "mpo_hold.h"

/* HOLD-1: see mpo_hold.h. */

#define MPO_HOLD_NULL ((void *)0)

unsigned int AdmissionMpoHoldAdmit(const ADMISSION_MPO_HOLD *Hold,
                                   unsigned int ListedMask) {
  if (Hold == MPO_HOLD_NULL || ListedMask == 0u || (ListedMask & ~3u) != 0u)
    return 0u;
  return Hold->Valid == 0u || (Hold->ListedMask & ListedMask) == 0u ? 1u : 0u;
}

void AdmissionMpoHoldMerge(ADMISSION_MPO_HOLD *Hold, unsigned int ListedMask,
                           unsigned int Count, const unsigned int *Layer,
                           const unsigned long long *PresentId) {
  unsigned int i;
  if (Hold == MPO_HOLD_NULL || !AdmissionMpoHoldAdmit(Hold, ListedMask))
    return;
  if (Hold->Valid == 0u) {
    Hold->ListedMask = 0u;
    Hold->Count = 0u;
  }
  Hold->Valid = 1u;
  Hold->ListedMask |= ListedMask;
  for (i = 0u; i < Count && Hold->Count < ADMISSION_MPO_HOLD_LAYERS; ++i) {
    Hold->Layer[Hold->Count] = Layer[i];
    Hold->PresentId[Hold->Count] = PresentId[i];
    ++Hold->Count;
  }
}

unsigned int AdmissionMpoHoldTake(ADMISSION_MPO_HOLD *Hold,
                                  ADMISSION_MPO_HOLD *Out) {
  if (Hold == MPO_HOLD_NULL || Out == MPO_HOLD_NULL || Hold->Valid == 0u)
    return 0u;
  *Out = *Hold;
  AdmissionMpoHoldClear(Hold);
  return 1u;
}

void AdmissionMpoHoldRestore(ADMISSION_MPO_HOLD *Hold,
                             const ADMISSION_MPO_HOLD *Taken) {
  if (Hold == MPO_HOLD_NULL || Taken == MPO_HOLD_NULL || Taken->Valid == 0u)
    return;
  *Hold = *Taken;
}

void AdmissionMpoHoldClear(ADMISSION_MPO_HOLD *Hold) {
  unsigned int i;
  if (Hold == MPO_HOLD_NULL)
    return;
  Hold->Valid = 0u;
  Hold->ListedMask = 0u;
  Hold->Count = 0u;
  for (i = 0u; i < ADMISSION_MPO_HOLD_LAYERS; ++i) {
    Hold->Layer[i] = 0u;
    Hold->PresentId[i] = 0ULL;
  }
}
