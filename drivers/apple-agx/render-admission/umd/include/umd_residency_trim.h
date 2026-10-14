#ifndef UMD_RESIDENCY_TRIM_H
#define UMD_RESIDENCY_TRIM_H

/* M2-f11 (EXP1271): WDDM 2.0 process residency budget for a pre-D3D12 UMD.
 * Microsoft (PFND3DDDI_MAKERESIDENTCB): E_OUTOFMEMORY means VidMm cannot make
 * every requested allocation resident within the process budget, and
 * NumBytesToTrim says how much the driver must remove from the device
 * residency requirement list with pfnEvictCb before it retries. The driver
 * retries in a loop, trimming between attempts, until the request succeeds
 * or nothing more can be trimmed. This UMD keeps one persistent residency
 * reference per slot (EXP978), so the trimmable set is its idle slots. */

typedef struct _ADMISSION_UMD_TRIM_SLOT {
  int Active, Resident, Transition, CopyHeld, Mapped;
  int Direct, SystemDirect, Borrowed, KmtResidencyHeld;
  unsigned SubmissionHolds, SourceHolds, KernelAllocation;
  unsigned long long Bytes, LastUseFence;
} ADMISSION_UMD_TRIM_SLOT;

/* Rounds of trim + retry for one residency request. */
#define ADMISSION_UMD_TRIM_ROUNDS 16u

/* A slot may lose its residency reference only when nothing can reach its
 * canonical allocation before the slot is made resident again: no submission
 * holds it (being built or in flight), no CPU map, shared owner or source
 * hold, no presentation use, and it is GPU-local. A CPU-visible class BO made
 * resident again under pressure can land in 4 KiB system pages the UAT
 * cannot map (M2-f8), so SystemDirect slots keep their reference. */
static inline int AdmissionUmdTrimEligible(const ADMISSION_UMD_TRIM_SLOT *s) {
  return s->Active && s->Resident && !s->Transition && !s->CopyHeld &&
         !s->Mapped && !s->Direct && !s->SystemDirect && !s->Borrowed &&
         !s->KmtResidencyHeld && !s->SubmissionHolds && !s->SourceHolds &&
         s->KernelAllocation && s->Bytes;
}

/* Chooses eligible slots, least recently used first (lower slot index first
 * on equal fences), until their bytes cover Want. At least one slot is chosen
 * when any is eligible, since VidMm may refuse with NumBytesToTrim 0. Writes
 * the slot indices to Chosen (capacity Count) and returns how many it chose;
 * *Covered receives their bytes. */
static inline unsigned AdmissionUmdTrimChoose(
    const ADMISSION_UMD_TRIM_SLOT *Slots, unsigned Count,
    unsigned long long Want, unsigned *Chosen,
    unsigned long long *Covered) {
  unsigned eligible = 0u, picked = 0u, i, j;
  unsigned long long bytes = 0ull;
  for (i = 0u; i < Count; ++i)
    if (AdmissionUmdTrimEligible(&Slots[i])) Chosen[eligible++] = i;
  for (i = 1u; i < eligible; ++i) {
    unsigned v = Chosen[i];
    for (j = i; j > 0u &&
         Slots[Chosen[j - 1u]].LastUseFence > Slots[v].LastUseFence; --j)
      Chosen[j] = Chosen[j - 1u];
    Chosen[j] = v;
  }
  while (picked < eligible) {
    bytes += Slots[Chosen[picked++]].Bytes;
    if (bytes >= Want) break;
  }
  if (Covered) *Covered = bytes;
  return picked;
}

#endif
