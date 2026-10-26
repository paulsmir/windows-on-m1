#include "render_pool_slab.h"

#define ALL_SLOTS 0xffffffffu

static unsigned int LowestSlot(unsigned int Mask) {
  unsigned int i = 0u;
  while (!(Mask & 1u)) {
    Mask >>= 1;
    ++i;
  }
  return i;
}

static void Unlink(ADMISSION_POOL_SLABS *Slabs, ADMISSION_POOL_SLAB *Slab) {
  ADMISSION_POOL_SLAB **at = &Slabs->List;
  while (*at != 0 && *at != Slab) at = &(*at)->Next;
  if (*at == Slab) *at = Slab->Next;
  Slab->Next = 0;
  --Slabs->Stats.Slabs;
}

static unsigned long long Promised(const ADMISSION_POOL_SLABS *Slabs) {
  return (Slabs->Reserved < Slabs->ReserveCap ?
      Slabs->Reserved : Slabs->ReserveCap) + Slabs->PrivateCount;
}

/* An empty slab may go back to Windows beyond SpareLimit empty slabs, and
 * only while the rest still covers the promised slots. */
static int MayFree(const ADMISSION_POOL_SLABS *Slabs) {
  return Slabs->Empty > Slabs->SpareLimit &&
         (Slabs->Stats.Slabs - 1u) * ADMISSION_POOL_SLAB_SLOTS >= Promised(Slabs);
}

static void FreeEmpty(ADMISSION_POOL_SLABS *Slabs, ADMISSION_POOL_SLAB *Slab) {
  Unlink(Slabs, Slab);
  --Slabs->Empty;
  ++Slabs->Stats.FreedSlabs;
  Slabs->FreeSlab(Slabs->Context, Slab);
}

static void Trim(ADMISSION_POOL_SLABS *Slabs) {
  ADMISSION_POOL_SLAB *s = Slabs->List;
  while (s != 0 && MayFree(Slabs)) {
    ADMISSION_POOL_SLAB *next = s->Next;
    if (s->Live == 0u) FreeEmpty(Slabs, s);
    s = next;
  }
}

static ADMISSION_POOL_SLAB *AddSlab(ADMISSION_POOL_SLABS *Slabs) {
  ADMISSION_POOL_SLAB *slab = Slabs->NewSlab(Slabs->Context);
  if (slab == 0) {
    ++Slabs->Stats.NewSlabFailures;
    return 0;
  }
  for (unsigned int i = 0u; i < ADMISSION_POOL_SLAB_SLOTS; ++i)
    slab->PrivateOwner[i] = 0;
  slab->FreeMask = ALL_SLOTS;
  slab->Live = 0u;
  slab->Next = Slabs->List;
  Slabs->List = slab;
  ++Slabs->Empty;
  ++Slabs->Stats.NewSlabs;
  if (++Slabs->Stats.Slabs > Slabs->Stats.PeakSlabs)
    Slabs->Stats.PeakSlabs = Slabs->Stats.Slabs;
  return slab;
}

void AdmissionPoolSlabGive(ADMISSION_POOL_SLABS *Slabs, ADMISSION_POOL_SLAB *Slab,
                           unsigned int Slot) {
  unsigned int bit;
  if (Slabs == 0 || Slab == 0 || Slot >= ADMISSION_POOL_SLAB_SLOTS) return;
  if (Slab->PrivateOwner[Slot] != 0) return;
  bit = 1u << Slot;
  if (Slab->FreeMask & bit) return; /* already free */
  Slab->FreeMask |= bit;
  if (--Slab->Live != 0u) return;
  ++Slabs->Empty;
  if (MayFree(Slabs)) FreeEmpty(Slabs, Slab);
}

int AdmissionPoolSlabReserve(ADMISSION_POOL_SLABS *Slabs, unsigned long long Count) {
  ADMISSION_POOL_SLAB *old_head;
  if (Slabs == 0) return 0;
  /* A wrapped total would discard existing promises and let Trim return
   * backing still needed by admitted allocations. Refuse before mutation. */
  if (Count > ~0ULL - Slabs->Reserved) {
    ++Slabs->Stats.ReserveFailures;
    return 0;
  }
  {
    unsigned long long reserved = Slabs->Reserved + Count;
    unsigned long long native = reserved < Slabs->ReserveCap ?
        reserved : Slabs->ReserveCap;
    if (native > ~0ULL - Slabs->PrivateCount) {
      ++Slabs->Stats.ReserveFailures;
      return 0;
    }
  }
  old_head = Slabs->List;
  Slabs->Reserved += Count;
  while (Slabs->Stats.Slabs * ADMISSION_POOL_SLAB_SLOTS < Promised(Slabs)) {
    int floor = Slabs->MayGrow != 0 && !Slabs->MayGrow(Slabs->Context);
    if (floor || AddSlab(Slabs) == 0) {
      Slabs->Reserved -= Count;
      ++Slabs->Stats.ReserveFailures;
      if (floor) ++Slabs->Stats.FloorRefusals;
      /* AddSlab prepends empty slabs under the caller's pool lock. Return
       * exactly this transaction's pages: Trim would keep new slabs as
       * spares even though the creation that acquired them was refused.
       * Existing promises, occupied slots and preexisting spares survive. */
      while (Slabs->List != old_head) FreeEmpty(Slabs, Slabs->List);
      return 0;
    }
  }
  if (Slabs->Reserved > Slabs->Stats.PeakReserved)
    Slabs->Stats.PeakReserved = Slabs->Reserved;
  return 1;
}

void AdmissionPoolSlabUnreserve(ADMISSION_POOL_SLABS *Slabs, unsigned long long Count) {
  if (Slabs == 0) return;
  Slabs->Reserved = Count < Slabs->Reserved ? Slabs->Reserved - Count : 0u;
  Trim(Slabs);
}

int AdmissionPoolSlabTake(ADMISSION_POOL_SLABS *Slabs, unsigned long long Count,
                          ADMISSION_POOL_SLAB **Slab, unsigned int *Slot) {
  unsigned long long taken = 0u;
  if (Slabs == 0 || Slab == 0 || Slot == 0 || Count == 0u) return 0;
  while (taken < Count) {
    ADMISSION_POOL_SLAB *best = 0, *s;
    /* The most used slab with a free slot; an empty one only when no partly
     * used slab has room (an empty slab has Live 0). */
    for (s = Slabs->List; s != 0; s = s->Next)
      if (s->FreeMask != 0u && (best == 0 || s->Live > best->Live)) best = s;
    if (best == 0) {
      best = AddSlab(Slabs);
      if (best == 0) {
        while (taken != 0u) {
          --taken;
          AdmissionPoolSlabGive(Slabs, Slab[taken], Slot[taken]);
        }
        return 0;
      }
    }
    if (best->Live == 0u) --Slabs->Empty;
    while (taken < Count && best->FreeMask != 0u) {
      unsigned int i = LowestSlot(best->FreeMask);
      best->FreeMask &= ~(1u << i);
      ++best->Live;
      Slab[taken] = best;
      Slot[taken] = i;
      ++taken;
    }
  }
  return 1;
}

/* Raw leases use the same verified contiguous chunks as normal placements,
 * but never borrow the native promise. A temporary additional floor protects
 * both that promise and normal slots still live after unreservation. */
int AdmissionPoolSlabTakePrivate(ADMISSION_POOL_SLABS *Slabs,
                                 ADMISSION_POOL_PRIVATE *Lease) {
  ADMISSION_POOL_SLAB *s, *slot_slab;
  unsigned int slot;
  unsigned long long live = 0u, need;
  if (Slabs == 0 || Lease == 0 || Lease->Slab != 0 ||
      Slabs->PrivateCount >= Slabs->PrivateCap ||
      Promised(Slabs) == ~0ULL)
    return 0;
  for (s = Slabs->List; s != 0; s = s->Next) live += s->Live;
  need = Promised(Slabs);
  if (live > need) need = live;
  if (need == ~0ULL) return 0;
  ++need;
  while (Slabs->Stats.Slabs * ADMISSION_POOL_SLAB_SLOTS < need) {
    if ((Slabs->MayGrow != 0 && !Slabs->MayGrow(Slabs->Context)) ||
        AddSlab(Slabs) == 0) {
      Trim(Slabs);
      return 0;
    }
  }
  /* Enough free slots now exist; Take cannot grow while the pool is locked. */
  if (!AdmissionPoolSlabTake(Slabs, 1u, &slot_slab, &slot)) return 0;
  ++Slabs->PrivateCount;
  slot_slab->PrivateOwner[slot] = Lease;
  Lease->Slab = slot_slab;
  Lease->Slot = slot;
  return 1;
}

int AdmissionPoolSlabTakePrivateRange(ADMISSION_POOL_SLABS *Slabs,
    unsigned long long Count, ADMISSION_POOL_PRIVATE *Leases) {
  unsigned long long i;
  if (Slabs == 0 || Leases == 0 || Count == 0u ||
      Slabs->PrivateCount > Slabs->PrivateCap ||
      Count > Slabs->PrivateCap - Slabs->PrivateCount ||
      Count > ~0ULL - Promised(Slabs))
    return 0;
  /* Validate the entire destination before acquiring anything. Never unwind
   * a lease that belonged to the caller before this transaction. */
  for (i = 0u; i < Count; ++i)
    if (Leases[i].Slab != 0 || Leases[i].Slot != 0u) return 0;
  for (i = 0u; i < Count; ++i) {
    if (!AdmissionPoolSlabTakePrivate(Slabs, &Leases[i])) {
      while (i != 0u) {
        --i;
        (void)AdmissionPoolSlabGivePrivate(Slabs, &Leases[i]);
      }
      return 0;
    }
  }
  return 1;
}

int AdmissionPoolSlabGivePrivate(ADMISSION_POOL_SLABS *Slabs,
                                 ADMISSION_POOL_PRIVATE *Lease) {
  ADMISSION_POOL_SLAB *s;
  unsigned int slot;
  if (Slabs == 0 || Lease == 0 || Lease->Slab == 0 ||
      Lease->Slot >= ADMISSION_POOL_SLAB_SLOTS || Slabs->PrivateCount == 0u)
    return 0;
  /* Find before dereferencing: a copied/stale lease may name a freed slab. */
  for (s = Slabs->List; s != 0 && s != Lease->Slab; s = s->Next) {}
  slot = Lease->Slot;
  if (s == 0 || s->PrivateOwner[slot] != Lease) return 0;
  s->PrivateOwner[slot] = 0;
  --Slabs->PrivateCount;
  Lease->Slab = 0;
  Lease->Slot = 0u;
  AdmissionPoolSlabGive(Slabs, s, slot);
  Trim(Slabs);
  return 1;
}

void AdmissionPoolSlabDrain(ADMISSION_POOL_SLABS *Slabs) {
  if (Slabs == 0 || Slabs->PrivateCount != 0u) return;
  while (Slabs->List != 0) {
    ADMISSION_POOL_SLAB *s = Slabs->List;
    Slabs->List = s->Next;
    s->Next = 0;
    s->Live = 0u;
    s->FreeMask = ALL_SLOTS;
    --Slabs->Stats.Slabs;
    ++Slabs->Stats.FreedSlabs;
    Slabs->FreeSlab(Slabs->Context, s);
  }
  Slabs->Empty = 0u;
}
