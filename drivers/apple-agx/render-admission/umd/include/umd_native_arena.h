#ifndef APPLE_AGX_UMD_NATIVE_ARENA_H
#define APPLE_AGX_UMD_NATIVE_ARENA_H

#include "umd_staging_ring.h"

/* RC62 (EXP1984): caller-serialized slice allocator for one native staging
 * arena. Native slices back BOs with independent lifetimes (persistent upload
 * heaps, query/readback BOs, short-lived texture staging). The RC52 FIFO ring
 * reclaimed only from its head, so one long-lived BO pinned all later space
 * once the write cursor wrapped to it, and every further request mapped a new
 * arena. Here a released slice is reusable at once: first fit, lowest offset.
 *
 * Release still needs final BO ownership and the actual last-use fence proof
 * (Completed >= LastUse), exactly as AdmissionUmdRingReleaseOwned did. The
 * live table is kept sorted by offset. Plain C, no CRT, C and /TP C++. */
#define ADMISSION_UMD_NATIVE_ARENA_SLICES 256u

typedef struct _ADMISSION_UMD_NATIVE_SPANS {
  unsigned int Capacity, Used, Count, Poisoned;
  unsigned long long NextTicket, Completed;
  ADMISSION_UMD_RING_SLICE Live[ADMISSION_UMD_NATIVE_ARENA_SLICES];
} ADMISSION_UMD_NATIVE_SPANS;

static inline int AdmissionUmdNativeSpansInit(ADMISSION_UMD_NATIVE_SPANS *Spans,
                                              unsigned int Capacity) {
  unsigned int i;
  if (!Spans || !Capacity || Capacity > ADMISSION_UMD_RING_MAX_BYTES) return 0;
  Spans->Capacity = Capacity;
  Spans->Used = Spans->Count = Spans->Poisoned = 0u;
  Spans->NextTicket = Spans->Completed = 0ULL;
  for (i = 0u; i < ADMISSION_UMD_NATIVE_ARENA_SLICES; ++i) {
    Spans->Live[i].Ticket = 0ULL;
    Spans->Live[i].Offset = Spans->Live[i].Bytes = 0u;
  }
  return 1;
}

static inline int AdmissionUmdNativeSpansReserve(ADMISSION_UMD_NATIVE_SPANS *Spans,
    unsigned int Bytes, unsigned int Alignment, ADMISSION_UMD_RING_SLICE *Slice) {
  unsigned long long cursor = 0ULL, start = 0ULL;
  unsigned int at, i;
  if (!Spans || !Slice || !Spans->Capacity || Spans->Poisoned || !Bytes ||
      Bytes > Spans->Capacity || !Alignment || (Alignment & (Alignment - 1u)) ||
      Alignment > Spans->Capacity || Spans->Count == ADMISSION_UMD_NATIVE_ARENA_SLICES ||
      Spans->NextTicket == ~0ULL) return 0;
  for (at = 0u; at <= Spans->Count; ++at) {
    unsigned long long limit = at < Spans->Count ?
        (unsigned long long)Spans->Live[at].Offset : (unsigned long long)Spans->Capacity;
    start = (cursor + Alignment - 1ULL) & ~((unsigned long long)Alignment - 1ULL);
    if (start <= limit && limit - start >= Bytes) break;
    if (at < Spans->Count)
      cursor = (unsigned long long)Spans->Live[at].Offset + Spans->Live[at].Bytes;
  }
  if (at > Spans->Count) return 0;
  for (i = Spans->Count; i > at; --i) Spans->Live[i] = Spans->Live[i - 1u];
  Spans->Live[at].Ticket = ++Spans->NextTicket;
  Spans->Live[at].Offset = (unsigned int)start;
  Spans->Live[at].Bytes = Bytes;
  ++Spans->Count;
  Spans->Used += Bytes;
  *Slice = Spans->Live[at];
  return 1;
}

/* Only after the BO has no CPU/submission references and DeallocateCb has
 * succeeded. LastUse is the actual last render fence (zero for never used);
 * Completed is a proof from that device's monitored timeline, never a ticket.
 * A failed/uncertain submission retains the owner and must not call this. */
static inline int AdmissionUmdNativeSpansRelease(ADMISSION_UMD_NATIVE_SPANS *Spans,
    unsigned long long Ticket, unsigned long long LastUse,
    unsigned long long Completed) {
  unsigned int i;
  if (!Spans || Spans->Poisoned || !Ticket || Completed < LastUse) return 0;
  for (i = 0u; i < Spans->Count; ++i) {
    if (Spans->Live[i].Ticket != Ticket) continue;
    Spans->Used -= Spans->Live[i].Bytes;
    for (; i + 1u < Spans->Count; ++i) Spans->Live[i] = Spans->Live[i + 1u];
    --Spans->Count;
    Spans->Live[Spans->Count].Ticket = 0ULL;
    Spans->Live[Spans->Count].Offset = Spans->Live[Spans->Count].Bytes = 0u;
    if (Completed > Spans->Completed) Spans->Completed = Completed;
    return 1;
  }
  return 0;
}

static inline void AdmissionUmdNativeSpansPoison(ADMISSION_UMD_NATIVE_SPANS *Spans) {
  if (Spans) Spans->Poisoned = 1u;
}

static inline int AdmissionUmdNativeSpansDrained(const ADMISSION_UMD_NATIVE_SPANS *Spans) {
  return Spans && Spans->Capacity && !Spans->Poisoned && !Spans->Count && !Spans->Used;
}
#endif
