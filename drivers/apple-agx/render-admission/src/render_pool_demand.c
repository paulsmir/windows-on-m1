#include "render_pool_demand.h"

#define G ADMISSION_POOL_DEMAND_GRANULE

int AdmissionPoolDemandInit(ADMISSION_POOL_DEMAND *Pool,
                            ADMISSION_POOL_GRANULE *Granules,
                            unsigned long long *CandidateList,
                            unsigned long long Count,
                            const ADMISSION_POOL_DEMAND_OPS *Ops) {
  ADMISSION_POOL_DEMAND_STATS zero = {0};
  if (Pool == 0 || Granules == 0 || CandidateList == 0 || Count == 0u ||
      Count > ~0ULL / G || Ops == 0 || Ops->Commit == 0 || Ops->Release == 0)
    return 0;
  Pool->Ops = *Ops;
  Pool->Granules = Granules;
  Pool->CandidateList = CandidateList;
  Pool->Count = Count;
  Pool->Candidates = 0u;
  Pool->ExecutedSeq = 0u;
  Pool->AnyExecuted = 0;
  Pool->ReleaseDelay = 0u;
  Pool->MaxRun = 0u;
  Pool->Stats = zero;
  return 1;
}

/* A granule that may have become releasable joins the reclaim list once. */
static void Consider(ADMISSION_POOL_DEMAND *Pool, unsigned long long Index) {
  ADMISSION_POOL_GRANULE *g = &Pool->Granules[Index];
  if (!g->Committed || g->Pinned || g->Owner != 0 || g->PagingRefs != 0u ||
      g->Candidate)
    return;
  g->Candidate = 1u;
  Pool->CandidateList[Pool->Candidates++] = Index;
}

/* Granules [First, First + Count), all uncommitted, in one call. */
static int CommitRun(ADMISSION_POOL_DEMAND *Pool, unsigned long long First,
                     unsigned long long Count) {
  unsigned long long i;
  if (!Pool->Ops.Commit(Pool->Ops.Context, First, Count)) {
    ++Pool->Stats.CommitFailures;
    return 0;
  }
  for (i = First; i < First + Count; ++i) {
    ADMISSION_POOL_GRANULE *g = &Pool->Granules[i];
    g->Committed = 1u;
    g->Recorded = 0u;
    g->IdleSince = 0u;
  }
  Pool->Stats.Commits += Count;
  ++Pool->Stats.CommitCalls;
  Pool->Stats.CommittedGranules += Count;
  if (Pool->Stats.CommittedGranules > Pool->Stats.PeakGranules)
    Pool->Stats.PeakGranules = Pool->Stats.CommittedGranules;
  return 1;
}

static int Commit(ADMISSION_POOL_DEMAND *Pool, unsigned long long Index) {
  return Pool->Granules[Index].Committed || CommitRun(Pool, Index, 1u);
}

/* Owner's granules lose it; the caller decides whether that is a kill. */
static void Disown(ADMISSION_POOL_DEMAND *Pool, ADMISSION_POOL_PLACEMENT *Owner) {
  unsigned long long i, first = Owner->Base / G, end = first + Owner->Bytes / G;
  for (i = first; i < end; ++i)
    if (Pool->Granules[i].Owner == Owner) {
      Pool->Granules[i].Owner = 0;
      Consider(Pool, i);
    }
  Owner->Base = Owner->Bytes = 0u;
}

ADMISSION_POOL_PLACE AdmissionPoolDemandPlace(ADMISSION_POOL_DEMAND *Pool,
                                              ADMISSION_POOL_PLACEMENT *Owner,
                                              unsigned long long Base,
                                              unsigned long long Bytes) {
  unsigned long long i, first, end;
  if (Pool == 0 || Owner == 0 || Bytes == 0u || ((Base | Bytes) & (G - 1u)) ||
      Base > ~0ULL - Bytes || (Base + Bytes) / G > Pool->Count)
    return AdmissionPoolPlaceInvalid;
  if (Owner->Bytes == Bytes && Owner->Base == Base) return AdmissionPoolPlaced;
  if (Owner->Bytes != 0u) {
    Disown(Pool, Owner);
    ++Pool->Stats.Moves;
  }
  first = Base / G;
  end = first + Bytes / G;
  /* VidMm never overlaps two resident allocations: an owner found in the
   * new range was freed (or moved) without the driver being told. */
  for (i = first; i < end; ++i) {
    ADMISSION_POOL_PLACEMENT *stale = Pool->Granules[i].Owner;
    if (stale != 0) {
      Disown(Pool, stale);
      ++Pool->Stats.Kills;
    }
  }
  /* One commit per run of uncommitted granules, before any is owned. */
  for (i = first; i < end;) {
    unsigned long long j = i;
    if (Pool->Granules[i].Committed) {
      ++i;
      continue;
    }
    while (j < end && !Pool->Granules[j].Committed &&
           (Pool->MaxRun == 0u || j - i < Pool->MaxRun))
      ++j;
    if (!CommitRun(Pool, i, j - i)) {
      /* Nothing in the range is owned yet: committed granules go back. */
      for (j = first; j < end; ++j) Consider(Pool, j);
      return AdmissionPoolPlaceNoMemory;
    }
    i = j;
  }
  for (i = first; i < end; ++i) {
    Pool->Granules[i].Owner = Owner;
    Pool->Granules[i].IdleSince = 0u; /* in use: the idle clock restarts */
  }
  Owner->Base = Base;
  Owner->Bytes = Bytes;
  ++Pool->Stats.Places;
  return AdmissionPoolPlaced;
}

void AdmissionPoolDemandUnplace(ADMISSION_POOL_DEMAND *Pool,
                                ADMISSION_POOL_PLACEMENT *Owner) {
  if (Pool == 0 || Owner == 0 || Owner->Bytes == 0u) return;
  Disown(Pool, Owner);
  ++Pool->Stats.Unplaces;
}

int AdmissionPoolDemandPin(ADMISSION_POOL_DEMAND *Pool,
                           unsigned long long Offset, unsigned long long Bytes) {
  unsigned long long i, end;
  if (Pool == 0 || Bytes == 0u || Offset > ~0ULL - Bytes ||
      Offset + Bytes > Pool->Count * G)
    return 0;
  end = (Offset + Bytes + G - 1u) / G;
  for (i = Offset / G; i < end; ++i) {
    if (!Commit(Pool, i)) return 0;
    if (!Pool->Granules[i].Pinned) {
      Pool->Granules[i].Pinned = 1u;
      ++Pool->Stats.PinnedGranules;
    }
  }
  return 1;
}

void AdmissionPoolDemandPagingRef(ADMISSION_POOL_DEMAND *Pool,
                                  unsigned long long Offset, int Delta) {
  ADMISSION_POOL_GRANULE *g;
  if (Pool == 0 || Offset / G >= Pool->Count) return;
  g = &Pool->Granules[Offset / G];
  if (Delta > 0) {
    ++g->PagingRefs;
    g->IdleSince = 0u;
  } else if (Delta < 0) {
    if (g->PagingRefs == 0u) {
      ++Pool->Stats.RefUnderflows;
      return;
    }
    if (--g->PagingRefs == 0u) Consider(Pool, Offset / G);
  }
}

void AdmissionPoolDemandRecord(ADMISSION_POOL_DEMAND *Pool,
                               unsigned long long Offset,
                               unsigned long long Bytes,
                               unsigned long long Seq) {
  unsigned long long i, end;
  if (Pool == 0 || Bytes == 0u || Offset > ~0ULL - Bytes ||
      Offset >= Pool->Count * G)
    return;
  end = (Offset + Bytes + G - 1u) / G;
  if (end > Pool->Count) end = Pool->Count;
  for (i = Offset / G; i < end; ++i) {
    ADMISSION_POOL_GRANULE *g = &Pool->Granules[i];
    if (!g->Recorded || Seq > g->RecordSeq) g->RecordSeq = Seq;
    g->Recorded = 1u;
  }
}

void AdmissionPoolDemandExecuted(ADMISSION_POOL_DEMAND *Pool,
                                 unsigned long long Seq) {
  if (Pool == 0) return;
  if (!Pool->AnyExecuted || Seq > Pool->ExecutedSeq) Pool->ExecutedSeq = Seq;
  Pool->AnyExecuted = 1;
}

static unsigned long long Reclaim(ADMISSION_POOL_DEMAND *Pool, int Aged,
                                  unsigned long long Now) {
  unsigned long long read, kept = 0u, released = 0u;
  if (Pool == 0) return 0u;
  for (read = 0u; read < Pool->Candidates; ++read) {
    unsigned long long index = Pool->CandidateList[read];
    ADMISSION_POOL_GRANULE *g = &Pool->Granules[index];
    ADMISSION_POOL_RELEASE result;
    if (!g->Committed || g->Pinned || g->Owner != 0 || g->PagingRefs != 0u) {
      g->Candidate = 0u; /* Consider() puts it back when it qualifies. */
      g->IdleSince = 0u;
      continue;
    }
    if (g->Recorded &&
        (!Pool->AnyExecuted || g->RecordSeq > Pool->ExecutedSeq)) {
      Pool->CandidateList[kept++] = index;
      continue;
    }
    if (Aged) {
      if (g->IdleSince == 0u) g->IdleSince = Now + 1u;
      if (Now + 1u - g->IdleSince < Pool->ReleaseDelay) {
        Pool->CandidateList[kept++] = index;
        continue;
      }
    }
    result = Pool->Ops.Release(Pool->Ops.Context, index);
    if (result == AdmissionPoolReleased) {
      g->Committed = 0u;
      g->Recorded = 0u;
      g->Candidate = 0u;
      g->IdleSince = 0u;
      --Pool->Stats.CommittedGranules;
      ++Pool->Stats.Releases;
      ++released;
    } else if (result == AdmissionPoolReleaseBusy) {
      ++Pool->Stats.BusyRetries;
      Pool->CandidateList[kept++] = index;
    } else {
      ++Pool->Stats.ReleaseFailures;
      ++Pool->Stats.PinnedGranules;
      g->Pinned = 1u;
      g->Candidate = 0u;
    }
  }
  Pool->Candidates = kept;
  return released;
}

unsigned long long AdmissionPoolDemandReclaim(ADMISSION_POOL_DEMAND *Pool) {
  return Reclaim(Pool, 0, 0u);
}

unsigned long long AdmissionPoolDemandReclaimAged(ADMISSION_POOL_DEMAND *Pool,
                                                  unsigned long long Now) {
  return Reclaim(Pool, 1, Now);
}

unsigned long long AdmissionPoolDemandSegmentBytes(unsigned long long RamBytes,
                                                   unsigned long long WindowBytes) {
  return AdmissionPoolDemandSegmentBytesEx(RamBytes, WindowBytes, 0u);
}

unsigned long long AdmissionPoolDemandSegmentBytesEx(unsigned long long RamBytes,
                                                     unsigned long long WindowBytes,
                                                     unsigned long long BrokerBytes) {
  const unsigned long long sixteen = 16ULL << 30;
  unsigned long long broker = BrokerBytes ? BrokerBytes
                                          : ADMISSION_POOL_DEMAND_BROKER_BYTES;
  unsigned long long cap = broker > ADMISSION_POOL_DEMAND_BROKER_RESERVE
                               ? broker - ADMISSION_POOL_DEMAND_BROKER_RESERVE : 0u;
  unsigned long long bytes;
  if (WindowBytes < cap) cap = WindowBytes;
  if (RamBytes == 0u) {
    bytes = ADMISSION_POOL_DEMAND_DEFAULT_BYTES;
  } else {
    unsigned long long half = RamBytes / 2u;
    unsigned long long eighty = RamBytes / 5u * 4u + RamBytes % 5u * 4u / 5u;
    unsigned long long above = RamBytes > sixteen ? RamBytes - sixteen : 0u;
    unsigned long long avail = above > half ? above : half;
    if (avail > eighty) avail = eighty;
    if (avail <= ADMISSION_POOL_DEMAND_APERTURE_RESERVE) return 0u;
    bytes = avail - ADMISSION_POOL_DEMAND_APERTURE_RESERVE;
  }
  if (bytes > cap) bytes = cap;
  bytes &= ~(ADMISSION_POOL_DEMAND_SIZE_STEP - 1u);
  return bytes < ADMISSION_POOL_DEMAND_MIN_BYTES ? 0u : bytes;
}
