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

unsigned int AdmissionMpoHoldSet3(ADMISSION_MPO_HOLD *Hold,
                                  unsigned int ListedMask,
                                  unsigned int Acceptable, unsigned int Count,
                                  const unsigned int *Layer,
                                  const unsigned long long *PresentId,
                                  unsigned int GateFree,
                                  unsigned int *QueueCommit) {
  unsigned int first;
  if (QueueCommit != 0)
    *QueueCommit = 0u;
  if (Hold == 0 || QueueCommit == 0 || Acceptable == 0u ||
      !AdmissionMpoHoldAdmit(Hold, ListedMask))
    return ADMISSION_MPO_SET3_BUSY;
  first = Hold->Valid == 0u ? 1u : 0u;
  AdmissionMpoHoldMerge(Hold, ListedMask, Count, Layer, PresentId);
  *QueueCommit = GateFree != 0u ? 1u : 0u;
  return first != 0u ? ADMISSION_MPO_SET3_MERGED_FIRST
                     : ADMISSION_MPO_SET3_MERGED_SECOND;
}

unsigned int AdmissionMpoHoldDeadline(long long Now, long long LastVsync,
                                      long long Period, long long Lead,
                                      long long *Deadline, long long *Wait) {
  long long start, deadline;
  if (Deadline != 0) *Deadline = 0;
  if (Wait != 0) *Wait = 0;
  if (Deadline == 0 || Wait == 0 || LastVsync <= 0 || Period <= 0 ||
      Lead < 0 || Lead >= Period || Now < LastVsync)
    return 0u;
  start = LastVsync + ((Now - LastVsync) / Period) * Period;
  deadline = start + Period - Lead;
  *Deadline = deadline;
  if (Now >= deadline)
    return 0u;
  *Wait = deadline - Now;
  return 1u;
}

unsigned int AdmissionMpoHoldReferenceFresh(long long Now, long long LastLatch,
                                            long long Period,
                                            unsigned int MaxPeriods) {
  if (LastLatch <= 0 || Period <= 0 || MaxPeriods == 0u || Now < LastLatch)
    return 0u;
  return Now - LastLatch <= (long long)MaxPeriods * Period ? 1u : 0u;
}

unsigned int AdmissionMpoHoldLatchMissed(long long CommitToLatch,
                                         long long Period, long long Lead) {
  if (Period <= 0 || Lead < 0)
    return 0u;
  return CommitToLatch > Lead + Period / 2 ? 1u : 0u;
}
