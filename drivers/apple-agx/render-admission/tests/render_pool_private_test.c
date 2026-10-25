#include <assert.h>
#include <stdlib.h>
#include <string.h>
#include "render_pool_slab.h"

static unsigned calls, frees, limit = 100, grow = 1;
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
static int Grow(void *unused) {(void)unused; return grow;}
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
  return 0;
}
