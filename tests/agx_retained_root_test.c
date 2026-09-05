#include "../m1n1_windows/src/hv_agx_retained_root.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define PAGE 0x4000ULL
#define ROOT_PA 0x900000000ULL
#define VA 0xffffffa000000000ULL
#define IPA 0x800000000ULL
#define PA 0x810000000ULL

/* Real heap-backed tables, synthetic addresses: no firmware memory can be freed.
 * A removed ownership guard or premature release is an assertion/ASan failure. */
struct fixture {
    struct hv_agx_retained_root core;
    unsigned long long retained[2048];
    unsigned int allocated, released, syncs, fail_at, live, last_release_sync;
    unsigned long long translated;
};

static unsigned char allocate(void *opaque, APPLE_AGX_UAT_PAGE *page)
{
    struct fixture *f = opaque;
    if (++f->allocated == f->fail_at)
        return 0;
    page->PhysicalAddress = 0x820000000ULL + f->allocated * PAGE;
    page->Entries = calloc(2048, sizeof(*page->Entries));
    assert(page->Entries);
    ++f->live;
    return 1;
}

static void release(void *opaque, const APPLE_AGX_UAT_PAGE *page)
{
    struct fixture *f = opaque;
    unsigned int i, j;
    assert(page->PhysicalAddress < ROOT_PA || page->PhysicalAddress >= ROOT_PA + 0x40000);
    assert(page->Entries != f->retained);
    assert(f->syncs > f->last_release_sync);
    f->last_release_sync = f->syncs;
    if (page->Level == 1 || page->Level == 2) {
        for (i = 0; i < f->core.Inventory.PageCount; ++i) {
            APPLE_AGX_UAT_PAGE *parent = &f->core.Pages[i];
            if (parent->Level + 1 != page->Level)
                continue;
            for (j = 0; j < (parent->Level == 0 ? 8u : 2048u); ++j)
                assert((parent->Entries[j] & 0xffffffc000ULL) != page->PhysicalAddress);
        }
    }
    free(page->Entries);
    ++f->released;
    assert(f->live);
    --f->live;
}

static unsigned long long translate(void *opaque, unsigned long long ipa)
{
    struct fixture *f = opaque;
    return ipa >= IPA && ipa < IPA + 16 * PAGE ? f->translated + ipa - IPA : 0;
}

static void sync_tables(void *opaque)
{
    ++((struct fixture *)opaque)->syncs;
}

static void initialize(struct fixture *f)
{
    memset(f, 0, sizeof(*f));
    f->translated = PA;
}

static int prepare(struct fixture *f, unsigned long long epoch)
{
    struct hv_agx_retained_ops ops = { f, allocate, release, translate, sync_tables };
    return hv_agx_retained_prepare(&f->core, ROOT_PA, f->retained, 0x40000, epoch, &ops);
}

static void firmware_boot(struct fixture *f)
{
    f->retained[0] = ROOT_PA + 0x4403;
    f->retained[1] = ROOT_PA + 0xc403;
}

static void check_prefix(struct fixture *f)
{
    assert(f->core.Roots.Ttbr1PhysicalAddress == ROOT_PA);
    assert(f->retained[0] == ROOT_PA + 0x4403);
    assert(f->retained[1] == ROOT_PA + 0xc403);
    assert(hv_agx_retained_prefix_unchanged(&f->core));
}

static void start(struct fixture *f)
{
    initialize(f);
    assert(prepare(f, 1) == HV_AGX_RETAINED_OK);
    firmware_boot(f);
    assert(hv_agx_retained_activate(&f->core) == HV_AGX_RETAINED_OK);
    check_prefix(f);
}

/* Catches copied root, pre-boot private writes, wrong system attributes, stale
 * lifetime reuse, and free-before-stop/free-of-firmware regressions. */
static void lifetime(void)
{
    struct fixture f;
    unsigned long long pa, descriptor, handle, second;
    initialize(&f);
    assert(prepare(&f, 1) == HV_AGX_RETAINED_OK);
    assert(f.core.Prepared && !f.core.Active);
    assert(f.core.Roots.Ttbr1PhysicalAddress == ROOT_PA);
    assert(f.retained[0] == 0 && f.retained[1] == 0);
    assert(hv_agx_retained_activate(&f.core) != HV_AGX_RETAINED_OK);
    firmware_boot(&f);
    f.retained[2047] = 3;
    assert(hv_agx_retained_activate(&f.core) == HV_AGX_RETAINED_OWNERSHIP);
    f.retained[2047] = 0;
    assert(hv_agx_retained_activate(&f.core) == HV_AGX_RETAINED_OK);
    assert(f.core.SystemVa == 0xffffffa080000000ULL && f.core.SystemBytes == PAGE);
    assert(AppleAgxUatResolvePage(0, &f.core.Roots, f.core.SystemVa,
        &f.core.Inventory, &pa, &descriptor) == AppleAgxUatResultOk);
    assert(pa == f.core.SystemPage.PhysicalAddress);
    assert((descriptor & 0xc000000000004fULL) == 0xc0000000000043ULL);
    assert(hv_agx_retained_map(&f.core, 1, VA, IPA, PAGE, &handle) == HV_AGX_RETAINED_OK);
    assert(handle && f.core.MappingCount == 1);
    assert(hv_agx_retained_query(&f.core, 1, handle, VA, IPA, PAGE, &pa) == HV_AGX_RETAINED_OK);
    assert(pa == PA);
    assert(AppleAgxUatResolvePage(0, &f.core.Roots, VA, &f.core.Inventory,
        &pa, &descriptor) == AppleAgxUatResultOk);
    assert((descriptor & 0xc000000000004fULL) == 0xc000000000004bULL);
    check_prefix(&f);
    assert(hv_agx_retained_close(&f.core, 1, 0) == HV_AGX_RETAINED_STATE);
    assert(f.core.Active && f.core.MappingCount == 1);
    assert(hv_agx_retained_unmap(&f.core, 1, handle, VA, IPA, PAGE) == HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_query(&f.core, 1, handle, VA, IPA, PAGE, &pa) != HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_map(&f.core, 1, VA, IPA, PAGE, &second) == HV_AGX_RETAINED_OK);
    assert(second > handle);
    assert(hv_agx_retained_close(&f.core, 1, 1) == HV_AGX_RETAINED_OK);
    assert(!f.core.Prepared && !f.core.Active && !f.core.MappingCount && !f.live);
    assert(f.retained[2] == 0);
    check_prefix(&f);
    assert(hv_agx_retained_close(&f.core, 1, 1) == HV_AGX_RETAINED_OK);
    assert(prepare(&f, 1) != HV_AGX_RETAINED_OK);
    assert(prepare(&f, 2) == HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_activate(&f.core) == HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_query(&f.core, 1, second, VA, IPA, PAGE, &pa) != HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_close(&f.core, 1, 1) != HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_close(&f.core, 2, 1) == HV_AGX_RETAINED_OK);
    assert(!f.live);
}

/* Catches missing checks that would let guest ranges escape their VA window,
 * accept a PA masquerading as IPA, or bypass exact capability ownership. */
static void rejection(void)
{
    struct fixture f;
    unsigned long long h, ignored = 1, pa;
    unsigned int i;
    const unsigned long long bad_pa[] = { 0, PA + 1, 1ULL << 40, ROOT_PA, ROOT_PA + 0x3c000 };
    start(&f);
    assert(hv_agx_retained_map(&f.core, 0, VA, IPA, PAGE, &ignored) != HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_map(&f.core, 2, VA, IPA, PAGE, &ignored) != HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_map(&f.core, 1, VA, IPA, 0, &ignored) != HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_map(&f.core, 1, VA, IPA, 2 * PAGE, &ignored) != HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_map(&f.core, 1, VA + 1, IPA, PAGE, &ignored) != HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_map(&f.core, 1, VA, IPA + 1, PAGE, &ignored) != HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_map(&f.core, 1, VA - PAGE, IPA, PAGE, &ignored) != HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_map(&f.core, 1, VA + 0x20000000, IPA, PAGE, &ignored) != HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_map(&f.core, 1, f.core.SystemVa, IPA, PAGE, &ignored) != HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_map(&f.core, 1, 0xffffff8000000000ULL, IPA, PAGE, &ignored) != HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_map(&f.core, 1, VA, PA, PAGE, &ignored) != HV_AGX_RETAINED_OK);
    for (i = 0; i < sizeof(bad_pa) / sizeof(bad_pa[0]); ++i) {
        f.translated = bad_pa[i];
        assert(hv_agx_retained_map(&f.core, 1, VA, IPA, PAGE, &ignored) != HV_AGX_RETAINED_OK);
        assert(!f.core.MappingCount);
    }
    f.translated = f.core.Roots.Ttbr0PhysicalAddress;
    assert(hv_agx_retained_map(&f.core, 1, VA, IPA, PAGE, &ignored) != HV_AGX_RETAINED_OK);
    f.translated = PA;
    assert(hv_agx_retained_map(&f.core, 1, VA, IPA, PAGE, &h) == HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_map(&f.core, 1, VA, IPA, PAGE, &ignored) == HV_AGX_RETAINED_OWNERSHIP);
    assert(hv_agx_retained_query(&f.core, 1, h + 1, VA, IPA, PAGE, &pa) != HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_query(&f.core, 1, h, VA + PAGE, IPA, PAGE, &pa) != HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_query(&f.core, 1, h, VA, IPA + PAGE, PAGE, &pa) != HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_unmap(&f.core, 1, h, VA, IPA, 2 * PAGE) != HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_unmap(&f.core, 1, h + 1, VA, IPA, PAGE) != HV_AGX_RETAINED_OK);
    assert(f.core.MappingCount == 1);
    for (i = 1; i < 16; ++i)
        assert(hv_agx_retained_map(&f.core, 1, VA + i * 0x2000000ULL,
            IPA + i * PAGE, PAGE, &ignored) == HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_map(&f.core, 1, VA + PAGE, IPA, PAGE, &ignored) != HV_AGX_RETAINED_OK);
    check_prefix(&f);
    assert(hv_agx_retained_close(&f.core, 1, 1) == HV_AGX_RETAINED_OK);
    assert(!f.live);
}

/* Catches table-allocation failures leaving live parent references, mappings,
 * leaked pages, or a modified private prefix. */
static void rollback(void)
{
    struct fixture f;
    unsigned long long h, pa, desc;
    unsigned int fail, live;
    for (fail = 1; fail <= 4; ++fail) {
        initialize(&f);
        f.fail_at = fail;
        if (fail == 1) {
            assert(prepare(&f, 1) == HV_AGX_RETAINED_ALLOCATION);
            assert(!f.live && !f.core.Prepared);
            continue;
        }
        assert(prepare(&f, 1) == HV_AGX_RETAINED_OK);
        firmware_boot(&f);
        assert(hv_agx_retained_activate(&f.core) == HV_AGX_RETAINED_ALLOCATION);
        assert(!f.core.Active && !f.core.MappingCount && f.live == 1);
        assert(f.retained[2] == 0);
        check_prefix(&f);
        f.fail_at = 0;
        assert(hv_agx_retained_activate(&f.core) == HV_AGX_RETAINED_OK);
        assert(hv_agx_retained_close(&f.core, 1, 1) == HV_AGX_RETAINED_OK);
        assert(!f.live);
    }
    start(&f);
    live = f.live;
    f.fail_at = f.allocated + 1;
    assert(hv_agx_retained_map(&f.core, 1, VA, IPA, PAGE, &h) == HV_AGX_RETAINED_ALLOCATION);
    assert(!f.core.MappingCount && f.live == live);
    assert(AppleAgxUatResolvePage(0, &f.core.Roots, VA, &f.core.Inventory,
        &pa, &desc) == AppleAgxUatResultNotMapped);
    check_prefix(&f);
    f.fail_at = 0;
    assert(hv_agx_retained_map(&f.core, 1, VA, IPA, PAGE, &h) == HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_close(&f.core, 1, 1) == HV_AGX_RETAINED_OK);
    assert(!f.live);
}

/* Simulated firmware corruption deliberately keeps production ownership alive;
 * the fixture alone discards its heap pages after the fail-closed verdict. */
static void discard_tainted_fixture(struct fixture *f)
{
    unsigned int i;
    for (i = 0; i < f->core.Inventory.PageCount; ++i)
        if (f->core.Pages[i].Entries != f->retained)
            free(f->core.Pages[i].Entries);
    if (f->core.SystemPage.Entries)
        free(f->core.SystemPage.Entries);
}

/* Catches silent prefix rewriting, foreign slot detachment, and cleanup of a
 * changed system leaf without validating exact owned backing first. */
static void tainted_tables(void)
{
    struct fixture f;
    unsigned long long h;
    unsigned int live, i;
    start(&f);
    live = f.live;
    f.retained[0] ^= 0x400;
    assert(hv_agx_retained_map(&f.core, 1, VA, IPA, PAGE, &h) == HV_AGX_RETAINED_TAINTED);
    assert(!f.core.PrefixUnchanged && f.core.Tainted);
    assert(hv_agx_retained_close(&f.core, 1, 1) == HV_AGX_RETAINED_TAINTED);
    assert(f.live == live && f.retained[0] == ROOT_PA + 0x4003);
    discard_tainted_fixture(&f);

    start(&f);
    live = f.live;
    f.retained[2] = ROOT_PA + 0x10003;
    assert(hv_agx_retained_close(&f.core, 1, 1) == HV_AGX_RETAINED_TAINTED);
    assert(f.live == live && f.retained[2] == ROOT_PA + 0x10003);
    discard_tainted_fixture(&f);

    start(&f);
    live = f.live;
    for (i = 0; i < f.core.Inventory.PageCount; ++i)
        if (f.core.Pages[i].Level == 2)
            f.core.Pages[i].Entries[0] ^= PAGE;
    assert(hv_agx_retained_close(&f.core, 1, 1) == HV_AGX_RETAINED_TAINTED);
    assert(f.live == live);
    discard_tainted_fixture(&f);
}

static void preparation_guards(void)
{
    struct fixture f;
    struct hv_agx_retained_ops ops;
    initialize(&f);
    ops = (struct hv_agx_retained_ops){&f, allocate, release, translate, sync_tables};
    assert(hv_agx_retained_prepare(&f.core, ROOT_PA + 1, f.retained, 0x40000, 1, &ops) != 0);
    assert(hv_agx_retained_prepare(&f.core, ROOT_PA, f.retained, PAGE, 1, &ops) != 0);
    assert(hv_agx_retained_prepare(&f.core, ROOT_PA, f.retained, 0x40000, 0, &ops) != 0);
    assert(hv_agx_retained_prepare(&f.core, (1ULL << 40) - PAGE, f.retained, 0x40000, 1, &ops) != 0);
    assert(!f.allocated);
    assert(prepare(&f, 1) == 0);
    assert(prepare(&f, 2) == HV_AGX_RETAINED_STATE);
    assert(f.allocated == 1);
    assert(hv_agx_retained_close(&f.core, 1, 1) == 0);
    assert(!f.live && f.retained[0] == 0 && f.retained[1] == 0);
    assert(prepare(&f, 2) == 0);
    firmware_boot(&f);
    f.retained[1] = f.retained[0];
    assert(hv_agx_retained_activate(&f.core) == HV_AGX_RETAINED_OWNERSHIP);
    assert(f.live == 1);
    assert(hv_agx_retained_close(&f.core, 2, 1) == 0);
    assert(!f.live);
}

int main(void)
{
    lifetime();
    rejection();
    rollback();
    tainted_tables();
    preparation_guards();
    return 0;
}
