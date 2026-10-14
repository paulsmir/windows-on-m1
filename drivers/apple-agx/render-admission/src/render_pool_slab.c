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
  return Slabs->Reserved < Slabs->ReserveCap ? Slabs->Reserved : Slabs->ReserveCap;
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
  bit = 1u << Slot;
  if (Slab->FreeMask & bit) return; /* already free */
  Slab->FreeMask |= bit;
  if (--Slab->Live != 0u) return;
  ++Slabs->Empty;
  if (MayFree(Slabs)) FreeEmpty(Slabs, Slab);
}

int AdmissionPoolSlabReserve(ADMISSION_POOL_SLABS *Slabs, unsigned long long Count) {
  if (Slabs == 0) return 0;
  Slabs->Reserved += Count;
  while (Slabs->Stats.Slabs * ADMISSION_POOL_SLAB_SLOTS < Promised(Slabs)) {
    if (AddSlab(Slabs) == 0) {
      Slabs->Reserved -= Count;
      ++Slabs->Stats.ReserveFailures;
      Trim(Slabs);
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

void AdmissionPoolSlabDrain(ADMISSION_POOL_SLABS *Slabs) {
  if (Slabs == 0) return;
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
