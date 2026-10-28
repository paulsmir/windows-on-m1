#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "render_pool_slab.h"

static unsigned calls, frees, limit = 100, grow = 1, grow_limit = ~0u;
static ADMISSION_POOL_SLAB *New(void *unused) {
  ADMISSION_POOL_SLAB *s;
  (void)unused;
  if (++calls > limit) return NULL;
  s = calloc(1, sizeof(*s)); assert(s);
  for (unsigned i = 0; i < 32; ++i)
    s->Pa[i] = 0x1000000ULL + calls * 0x200000ULL + i * 0x10000ULL;
  return s;
}
static void Free(void *unused, ADMISSION_POOL_SLAB *s) {
  (void)unused; ++frees; free(s);
}
static int Grow(void *unused) {(void)unused; return grow && calls < grow_limit;}
/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * Whole-allocation acquisition must not leak an acquired prefix on late
 * refusal, mutate active input leases, or consume the native promise. */
static void Range(void) {
  ADMISSION_POOL_SLABS p = {0};
  ADMISSION_POOL_PRIVATE leases[65] = {0}, existing = {0};
  p.NewSlab = New; p.FreeSlab = Free; p.MayGrow = Grow;
  p.ReserveCap = 32; p.PrivateCap = 34;
  calls = frees = 0; limit = 100; grow = 1;
  assert(!AdmissionPoolSlabTakePrivateRange(&p, 0, leases));
  assert(!AdmissionPoolSlabTakePrivateRange(&p, 1, NULL));
  assert(!AdmissionPoolSlabTakePrivateRange(NULL, 1, leases));
  assert(!AdmissionPoolSlabTakePrivateRange(&p, ~0ULL, leases));
  assert(!calls);
  assert(AdmissionPoolSlabReserve(&p, 32));
  assert(AdmissionPoolSlabTakePrivate(&p, &existing));
  /* The 32nd range lease requires a third slab, after 31 successes. */
  limit = calls;
  assert(!AdmissionPoolSlabTakePrivateRange(&p, 33, leases));
  assert(p.PrivateCount == 1 && p.Reserved == 32 && p.Stats.Slabs == 2);
  for (unsigned i = 0; i < 33; ++i) assert(!leases[i].Slab && !leases[i].Slot);
  assert(existing.Slab->PrivateOwner[existing.Slot] == &existing);
  limit = 100; grow_limit = calls;
  assert(!AdmissionPoolSlabTakePrivateRange(&p, 33, leases));
  assert(p.PrivateCount == 1 && p.Stats.Slabs == 2);
  for (unsigned i = 0; i < 33; ++i) assert(!leases[i].Slab);
  grow_limit = ~0u;
  /* Allow one new slab, then refuse the following one. Rollback must also
   * free backing acquired by this transaction, not only return old slots. */
  p.PrivateCap = 66;
  for (unsigned floor = 0; floor < 2; ++floor) {
    unsigned before = frees;
    limit = floor ? 100 : calls + 1;
    grow_limit = floor ? calls + 1 : ~0u;
    assert(!AdmissionPoolSlabTakePrivateRange(&p, 65, leases));
    assert(p.PrivateCount == 1 && p.Reserved == 32 && p.Stats.Slabs == 2);
    assert(frees == before + 1);
    assert(existing.Slab->PrivateOwner[existing.Slot] == &existing);
    for (unsigned i = 0; i < 65; ++i) assert(!leases[i].Slab && !leases[i].Slot);
  }
  limit = 100; grow_limit = ~0u; p.PrivateCap = 34;
  assert(AdmissionPoolSlabTakePrivate(&p, &leases[32]));
  p.PrivateCap = 35;
  { unsigned before = calls;
    assert(!AdmissionPoolSlabTakePrivateRange(&p, 33, leases));
    assert(calls == before && p.PrivateCount == 2 && !leases[0].Slab);
  }
  p.PrivateCap = 34;
  assert(AdmissionPoolSlabGivePrivate(&p, &leases[32]));
  assert(AdmissionPoolSlabTakePrivateRange(&p, 33, leases));
  assert(p.PrivateCount == 34 && p.Stats.Slabs == 3);
  for (unsigned i = 0; i < 33; ++i) {
    assert(leases[i].Slab->PrivateOwner[leases[i].Slot] == &leases[i]);
    assert(!(leases[i].Slab->Pa[leases[i].Slot] & 0xffff));
  }
  { ADMISSION_POOL_SLAB *native[32]; unsigned slots[32], before = calls;
    grow = 0;
    assert(AdmissionPoolSlabTake(&p, 32, native, slots));
    assert(calls == before);
    for (unsigned i = 0; i < 32; ++i)
      AdmissionPoolSlabGive(&p, native[i], slots[i]);
  }
  for (unsigned i = 33; i; --i)
    assert(AdmissionPoolSlabGivePrivate(&p, &leases[i-1]));
  assert(AdmissionPoolSlabGivePrivate(&p, &existing));
  AdmissionPoolSlabUnreserve(&p, 32);
  assert(!p.PrivateCount && !p.Stats.Slabs);
}
/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * rc86: a lease may skip its clear only while its slot still holds the
 * zeroed pages NewSlab returned. A slot ever handed out for a placement, or
 * given back by an owner that did not zero it, must never report Zero; the
 * clean preference must not break packing into the most used slab. */
static void Clean(void) {
  ADMISSION_POOL_SLABS p = {0};
  ADMISSION_POOL_PRIVATE a = {0}, b = {0}, c = {0};
  ADMISSION_POOL_SLAB *slab[33];
  unsigned slot[33];
  p.NewSlab = New; p.FreeSlab = Free; p.MayGrow = Grow;
  p.ReserveCap = 64; p.PrivateCap = 8; p.SpareLimit = 4;
  calls = frees = 0; limit = 100; grow = 1;
  assert(AdmissionPoolSlabTakePrivate(&p, &a));
  assert(a.Zero == 1u);                      /* fresh Windows-zeroed slot */
  { unsigned dirty = a.Slot;
    a.Zero = 0u;                              /* owner used it, no clear */
    assert(AdmissionPoolSlabGivePrivate(&p, &a) && !a.Zero);
    assert(AdmissionPoolSlabTakePrivate(&p, &b));
    assert(b.Zero == 1u && b.Slot != dirty); } /* another clean slot */
  b.Zero = 1u;                                /* owner zeroed it */
  { ADMISSION_POOL_SLAB *was = b.Slab; unsigned at = b.Slot;
    assert(AdmissionPoolSlabGivePrivate(&p, &b));
    assert(AdmissionPoolSlabTakePrivate(&p, &c));
    assert(c.Zero == 1u && c.Slab == was && c.Slot == at);
    assert(AdmissionPoolSlabGivePrivate(&p, &c)); }
  /* Every slot of slab 1 handed out once for placements: dirty. */
  assert(AdmissionPoolSlabTake(&p, 32, slab, slot));
  for (unsigned i = 0; i < 32; ++i) AdmissionPoolSlabGive(&p, slab[i], slot[i]);
  assert(p.Stats.Slabs == 1);
  for (unsigned i = 0; i < 32; ++i) {
    ADMISSION_POOL_PRIVATE l = {0};
    assert(AdmissionPoolSlabTakePrivate(&p, &l) && l.Zero == 0u);
    assert(AdmissionPoolSlabGivePrivate(&p, &l));
  }
  /* Packing: with a partly used slab, a lease goes there even though a
   * newer empty slab is still clean. */
  assert(AdmissionPoolSlabTake(&p, 33, slab, slot)); /* 32 + 1 in a new slab */
  AdmissionPoolSlabGive(&p, slab[0], slot[0]);       /* slab 1: 31 live */
  assert(AdmissionPoolSlabTakePrivate(&p, &a));
  assert(a.Slab == slab[0] && a.Zero == 0u);
  assert(AdmissionPoolSlabGivePrivate(&p, &a));
  for (unsigned i = 1; i < 33; ++i) AdmissionPoolSlabGive(&p, slab[i], slot[i]);
  AdmissionPoolSlabDrain(&p);
}
int main(void) {
  ADMISSION_POOL_SLABS p = {0}, other = {0};
  ADMISSION_POOL_PRIVATE a = {0}, b = {0}, copy;
  ADMISSION_POOL_SLAB *normal[64]; unsigned slots[64];
  p.NewSlab = New; p.FreeSlab = Free; p.MayGrow = Grow; p.ReserveCap = 32;
  assert(!AdmissionPoolSlabTakePrivate(&p, &a)); /* default off */
  assert(calls == 0 && !a.Slab);
  p.PrivateCap = 2;
  assert(AdmissionPoolSlabReserve(&p, 32));
  assert(calls == 1);
  grow = 0;
  assert(!AdmissionPoolSlabTakePrivate(&p, &a)); /* cannot eat promised slots */
  assert(calls == 1 && p.PrivateCount == 0 && p.Reserved == 32);
  grow = 1; limit = calls;
  assert(!AdmissionPoolSlabTakePrivate(&p, &a)); /* OS allocation failure */
  assert(p.PrivateCount == 0 && p.Stats.Slabs == 1 && !a.Slab);
  limit = 100;
  assert(AdmissionPoolSlabTakePrivate(&p, &a));
  assert(AdmissionPoolSlabTakePrivate(&p, &b));
  assert(a.Slab != b.Slab || a.Slot != b.Slot);
  assert(!(a.Slab->Pa[a.Slot] & 0x3fff));
  copy = a;
  assert(!AdmissionPoolSlabTakePrivate(&p, &a)); /* active owner cannot move */
  assert(!AdmissionPoolSlabGivePrivate(&other, &a));
  assert(!AdmissionPoolSlabGivePrivate(&p, &copy)); /* copied lease is not owner */
  AdmissionPoolSlabGive(&p, a.Slab, a.Slot); /* normal release cannot free it */
  assert(!(a.Slab->FreeMask & (1u << a.Slot)));
  { ADMISSION_POOL_PRIVATE c = {0}; unsigned before = calls;
    assert(!AdmissionPoolSlabTakePrivate(&p, &c)); assert(calls == before); }
  grow = 0;
  { unsigned before = calls;
    assert(AdmissionPoolSlabTake(&p, 32, normal, slots));
    assert(calls == before); /* private ownership did not steal the promise */
    for (unsigned i = 0; i < 32; ++i) {
      assert(normal[i] != a.Slab || slots[i] != a.Slot);
      assert(normal[i] != b.Slab || slots[i] != b.Slot);
      AdmissionPoolSlabGive(&p, normal[i], slots[i]);
    }
  }
  AdmissionPoolSlabUnreserve(&p, 32);
  { unsigned before = frees; AdmissionPoolSlabDrain(&p); assert(frees == before); }
  assert(AdmissionPoolSlabGivePrivate(&p, &a));
  assert(!a.Slab && !AdmissionPoolSlabGivePrivate(&p, &a));
  assert(!AdmissionPoolSlabGivePrivate(&p, &copy));
  assert(AdmissionPoolSlabGivePrivate(&p, &b));
  assert(p.PrivateCount == 0 && p.Stats.Slabs == 0);
  /* A later native reservation also preserves the existing private slot. */
  grow = 1; p.PrivateCap = 1;
  assert(AdmissionPoolSlabTakePrivate(&p, &a));
  assert(AdmissionPoolSlabReserve(&p, 32));
  assert(p.Stats.Slabs == 2);
  { unsigned before = calls;
    grow = 0; assert(AdmissionPoolSlabTake(&p, 32, normal, slots));
    assert(calls == before);
  }
  AdmissionPoolSlabUnreserve(&p, 32);
  assert(AdmissionPoolSlabGivePrivate(&p, &a));
  for (unsigned i = 0; i < 32; ++i)
    AdmissionPoolSlabGive(&p, normal[i], slots[i]);
  assert(p.Stats.Slabs == 0);
  assert(AdmissionPoolSlabTake(&p, 32, normal, slots));
  /* Normal references can outlive a reservation: private take must still
   * grow at the fallible boundary rather than bypassing the memory floor. */
  assert(!AdmissionPoolSlabTakePrivate(&p, &a));
  grow = 1; assert(AdmissionPoolSlabTakePrivate(&p, &a));
  for (unsigned i = 0; i < 32; ++i)
    AdmissionPoolSlabGive(&p, normal[i], slots[i]);
  assert(AdmissionPoolSlabGivePrivate(&p, &a));
  /* Count arithmetic must fail before growth, even with a maximal cap. */
  p.Reserved = p.ReserveCap = ~0ULL; p.PrivateCap = ~0ULL;
  assert(!AdmissionPoolSlabTakePrivate(&p, &a));
  p.Reserved = p.ReserveCap = 0;
  AdmissionPoolSlabDrain(&p);
  Range();
  Clean();
  return 0;
}
