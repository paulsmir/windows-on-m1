#ifndef UMD_CPU_POOL_REHOME_H
#define UMD_CPU_POOL_REHOME_H

/* rc107 (EXP1974): a CPU-visible canonical ("SystemDirect") pool allocation
 * keeps the aperture in its supported set, because dxgmms2 refuses a
 * CpuVisible allocation without one on a WDDM 3.0 driver (RC51 decode,
 * dxgmms2 RVA 0x88758). When segment 3 is full (EXP1974: peak 50161 of
 * 50176 granules, 3677 MiB promised to a 3136 MiB segment), VidMm keeps such
 * an allocation resident by republishing it in segment 0 on scattered 4-KiB
 * system pages, which the 16-KiB UAT cannot map: the submission referencing
 * it then fails native proof (LeafAbsent) and the device is removed. A CPU
 * lock does not pin the placement (Lock2 flags are reserved).
 *
 * The bounded repair asks VidMm to page that one allocation in again while
 * the pool has room: evict idle slots now (EvictOnlyIfNecessary=0, which
 * EXP1124 showed pages out at the first opportunity), evict the demoted
 * allocation now, wait until the KMD sees no valid non-pool page of it, then
 * make it resident again. The page-in is expected to land in the preferred
 * segment (segment 3) once it has room; the KMD census then verifies where
 * it landed, and the complete native proof follows. Neither the census nor
 * this repair ever substitutes for that proof.
 *
 * Plain C, no CRT, C and /TP C++. */

/* Callers that own the slot exclusively for the repair. */
#define ADMISSION_UMD_REHOME_SUBMIT 0u /* in this submission's held set */
#define ADMISSION_UMD_REHOME_MAP 1u    /* reserved by a CPU map (Transition) */

/* Repairs per submission or map; pass N evicts up to Bytes * (1 + 3 * N)
 * of idle residency first. */
#define ADMISSION_UMD_REHOME_ROUNDS 2u
/* Touch + placement census polls while VidMm processes the eviction. */
#define ADMISSION_UMD_REHOME_POLLS 16u

typedef struct _ADMISSION_UMD_REHOME_SLOT {
  int Active, Resident, Transition, CopyHeld, Mapped;
  int SystemDirect, Borrowed, Direct, PrivateStaging, NativeStaging;
  int KmtResidencyHeld;
  unsigned SubmissionHolds, SourceHolds, KernelAllocation;
  unsigned long long CanonicalGpuVa, Bytes;
} ADMISSION_UMD_REHOME_SLOT;

/* Only an ordinary CPU-visible pool canonical allocation can move back into
 * the pool. Native-staging (ExistingSysMem), borrowed, presentation Direct
 * and private-staging slots have other backing owners. The caller must own
 * the slot: held by the submission being proved, or reserved by a CPU map
 * that has not published its pointer, with no other hold of either kind. */
static inline int AdmissionUmdRehomeEligible(const ADMISSION_UMD_REHOME_SLOT *s,
                                             unsigned Mode) {
  if (!s->Active || !s->Resident || !s->SystemDirect || s->Borrowed ||
      s->Direct || s->PrivateStaging || s->NativeStaging ||
      s->KmtResidencyHeld || s->SourceHolds || !s->KernelAllocation ||
      !s->CanonicalGpuVa || !s->Bytes)
    return 0;
  if (Mode == ADMISSION_UMD_REHOME_SUBMIT)
    return !s->Transition && s->CopyHeld && s->SubmissionHolds;
  if (Mode == ADMISSION_UMD_REHOME_MAP)
    return s->Transition && !s->Mapped && !s->CopyHeld && !s->SubmissionHolds;
  return 0;
}

/* Idle residency to evict before paging the allocation back in. */
static inline unsigned long long AdmissionUmdRehomeWant(unsigned long long Bytes,
                                                        unsigned Pass) {
  const unsigned long long factor = 1ull + 3ull * Pass;
  return Bytes > ~0ull / factor ? ~0ull : Bytes * factor;
}

#endif
