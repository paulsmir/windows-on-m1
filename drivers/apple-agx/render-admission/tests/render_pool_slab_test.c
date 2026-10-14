#include "render_pool_slab.h"

#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define SLOTS ADMISSION_POOL_SLAB_SLOTS
static unsigned news, frees, fail_new_at;
static ADMISSION_POOL_SLAB *live_slabs[64];

static ADMISSION_POOL_SLAB *fake_new(void *context)
{
    (void)context;
    if (fail_new_at && news + 1 == fail_new_at) {
        ++news;
        return 0;
    }
    ADMISSION_POOL_SLAB *s = calloc(1, sizeof(*s));
    assert(s);
    s->Handle = (void *)(unsigned long)(news + 1);
    for (unsigned i = 0; i < SLOTS; ++i)
        s->Pa[i] = 0x900000000ULL + (unsigned long long)news * 0x200000ULL + i * 0x10000ULL;
    live_slabs[news++] = s;
    return s;
}

static void fake_free(void *context, ADMISSION_POOL_SLAB *slab)
{
    (void)context;
    assert(slab->Live == 0);
    for (unsigned i = 0; i < 64; ++i)
        if (live_slabs[i] == slab)
            live_slabs[i] = 0;
    ++frees;
    free(slab);
}

static ADMISSION_POOL_SLABS slabs;

static void reset(unsigned long long spare)
{
    AdmissionPoolSlabDrain(&slabs);
    memset(&slabs, 0, sizeof(slabs));
    memset(live_slabs, 0, sizeof(live_slabs));
    news = frees = fail_new_at = 0;
    slabs.NewSlab = fake_new;
    slabs.FreeSlab = fake_free;
    slabs.SpareLimit = spare;
}

static void test_one_slab_serves_32_granules(void)
{
    ADMISSION_POOL_SLAB *s[40];
    unsigned slot[40];
    reset(0);
    assert(AdmissionPoolSlabTake(&slabs, 3, s, slot));
    assert(news == 1 && s[0] == s[2] && s[0]->Live == 3);
    /* Distinct slots with 64 KiB-aligned addresses. */
    assert(slot[0] != slot[1] && slot[1] != slot[2]);
    for (unsigned i = 0; i < 3; ++i)
        assert(!(s[i]->Pa[slot[i]] & 0xffff));
    assert(AdmissionPoolSlabTake(&slabs, 29, s + 3, slot + 3));
    assert(news == 1 && s[3]->Live == 32 && s[3]->FreeMask == 0);
    /* The 33rd granule needs a second slab. */
    assert(AdmissionPoolSlabTake(&slabs, 1, s + 32, slot + 32));
    assert(news == 2 && s[32] != s[0] && slabs.Stats.Slabs == 2);
}

static void test_packing_prefers_partly_used_slabs(void)
{
    ADMISSION_POOL_SLAB *s[64];
    unsigned slot[64];
    reset(1);
    assert(AdmissionPoolSlabTake(&slabs, 40, s, slot)); /* slabs A (32) and B (8) */
    assert(news == 2);
    ADMISSION_POOL_SLAB *a = s[0], *b = s[39];
    assert(a != b && b->Live == 8);
    /* Free 4 slots of A: a new request fills B first (more used? no: B has
     * 8 live, A has 28) -- the most used partial slab is A. */
    for (unsigned i = 0; i < 4; ++i)
        AdmissionPoolSlabGive(&slabs, s[i], slot[i]);
    assert(a->Live == 28);
    ADMISSION_POOL_SLAB *t[4];
    unsigned ts[4];
    assert(AdmissionPoolSlabTake(&slabs, 4, t, ts));
    for (unsigned i = 0; i < 4; ++i)
        assert(t[i] == a);
    assert(news == 2 && a->Live == 32);
}

static void test_empty_slabs_return_beyond_the_spare(void)
{
    ADMISSION_POOL_SLAB *s[96];
    unsigned slot[96];
    reset(1);
    assert(AdmissionPoolSlabTake(&slabs, 96, s, slot)); /* three full slabs */
    assert(news == 3);
    for (unsigned i = 0; i < 32; ++i)
        AdmissionPoolSlabGive(&slabs, s[i], slot[i]);
    /* One empty slab is kept as the spare. */
    assert(frees == 0 && slabs.Empty == 1);
    for (unsigned i = 32; i < 64; ++i)
        AdmissionPoolSlabGive(&slabs, s[i], slot[i]);
    assert(frees == 1 && slabs.Empty == 1 && slabs.Stats.Slabs == 2);
    /* The spare serves the next commit without Windows. */
    ADMISSION_POOL_SLAB *t[1];
    unsigned ts[1];
    assert(AdmissionPoolSlabTake(&slabs, 1, t, ts));
    assert(news == 3 && slabs.Empty == 0);
}

static void test_take_is_all_or_nothing(void)
{
    ADMISSION_POOL_SLAB *s[64];
    unsigned slot[64];
    reset(0);
    assert(AdmissionPoolSlabTake(&slabs, 30, s, slot));
    fail_new_at = 2; /* the second slab cannot be had */
    assert(!AdmissionPoolSlabTake(&slabs, 10, s + 30, slot + 30));
    assert(slabs.Stats.NewSlabFailures == 1 && s[0]->Live == 30);
    /* The two slots taken from the first slab went back. */
    assert(s[0]->FreeMask == 0xc0000000u);
    fail_new_at = 0;
    assert(AdmissionPoolSlabTake(&slabs, 10, s + 30, slot + 30));
    assert(s[0]->Live == 32 && slabs.Stats.Slabs == 2);
}

static void test_drain(void)
{
    ADMISSION_POOL_SLAB *s[40];
    unsigned slot[40];
    reset(2);
    assert(AdmissionPoolSlabTake(&slabs, 40, s, slot));
    for (unsigned i = 0; i < 40; ++i)
        AdmissionPoolSlabGive(&slabs, s[i], slot[i]);
    assert(frees == 0 && slabs.Empty == 2);
    AdmissionPoolSlabDrain(&slabs);
    assert(frees == 2 && slabs.List == 0 && slabs.Stats.Slabs == 0);
}

int main(void)
{
    test_one_slab_serves_32_granules();
    test_packing_prefers_partly_used_slabs();
    test_empty_slabs_return_beyond_the_spare();
    test_take_is_all_or_nothing();
    test_drain();
    reset(0);
    AdmissionPoolSlabDrain(&slabs);
    return 0;
}
