#ifndef APPLE_AGX_UMD_STAGING_RING_H
#define APPLE_AGX_UMD_STAGING_RING_H

/* RC52: caller-serialized metadata for a bounded staging ring. This does not
 * allocate/map pages, submit work, authenticate a KMD owner or prove completion.
 * Integrators must supply those contracts; numerical user input is not a fence
 * proof. Initialize once on creation, never reset a live/poisoned ring.
 *
 * One serialized producer may queue several slices for one submission. Commit
 * them together only after acceptance with a known fence. Each accepted batch
 * requires a strictly increasing fence; use CommitAll for shared slices. Cancel
 * only before
 * acceptance, in reverse reservation order. If acceptance
 * or completion is uncertain, poison and retain the entire backing owner.
 * CPU-held reservations (readback/persistent views) additionally require an
 * explicit release AFTER their GPU fence has completed. All entries reclaim in
 * FIFO order; alignment and wrap padding belong to the following entry.
 *
 * Header-only plain C types support both C and the x86 /TP C++ build without
 * new external linkage or user CRT dependencies. Storage belongs on the heap.
 * Runtime routing is intentionally not connected until the Windows mapping
 * lifetime and native backing owner are established. */
#define ADMISSION_UMD_RING_MAX_BYTES (128u * 1024u * 1024u)
#define ADMISSION_UMD_RING_MAX_SLICES 256u

typedef struct _ADMISSION_UMD_RING_SLICE {
  unsigned long long Ticket;
  unsigned int Offset, Bytes;
} ADMISSION_UMD_RING_SLICE;

typedef struct _ADMISSION_UMD_RING_ENTRY {
  ADMISSION_UMD_RING_SLICE Slice;
  unsigned long long Fence;
  unsigned int Span, BeforeWrite, Submitted, CpuHeld;
} ADMISSION_UMD_RING_ENTRY;

typedef struct _ADMISSION_UMD_STAGING_RING {
  unsigned int Capacity, Used, Write, Head, Count, Pending, Poisoned;
  unsigned long long NextTicket, LastFence, Completed;
  ADMISSION_UMD_RING_ENTRY Entries[ADMISSION_UMD_RING_MAX_SLICES];
} ADMISSION_UMD_STAGING_RING;

static inline int AdmissionUmdRingInit(ADMISSION_UMD_STAGING_RING *Ring,
                                       unsigned int Capacity) {
  unsigned int i;
  if (!Ring || !Capacity || Capacity > ADMISSION_UMD_RING_MAX_BYTES) return 0;
  Ring->Capacity = Capacity;
  Ring->Used = Ring->Write = Ring->Head = Ring->Count = Ring->Pending = 0u;
  Ring->Poisoned = 0u;
  Ring->NextTicket = Ring->LastFence = Ring->Completed = 0ULL;
  for (i = 0u; i < ADMISSION_UMD_RING_MAX_SLICES; ++i) {
    ADMISSION_UMD_RING_ENTRY *e = &Ring->Entries[i];
    e->Slice.Ticket = e->Fence = 0ULL;
    e->Slice.Offset = e->Slice.Bytes = e->Span = e->BeforeWrite = 0u;
    e->Submitted = e->CpuHeld = 0u;
  }
  return 1;
}

static inline int AdmissionUmdRingReserve(ADMISSION_UMD_STAGING_RING *Ring,
    unsigned int Bytes, unsigned int Alignment, unsigned int CpuHeld,
    ADMISSION_UMD_RING_SLICE *Slice) {
  unsigned long long aligned, span;
  unsigned int start, index;
  ADMISSION_UMD_RING_ENTRY *e;
  if (!Ring || !Slice || !Ring->Capacity || Ring->Poisoned ||
      !Bytes || Bytes > Ring->Capacity || !Alignment ||
      (Alignment & (Alignment - 1u)) || Alignment > Ring->Capacity ||
      CpuHeld > 1u || Ring->Count == ADMISSION_UMD_RING_MAX_SLICES ||
      Ring->NextTicket == ~0ULL || Ring->LastFence == ~0ULL || Ring->Completed == ~0ULL) return 0;
  start = Ring->Count ? Ring->Write : 0u;
  aligned = ((unsigned long long)start + Alignment - 1u) &
      ~((unsigned long long)Alignment - 1u);
  if (aligned + Bytes > Ring->Capacity) {
    span = (unsigned long long)Ring->Capacity - start + Bytes;
    aligned = 0ULL;
  } else span = aligned - start + Bytes;
  if (span > Ring->Capacity - Ring->Used) return 0;
  index = (Ring->Head + Ring->Count) % ADMISSION_UMD_RING_MAX_SLICES;
  e = &Ring->Entries[index];
  e->Slice.Ticket = ++Ring->NextTicket;
  e->Slice.Offset = (unsigned int)aligned;
  e->Slice.Bytes = Bytes;
  e->Fence = 0ULL;
  e->Span = (unsigned int)span;
  e->BeforeWrite = start;
  e->Submitted = 0u;
  e->CpuHeld = CpuHeld;
  Ring->Used += e->Span;
  Ring->Write = (unsigned int)(aligned + Bytes);
  if (Ring->Write == Ring->Capacity) Ring->Write = 0u;
  ++Ring->Count;
  ++Ring->Pending;
  *Slice = e->Slice;
  return 1;
}

static inline ADMISSION_UMD_RING_ENTRY *AdmissionUmdRingPending(
    ADMISSION_UMD_STAGING_RING *Ring, unsigned long long Ticket) {
  ADMISSION_UMD_RING_ENTRY *e;
  if (!Ring || Ring->Poisoned || !Ring->Pending || !Ring->Count || !Ticket)
    return 0;
  e = &Ring->Entries[(Ring->Head + Ring->Count - 1u) % ADMISSION_UMD_RING_MAX_SLICES];
  return !e->Submitted && e->Slice.Ticket == Ticket ? e : 0;
}

static inline int AdmissionUmdRingCommit(ADMISSION_UMD_STAGING_RING *Ring,
    unsigned long long Ticket, unsigned long long Fence) {
  ADMISSION_UMD_RING_ENTRY *e = AdmissionUmdRingPending(Ring, Ticket);
  if (!e || Ring->Pending != 1u || !Fence || Fence <= Ring->LastFence || Fence <= Ring->Completed)
    return 0;
  e->Fence = Fence;
  e->Submitted = 1u;
  Ring->LastFence = Fence;
  Ring->Pending = 0u;
  return 1;
}

/* All pending spans belong to one accepted batch. A clean pre-accept refusal
 * cancels them backwards; any partial/unknown acceptance poisons the ring. */
static inline int AdmissionUmdRingCommitAll(ADMISSION_UMD_STAGING_RING *Ring,
                                            unsigned long long Fence) {
  unsigned int i;
  if (!Ring || Ring->Poisoned || !Ring->Pending || !Fence ||
      Fence <= Ring->LastFence || Fence <= Ring->Completed) return 0;
  for (i = Ring->Count - Ring->Pending; i < Ring->Count; ++i) {
    ADMISSION_UMD_RING_ENTRY *e =
        &Ring->Entries[(Ring->Head + i) % ADMISSION_UMD_RING_MAX_SLICES];
    e->Fence = Fence;
    e->Submitted = 1u;
  }
  Ring->LastFence = Fence;
  Ring->Pending = 0u;
  return 1;
}

static inline int AdmissionUmdRingCancel(ADMISSION_UMD_STAGING_RING *Ring,
                                         unsigned long long Ticket) {
  ADMISSION_UMD_RING_ENTRY *e = AdmissionUmdRingPending(Ring, Ticket);
  if (!e) return 0;
  Ring->Used -= e->Span;
  Ring->Write = e->BeforeWrite;
  --Ring->Count;
  --Ring->Pending;
  e->Slice.Ticket = 0ULL;
  return 1;
}

static inline void AdmissionUmdRingReclaim(ADMISSION_UMD_STAGING_RING *Ring) {
  while (Ring->Count) {
    ADMISSION_UMD_RING_ENTRY *e = &Ring->Entries[Ring->Head];
    if (!e->Submitted || e->Fence > Ring->Completed || e->CpuHeld) break;
    Ring->Used -= e->Span;
    e->Slice.Ticket = 0ULL;
    Ring->Head = (Ring->Head + 1u) % ADMISSION_UMD_RING_MAX_SLICES;
    --Ring->Count;
  }
  if (!Ring->Count) Ring->Write = 0u;
}

static inline int AdmissionUmdRingRetire(ADMISSION_UMD_STAGING_RING *Ring,
                                         unsigned long long Completed) {
  if (!Ring || !Ring->Capacity || Ring->Poisoned || Completed < Ring->Completed)
    return 0;
  Ring->Completed = Completed;
  AdmissionUmdRingReclaim(Ring);
  return 1;
}

static inline int AdmissionUmdRingReleaseCpu(ADMISSION_UMD_STAGING_RING *Ring,
                                             unsigned long long Ticket) {
  unsigned int i;
  if (!Ring || Ring->Poisoned || !Ticket) return 0;
  for (i = 0u; i < Ring->Count; ++i) {
    ADMISSION_UMD_RING_ENTRY *e =
        &Ring->Entries[(Ring->Head + i) % ADMISSION_UMD_RING_MAX_SLICES];
    if (e->Slice.Ticket != Ticket) continue;
    if (!e->Submitted || !e->CpuHeld || e->Fence > Ring->Completed) return 0;
    e->CpuHeld = 0u;
    AdmissionUmdRingReclaim(Ring);
    return 1;
  }
  return 0;
}

static inline void AdmissionUmdRingPoison(ADMISSION_UMD_STAGING_RING *Ring) {
  if (Ring) Ring->Poisoned = 1u;
}

static inline int AdmissionUmdRingDrained(const ADMISSION_UMD_STAGING_RING *Ring) {
  return Ring && Ring->Capacity && !Ring->Poisoned && !Ring->Count && !Ring->Used;
}
#endif
