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
  /* T13 CPU map lives in private process memory, not canonical backing. */
  int PrivateCpuMap;
  unsigned SubmissionHolds, SourceHolds, KernelAllocation;
  unsigned long long Bytes, LastUseFence;
} ADMISSION_UMD_TRIM_SLOT;

/* Rounds of trim + retry for one residency request. */
#define ADMISSION_UMD_TRIM_ROUNDS 16u

/* A slot may lose its residency reference only when nothing can reach its
 * canonical allocation before the slot is made resident again: no submission
 * holds it (being built or in flight), no canonical CPU map, shared owner or
 * source hold, and no presentation use. A T13 private CPU map survives
 * canonical eviction: VidMm preserves canonical contents, and submission
 * restores residency under its holds before staging copies or GPU use. The
 * private pointer and dirty tracking must remain intact. SystemDirect is
 * eligible only between CPU locks:
 * CPU map reserves Transition through MakeResident, the paging wait and Lock;
 * Mapped then retains our residency reference until Unlock succeeds; it does
 * not pin VidMm's physical placement to the native pool. GPU submissions
 * restore residency under their submission holds. Placement still belongs to
 * VidMm; the KMD must reject any unsupported aperture backing (M2-f8). */
static inline int AdmissionUmdTrimEligible(const ADMISSION_UMD_TRIM_SLOT *s) {
  return s->Active && s->Resident && !s->Transition && !s->CopyHeld &&
         (!s->Mapped || (s->PrivateCpuMap && !s->SystemDirect)) &&
         !s->Direct && !s->Borrowed &&
         !s->KmtResidencyHeld && !s->SubmissionHolds && !s->SourceHolds &&
         s->KernelAllocation && s->Bytes;
}

/* Chooses eligible slots, least recently used first (lower slot index first
 * on equal fences), until their bytes cover Want. If the terminal slot would
 * overshoot, use the smallest remaining eligible slot covering the shortfall
 * (oldest first on equal sizes). Avoid dropping a large heap reference for a
 * small deficit when a smaller idle allocation suffices. At least one slot is chosen
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
    unsigned best = picked;
    unsigned long long remaining = Want > bytes ? Want - bytes : 0ull;
    if (Slots[Chosen[best]].Bytes > remaining) {
      for (j = picked + 1u; j < eligible; ++j) {
        unsigned long long candidate = Slots[Chosen[j]].Bytes;
        if (candidate >= remaining && candidate < Slots[Chosen[best]].Bytes)
          best = j;
      }
    }
    /* Only the terminal choice can move; the earlier LRU prefix is intact. */
    if (best != picked) {
      unsigned swap = Chosen[picked];
      Chosen[picked] = Chosen[best];
      Chosen[best] = swap;
    }
    bytes += Slots[Chosen[picked++]].Bytes;
    if (bytes >= Want) break;
  }
  if (Covered) *Covered = bytes;
  return picked;
}

#endif
