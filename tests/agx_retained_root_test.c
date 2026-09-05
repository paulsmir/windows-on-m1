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
    return ipa >= IPA && ipa < IPA + 257 * PAGE ? f->translated + ipa - IPA : 0;
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
    for (i = 1; i < 256; ++i)
        assert(hv_agx_retained_map(&f.core, 1, VA + i * PAGE,
            IPA + i * PAGE, PAGE, &ignored) == HV_AGX_RETAINED_OK);
    assert(hv_agx_retained_map(&f.core, 1, VA + 256 * PAGE, IPA, PAGE, &ignored) ==
        HV_AGX_RETAINED_ALLOCATION);
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

/* A PA-only software walk must not accept a hardware-invalid intermediate
 * descriptor or a descriptor targeting anything outside the owned L2 pages. */
static void invalid_intermediate_descriptors(void)
{
    unsigned int operation, corruption;
    for (operation = 0; operation < 4; ++operation) {
        for (corruption = 0; corruption < 5; ++corruption) {
            struct fixture f;
            unsigned long long h, pa = 1, *entry = NULL, invalid;
            unsigned int i, live, released;
            start(&f);
            assert(hv_agx_retained_map(&f.core, 1, VA, IPA, PAGE, &h) == 0);
            for (i = 0; i < f.core.Inventory.PageCount; ++i)
                if (f.core.Pages[i].Level == 1)
                    entry = &f.core.Pages[i].Entries[operation == 3 ? 64 : 0];
            assert(entry && *entry);
            if (corruption == 0)
                invalid = *entry & ~1ULL; /* VALID cleared, PA unchanged. */
            else if (corruption == 1)
                invalid = *entry & ~2ULL; /* TYPE cleared, PA unchanged. */
            else if (corruption == 2)
                invalid = *entry | (1ULL << 55); /* Not our table encoding. */
            else if (corruption == 3)
                invalid = (ROOT_PA + 0x4000) | 3; /* Firmware-owned table. */
            else
                invalid = f.core.Roots.Ttbr0PhysicalAddress | 3; /* Wrong owned level. */
            *entry = invalid;
            live = f.live;
            released = f.released;
            if (operation == 0) {
                assert(hv_agx_retained_query(&f.core, 1, h, VA, IPA, PAGE, &pa) ==
                    HV_AGX_RETAINED_TAINTED);
                assert(pa == 0);
            } else if (operation == 1) {
                assert(hv_agx_retained_unmap(&f.core, 1, h, VA, IPA, PAGE) ==
                    HV_AGX_RETAINED_TAINTED);
            } else {
                assert(hv_agx_retained_close(&f.core, 1, 1) == HV_AGX_RETAINED_TAINTED);
            }
            assert(f.core.Tainted && f.core.MappingCount == 1);
            assert(f.live == live && f.released == released && *entry == invalid);
            check_prefix(&f);
            discard_tainted_fixture(&f);
        }
    }
}

/* The production graph exceeds the old record capacity and aliases its buffer
 * manager page into TTBR0. A broad low window or copied TTBR1 fails this test. */
static void production_inventory(void)
{
    struct fixture f;
    unsigned long long handles[200], alias, ignored, pa, descriptor;
    unsigned long long root0, system_pa;
    unsigned int i, lifetime;
    initialize(&f);
    for (lifetime = 1; lifetime <= 2; ++lifetime) {
        assert(prepare(&f, lifetime) == 0);
        firmware_boot(&f);
        assert(hv_agx_retained_activate(&f.core) == 0);
        root0 = f.core.Roots.Ttbr0PhysicalAddress;
        system_pa = f.core.SystemPage.PhysicalAddress;
        for (i = 0; i < 200; ++i) {
            /* Spread over all 16 permitted high L2 tables. */
            unsigned long long va = VA + (i % 16) * 0x2000000ULL + (i / 16) * PAGE;
            assert(hv_agx_retained_map(&f.core, lifetime, va, IPA + i * PAGE,
                PAGE, &handles[i]) == 0);
            assert(hv_agx_retained_query(&f.core, lifetime, handles[i], va,
                IPA + i * PAGE, PAGE, &pa) == 0 && pa == PA + i * PAGE);
        }
        assert(hv_agx_retained_map(&f.core, lifetime, 0x420000000ULL, IPA,
            PAGE, &alias) == 0);
        assert(hv_agx_retained_query(&f.core, lifetime, alias, 0x420000000ULL,
            IPA, PAGE, &pa) == 0 && pa == PA);
        assert(hv_agx_retained_map(&f.core, lifetime, 0x420004000ULL, IPA,
            PAGE, &ignored) == HV_AGX_RETAINED_RANGE);
        assert(hv_agx_retained_map(&f.core, lifetime, 0x41fffc000ULL, IPA,
            PAGE, &ignored) == HV_AGX_RETAINED_RANGE);
        assert(hv_agx_retained_map(&f.core, lifetime, 0x430000000ULL, IPA,
            PAGE, &ignored) == HV_AGX_RETAINED_RANGE);
        assert(f.core.Roots.Ttbr0PhysicalAddress == root0);
        check_prefix(&f);
        assert(AppleAgxUatResolvePage(0, &f.core.Roots, f.core.SystemVa,
            &f.core.Inventory, &pa, &descriptor) == AppleAgxUatResultOk);
        assert(pa == system_pa);
        assert(hv_agx_retained_close(&f.core, lifetime, 1) == 0);
        assert(!f.live && !f.retained[2]);
        check_prefix(&f);
    }
}

/* An arbitrary query failure cannot establish that a caller's owned mapping
 * was removed. Require the full prior UNMAP tuple and current real absence. */
static void absent_receipt(void)
{
    const unsigned long long vas[] = {VA, 0x420000000ULL};
    unsigned int i;
    for (i = 0; i < 2; ++i) {
        struct fixture f;
        unsigned long long h, newer, keep, va = vas[i];
        start(&f);
        assert(hv_agx_retained_map(&f.core, 1, va, IPA, PAGE, &h) == 0);
        /* Keep the high L2 present to distinguish a zero leaf from pruning. */
        assert(hv_agx_retained_map(&f.core, 1, VA + PAGE, IPA + PAGE,
            PAGE, &keep) == 0);
        assert(hv_agx_retained_verify_absent(&f.core, 1, h, va, IPA, PAGE) ==
            HV_AGX_RETAINED_OWNERSHIP);
        assert(hv_agx_retained_unmap(&f.core, 1, h, va, IPA, PAGE) == 0);
        assert(hv_agx_retained_verify_absent(&f.core, 1, h, va, IPA, PAGE) == 0);
        assert(hv_agx_retained_verify_absent(&f.core, 0, h, va, IPA, PAGE) ==
            HV_AGX_RETAINED_STATE);
        assert(hv_agx_retained_verify_absent(&f.core, 2, h, va, IPA, PAGE) ==
            HV_AGX_RETAINED_STATE);
        assert(hv_agx_retained_verify_absent(&f.core, 1, h + 1, va, IPA, PAGE) ==
            HV_AGX_RETAINED_OWNERSHIP);
        assert(hv_agx_retained_verify_absent(&f.core, 1, h, va + PAGE, IPA, PAGE) ==
            HV_AGX_RETAINED_OWNERSHIP);
        assert(hv_agx_retained_verify_absent(&f.core, 1, h, va, IPA + PAGE, PAGE) ==
            HV_AGX_RETAINED_OWNERSHIP);
        assert(hv_agx_retained_verify_absent(&f.core, 1, h, va, IPA, 2 * PAGE) ==
            HV_AGX_RETAINED_OWNERSHIP);
        assert(hv_agx_retained_verify_absent(&f.core, 1, h,
            0xffffff8000000000ULL, IPA, PAGE) == HV_AGX_RETAINED_OWNERSHIP);
        assert(hv_agx_retained_verify_absent(&f.core, 1, h,
            HV_AGX_RETAINED_SYSTEM_VA, IPA, PAGE) == HV_AGX_RETAINED_OWNERSHIP);
        assert(hv_agx_retained_map(&f.core, 1, va, IPA, PAGE, &newer) == 0);
        assert(newer != h);
        assert(hv_agx_retained_verify_absent(&f.core, 1, h, va, IPA, PAGE) ==
            HV_AGX_RETAINED_OWNERSHIP);
        assert(hv_agx_retained_unmap(&f.core, 1, newer, va, IPA, PAGE) == 0);
        assert(hv_agx_retained_verify_absent(&f.core, 1, h, va, IPA, PAGE) ==
            HV_AGX_RETAINED_OWNERSHIP);
        assert(hv_agx_retained_verify_absent(&f.core, 1, newer, va, IPA, PAGE) == 0);
        assert(hv_agx_retained_unmap(&f.core, 1, keep, VA + PAGE,
            IPA + PAGE, PAGE) == 0);
        assert(hv_agx_retained_verify_absent(&f.core, 1, newer, va, IPA, PAGE) ==
            HV_AGX_RETAINED_OWNERSHIP);
        assert(hv_agx_retained_verify_absent(&f.core, 1, keep, VA + PAGE,
            IPA + PAGE, PAGE) == 0);
        assert(hv_agx_retained_close(&f.core, 1, 1) == 0);
        assert(hv_agx_retained_verify_absent(&f.core, 1, keep, VA + PAGE,
            IPA + PAGE, PAGE) == HV_AGX_RETAINED_STATE);
        assert(prepare(&f, 2) == 0);
        assert(hv_agx_retained_activate(&f.core) == 0);
        assert(hv_agx_retained_verify_absent(&f.core, 2, keep, VA + PAGE,
            IPA + PAGE, PAGE) == HV_AGX_RETAINED_OWNERSHIP);
        assert(hv_agx_retained_close(&f.core, 2, 1) == 0);
        assert(!f.live);
    }
}

static APPLE_AGX_UAT_PAGE *fixture_page(struct fixture *f, unsigned long long pa)
{
    unsigned int i;
    for (i = 0; i < f->core.Inventory.PageCount; ++i)
        if (f->core.Pages[i].PhysicalAddress == pa)
            return &f->core.Pages[i];
    assert(0);
    return NULL;
}

/* A table alias has two parents, so shared pruning would leave a dangling
 * descriptor after freeing its child. Data aliases must remain allowed. */
static void duplicate_table_parent(void)
{
    struct fixture f;
    unsigned long long h;
    unsigned int live;
    start(&f);
    assert(hv_agx_retained_map(&f.core, 1, VA, IPA, PAGE, &h) == 0);
    fixture_page(&f, f.core.Roots.Ttbr0PhysicalAddress)->Entries[0] = f.retained[2];
    live = f.live;
    assert(hv_agx_retained_close(&f.core, 1, 1) == HV_AGX_RETAINED_TAINTED);
    assert(f.live == live && f.core.MappingCount == 1);
    discard_tainted_fixture(&f);
}

/* All entry points must reject malformed low and high intermediate entries
 * before shared UAT can interpret only their PA, mutate them, or prune pages. */
static void both_halves_malformed(void)
{
    unsigned int half, depth, corruption, operation;
    for (half = 0; half < 2; ++half)
    for (depth = 0; depth < 2; ++depth)
    for (corruption = 0; corruption < 5; ++corruption)
    for (operation = 0; operation < 5; ++operation) {
        struct fixture f;
        unsigned long long va = half ? 0x420000000ULL : VA;
        unsigned long long h, pa = 1, ignored, *entry, invalid;
        APPLE_AGX_UAT_PAGE *l1, *l2;
        unsigned int live, released;
        start(&f);
        assert(hv_agx_retained_map(&f.core, 1, va, IPA, PAGE, &h) == 0);
        entry = half ? &fixture_page(&f, f.core.Roots.Ttbr0PhysicalAddress)->Entries[0]
                     : &f.retained[2];
        l1 = fixture_page(&f, *entry & 0xffffffc000ULL);
        l2 = fixture_page(&f, l1->Entries[(va >> 25) & 2047] & 0xffffffc000ULL);
        if (depth)
            entry = &l1->Entries[(va >> 25) & 2047];
        if (operation == 4) {
            /* Simulate a surviving sibling PTE so UNMAP keeps this path and
             * absence verification must examine both intermediate levels. */
            l2->Entries[1] = 3;
            assert(hv_agx_retained_unmap(&f.core, 1, h, va, IPA, PAGE) == 0);
            assert(hv_agx_retained_verify_absent(&f.core, 1, h, va, IPA, PAGE) == 0);
        }
        invalid = corruption == 0 ? *entry & ~1ULL :
                  corruption == 1 ? *entry & ~2ULL :
                  corruption == 2 ? *entry | (1ULL << 55) :
                  corruption == 3 ? (ROOT_PA + PAGE) | 3ULL :
                                    f.core.Roots.Ttbr0PhysicalAddress | 3ULL;
        *entry = invalid;
        live = f.live;
        released = f.released;
        if (operation == 0)
            assert(hv_agx_retained_map(&f.core, 1, VA + 2 * PAGE, IPA,
                PAGE, &ignored) == HV_AGX_RETAINED_TAINTED);
        else if (operation == 1) {
            assert(hv_agx_retained_query(&f.core, 1, h, va, IPA, PAGE, &pa) ==
                HV_AGX_RETAINED_TAINTED);
            assert(pa == 0);
        } else if (operation == 2)
            assert(hv_agx_retained_unmap(&f.core, 1, h, va, IPA, PAGE) ==
                HV_AGX_RETAINED_TAINTED);
        else if (operation == 3)
            assert(hv_agx_retained_close(&f.core, 1, 1) == HV_AGX_RETAINED_TAINTED);
        else
            assert(hv_agx_retained_verify_absent(&f.core, 1, h, va, IPA, PAGE) ==
                HV_AGX_RETAINED_TAINTED);
        assert(f.live == live && f.released == released && *entry == invalid);
        check_prefix(&f);
        discard_tainted_fixture(&f);
    }
}

/* PTE absence means zero, never a resolver error caused by invalid leaf bits. */
static void absent_nonzero_leaf(void)
{
    unsigned int half, invalid;
    for (half = 0; half < 2; ++half)
    for (invalid = 0; invalid < 2; ++invalid) {
        struct fixture f;
        APPLE_AGX_UAT_PAGE *root, *l1, *l2;
        unsigned long long va = half ? 0x420000000ULL : VA, h, saved;
        start(&f);
        assert(hv_agx_retained_map(&f.core, 1, va, IPA, PAGE, &h) == 0);
        root = fixture_page(&f, half ? f.core.Roots.Ttbr0PhysicalAddress : ROOT_PA);
        l1 = fixture_page(&f, root->Entries[half ? 0 : 2] & 0xffffffc000ULL);
        l2 = fixture_page(&f, l1->Entries[(va >> 25) & 2047] & 0xffffffc000ULL);
        saved = l2->Entries[0];
        l2->Entries[1] = 3;
        assert(hv_agx_retained_unmap(&f.core, 1, h, va, IPA, PAGE) == 0);
        assert(hv_agx_retained_verify_absent(&f.core, 1, h, va, IPA, PAGE) == 0);
        l2->Entries[0] = invalid ? saved & ~3ULL : saved;
        assert(hv_agx_retained_verify_absent(&f.core, 1, h, va, IPA, PAGE) ==
            HV_AGX_RETAINED_OWNERSHIP);
        assert(l2->Entries[0] == (invalid ? saved & ~3ULL : saved));
        discard_tainted_fixture(&f);
    }
}

/* Failure of either low-alias table allocation must unwind only the new
 * descendants, preserve high/system mappings, and permit a later retry. */
static void low_alias_rollback(void)
{
    unsigned int fail;
    for (fail = 1; fail <= 2; ++fail) {
        struct fixture f;
        unsigned long long high, low, pa;
        unsigned int live;
        start(&f);
        assert(hv_agx_retained_map(&f.core, 1, VA, IPA, PAGE, &high) == 0);
        live = f.live;
        f.fail_at = f.allocated + fail;
        assert(hv_agx_retained_map(&f.core, 1, 0x420000000ULL, IPA, PAGE, &low) ==
            HV_AGX_RETAINED_ALLOCATION);
        assert(f.live == live && f.core.MappingCount == 1 && !low);
        assert(!fixture_page(&f, f.core.Roots.Ttbr0PhysicalAddress)->Entries[0]);
        assert(hv_agx_retained_query(&f.core, 1, high, VA, IPA, PAGE, &pa) == 0 && pa == PA);
        check_prefix(&f);
        f.fail_at = 0;
        assert(hv_agx_retained_map(&f.core, 1, 0x420000000ULL, IPA, PAGE, &low) == 0);
        assert(hv_agx_retained_close(&f.core, 1, 1) == 0 && !f.live);
    }
}

int main(void)
{
    production_inventory();
    absent_receipt();
    duplicate_table_parent();
    both_halves_malformed();
    absent_nonzero_leaf();
    low_alias_rollback();
    lifetime();
    rejection();
    rollback();
    tainted_tables();
    preparation_guards();
    invalid_intermediate_descriptors();
    return 0;
}
