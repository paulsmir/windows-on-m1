#include "../m1n1_windows/src/hv_agx_retained_root.h"
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#define PAGE 0x4000ULL
#define ROOT_PA 0x900000000ULL
#define VA 0xffffffa000000000ULL
#define IPA 0x800000000ULL
#define PA 0x810000000ULL

/* Independent literal native profile, including physical offset and guards. */
static const AGX_FW_IO_DESCRIPTOR expected[25] = {
    {0x204d00000, 0xffffffa068000000, 0x1c000, 0x1c000, 1},
    {0x20e100000, 0xffffffa068020000, 0x4000, 0x4000, 0},
    {0x23b104000, 0xffffffa068028000, 0x4000, 0x4000, 1},
    {0x204000000, 0xffffffa068030000, 0x20000, 0x20000, 1},
    {0}, {0}, {0},
    {0x23b2e8000, 0xffffffa068054000, 0x1000, 0x1000, 0},
    {0x23bc00000, 0xffffffa06805c000, 0x1000, 0x1000, 1},
    {0x204d80000, 0xffffffa068064000, 0x5000, 0x5000, 1},
    {0x204d61000, 0xffffffa068071000, 0x1000, 0x1000, 1},
    {0x200000000, 0xffffffa068078000, 0xd6400, 0xd6400, 1},
    {0},
    {0x23b738000, 0xffffffa068154000, 0x1000, 0x1000, 1},
    {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0}, {0},
};

struct fixture {
    struct hv_agx_retained_root core;
    unsigned long long retained[2048];
    unsigned int allocated, released, live, fail_at, syncs, last_release_sync;
    unsigned char corrupt_on_io_sync;
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
    /* No MMIO, Windows data, or retained firmware page belongs to the allocator. */
    assert(page->PhysicalAddress > 0x820000000ULL && page->PhysicalAddress < ROOT_PA);
    assert(page->Entries != f->retained && f->syncs > f->last_release_sync);
    f->last_release_sync = f->syncs;
    for (i = 0; i < f->core.Inventory.PageCount; ++i) {
        APPLE_AGX_UAT_PAGE *p = &f->core.Pages[i];
        if (p->Level + 1 != page->Level)
            continue;
        for (j = 0; j < (p->Level == 0 ? 8u : 2048u); ++j)
            assert((p->Entries[j] & 0xffffffc000ULL) != page->PhysicalAddress);
    }
    free(page->Entries);
    ++f->released;
    assert(f->live);
    --f->live;
}

static unsigned long long translate(void *opaque, unsigned long long ipa)
{
    (void)opaque;
    return ipa >= IPA && ipa < IPA + 256 * PAGE ? PA + ipa - IPA : 0;
}

static void sync_tables(void *opaque)
{
    struct fixture *f = opaque;
    unsigned int i;
    ++f->syncs;
    if (!f->corrupt_on_io_sync || !f->core.IoMappedSlots)
        return;
    for (i = 0; i < f->core.Inventory.PageCount; ++i) {
        APPLE_AGX_UAT_PAGE *p = &f->core.Pages[i];
        if (p->Level == 2 && p->Entries[0] == (0x204d00000ULL | 0xc0000000000447ULL)) {
            p->Entries[0] ^= 4;
            f->corrupt_on_io_sync = 0;
            return;
        }
    }
}

static void start_epoch(struct fixture *f, unsigned long long epoch)
{
    struct hv_agx_retained_ops ops = {f, allocate, release, translate, sync_tables};
    assert(hv_agx_retained_prepare(&f->core, ROOT_PA, f->retained, 0x40000,
        epoch, &ops) == 0);
    f->retained[0] = ROOT_PA + 0x4403;
    f->retained[1] = ROOT_PA + 0xc403;
    assert(hv_agx_retained_activate(&f->core) == 0);
}

static void prefix(struct fixture *f)
{
    assert(f->core.Roots.Ttbr1PhysicalAddress == ROOT_PA);
    assert(f->retained[0] == ROOT_PA + 0x4403);
    assert(f->retained[1] == ROOT_PA + 0xc403);
    assert(hv_agx_retained_prefix_unchanged(&f->core));
}

static unsigned long long window_va(unsigned int i)
{
    return VA + (i % 16) * 0x2000000ULL + (i / 16) * PAGE;
}

static void fill_windows(struct fixture *f, unsigned long long epoch)
{
    unsigned int i;
    unsigned long long handle;
    /* 199 high + exact low alias = the current 200-leaf inventory size. */
    for (i = 0; i < 199; ++i)
        assert(hv_agx_retained_map(&f->core, epoch, window_va(i), IPA + i * PAGE,
            PAGE, &handle) == 0);
    assert(hv_agx_retained_map(&f->core, epoch, 0x420000000ULL, IPA, PAGE, &handle) == 0);
}

static void check_windows(struct fixture *f, unsigned long long epoch)
{
    unsigned int i;
    unsigned long long pa;
    assert(f->core.MappingCount == 200);
    for (i = 0; i < 200; ++i) {
        const struct hv_agx_retained_mapping *m = &f->core.Mappings[i];
        assert(hv_agx_retained_query(&f->core, epoch, m->Handle, m->Va,
            m->Ipa, m->Length, &pa) == 0 && pa == m->Pa);
    }
}

/* Must use the real page-table image and literal expected descriptors, not
 * derive expected output from the production profile or resolver encoding. */
static void check_io(struct fixture *f)
{
    unsigned int i, leaves = 0;
    unsigned long long offset, pa, descriptor;
    for (i = 0; i < 25; ++i) {
        const AGX_FW_IO_DESCRIPTOR *d = &expected[i];
        unsigned long long length = (d->Size + (d->Phys & (PAGE - 1)) + PAGE - 1) & ~(PAGE - 1);
        if (!d->Size)
            continue;
        for (offset = 0; offset < length; offset += PAGE) {
            unsigned long long want = (d->Phys & ~(PAGE - 1)) + offset;
            assert(AppleAgxUatResolvePage(0, &f->core.Roots,
                (d->Virt & ~(PAGE - 1)) + offset, &f->core.Inventory,
                &pa, &descriptor) == AppleAgxUatResultOk);
            assert(pa == want && descriptor == (want | 0xc0000000000447ULL));
            ++leaves;
        }
        assert(AppleAgxUatResolvePage(0, &f->core.Roots,
            (d->Virt & ~(PAGE - 1)) + length, &f->core.Inventory,
            &pa, &descriptor) == AppleAgxUatResultNotMapped);
    }
    assert(leaves == 77);
}

static void production_lifetime(void)
{
    struct fixture f;
    unsigned int epoch, allocations, mappings;
    unsigned long long root0, system;
    memset(&f, 0, sizeof(f));
    for (epoch = 1; epoch <= 2; ++epoch) {
        start_epoch(&f, epoch);
        fill_windows(&f, epoch);
        root0 = f.core.Roots.Ttbr0PhysicalAddress;
        system = f.core.SystemPage.PhysicalAddress;
        assert(hv_agx_retained_io_prepare(&f.core, epoch) == 0);
        allocations = f.allocated;
        mappings = f.core.Inventory.MappingCount;
        assert(hv_agx_retained_io_prepare(&f.core, epoch) == 0);
        assert(f.allocated == allocations && f.core.Inventory.MappingCount == mappings);
        assert(mappings == 211 && f.core.Inventory.PageCount <= 24);
        assert(f.core.Roots.Ttbr0PhysicalAddress == root0);
        assert(f.core.SystemPage.PhysicalAddress == system);
        check_windows(&f, epoch);
        check_io(&f);
        prefix(&f);
        assert(hv_agx_retained_close(&f.core, epoch, 0) == HV_AGX_RETAINED_STATE);
        check_io(&f);
        assert(hv_agx_retained_close(&f.core, epoch, 1) == 0);
        assert(!f.live && !f.retained[2] && !f.core.Inventory.MappingCount);
        prefix(&f);
    }
}

static void start(struct fixture *f)
{
    memset(f, 0, sizeof(*f));
    start_epoch(f, 1);
}

static void zero_manifest(const AGX_FW_IO_MANIFEST *m)
{
    const unsigned char *bytes = (const unsigned char *)m;
    unsigned int i;
    for (i = 0; i < sizeof(*m); ++i)
        assert(bytes[i] == 0);
}

/* Fixed metadata is readable only after complete verified publication. Failed
 * reads erase stale output; guest-supplied physical metadata never admits IO. */
static void manifest_and_foreign_requests(void)
{
    struct fixture f;
    AGX_FW_IO_MANIFEST manifest, snapshot;
    unsigned long long ignored, pa;
    unsigned int i;
    memset(&f, 0, sizeof(f));
    memset(&manifest, 0xa5, sizeof(manifest));
    assert(hv_agx_retained_io_manifest(&f.core, 1, &manifest) == HV_AGX_RETAINED_STATE);
    zero_manifest(&manifest);
    start_epoch(&f, 1);
    assert(hv_agx_retained_io_manifest(&f.core, 1, &manifest) == HV_AGX_RETAINED_STATE);
    zero_manifest(&manifest);
    assert(hv_agx_retained_io_prepare(&f.core, 0) == HV_AGX_RETAINED_STATE);
    assert(hv_agx_retained_io_prepare(&f.core, 2) == HV_AGX_RETAINED_STATE);
    assert(hv_agx_retained_io_prepare(&f.core, 1) == 0);
    assert(hv_agx_retained_io_manifest(&f.core, 1, &manifest) == 0);
    assert(manifest.Magic == 0x4f495247 && manifest.Version == 1);
    assert(manifest.Bytes == 864 && manifest.Chip == 0x8103);
    assert(manifest.Epoch == 1 && manifest.Root == ROOT_PA);
    assert(manifest.Ready == 1 && manifest.Count == 25);
    assert(!manifest.Reserved[0] && !manifest.Reserved[1] && !manifest.Reserved[2]);
    assert(memcmp(manifest.Records, expected, sizeof(expected)) == 0);
    snapshot = manifest;
    for (i = 0; i < 25; ++i) {
        if (!expected[i].Size)
            continue;
        assert(hv_agx_retained_map(&f.core, 1, expected[i].Virt, IPA, PAGE, &ignored) ==
            HV_AGX_RETAINED_RANGE);
        assert(hv_agx_retained_map(&f.core, 1, VA, expected[i].Phys, PAGE, &ignored) ==
            HV_AGX_RETAINED_RANGE);
        assert(hv_agx_retained_query(&f.core, 1, i + 1, expected[i].Virt,
            expected[i].Phys, PAGE, &pa) == HV_AGX_RETAINED_OWNERSHIP);
        assert(hv_agx_retained_unmap(&f.core, 1, i + 1, expected[i].Virt,
            expected[i].Phys, PAGE) == HV_AGX_RETAINED_OWNERSHIP);
        assert(hv_agx_retained_verify_absent(&f.core, 1, i + 1, expected[i].Virt,
            expected[i].Phys, PAGE) == HV_AGX_RETAINED_OWNERSHIP);
    }
    assert(!f.core.MappingCount);
    assert(hv_agx_retained_io_manifest(&f.core, 1, &manifest) == 0);
    assert(memcmp(&manifest, &snapshot, sizeof(manifest)) == 0);
    assert(hv_agx_retained_io_manifest(&f.core, 2, &manifest) == HV_AGX_RETAINED_STATE);
    zero_manifest(&manifest);
    assert(hv_agx_retained_close(&f.core, 1, 1) == 0);
    assert(hv_agx_retained_io_manifest(&f.core, 1, &manifest) == HV_AGX_RETAINED_STATE);
    zero_manifest(&manifest);
    start_epoch(&f, 2);
    assert(hv_agx_retained_io_manifest(&f.core, 2, &manifest) == HV_AGX_RETAINED_STATE);
    zero_manifest(&manifest);
    assert(hv_agx_retained_io_prepare(&f.core, 2) == 0);
    assert(hv_agx_retained_io_manifest(&f.core, 1, &manifest) == HV_AGX_RETAINED_STATE);
    zero_manifest(&manifest);
    assert(hv_agx_retained_io_manifest(&f.core, 2, &manifest) == 0 && manifest.Epoch == 2);
    assert(hv_agx_retained_close(&f.core, 2, 1) == 0 && !f.live);
    assert(hv_agx_retained_io_prepare(NULL, 1) == HV_AGX_RETAINED_INVALID);
    assert(hv_agx_retained_io_manifest(NULL, 1, &manifest) == HV_AGX_RETAINED_INVALID);
    zero_manifest(&manifest);
    assert(hv_agx_retained_io_manifest(&f.core, 1, NULL) == HV_AGX_RETAINED_INVALID);
}

/* IO ranges must not consume any of the 256 promised Windows lease records. */
static void total_capacity(void)
{
    struct fixture f;
    unsigned int i;
    unsigned long long handle;
    start(&f);
    fill_windows(&f, 1);
    assert(hv_agx_retained_io_prepare(&f.core, 1) == 0);
    for (i = 199; i < 255; ++i)
        assert(hv_agx_retained_map(&f.core, 1, window_va(i), IPA + i * PAGE,
            PAGE, &handle) == 0);
    assert(f.core.MappingCount == 256 && f.core.Inventory.MappingCount == 267);
    assert(hv_agx_retained_map(&f.core, 1, window_va(255), IPA + 255 * PAGE,
        PAGE, &handle) == HV_AGX_RETAINED_ALLOCATION);
    check_io(&f);
    assert(hv_agx_retained_close(&f.core, 1, 1) == 0 && !f.live);
}

/* The single new IO L2 allocation may fail; every later range may also hit
 * inventory exhaustion. Earlier successful mappings remain owner obligations
 * until stopped CLOSE, and must not become a ready manifest or an active retry. */
static void partial_setup(void)
{
    unsigned int failure;
    for (failure = 0; failure <= 10; ++failure) {
        struct fixture f;
        AGX_FW_IO_MANIFEST manifest;
        unsigned int live, released, capacity, mapped, slot, seen;
        unsigned long long pa, descriptor;
        start(&f);
        fill_windows(&f, 1);
        live = f.live;
        released = f.released;
        capacity = f.core.Inventory.MappingCapacity;
        if (failure == 10)
            f.fail_at = f.allocated + 1;
        else
            f.core.Inventory.MappingCapacity = f.core.Inventory.MappingCount + failure;
        assert(hv_agx_retained_io_prepare(&f.core, 1) == HV_AGX_RETAINED_ALLOCATION);
        mapped = failure == 10 ? 0 : failure;
        assert(f.core.Inventory.MappingCount == 201 + mapped);
        assert(f.live == live + (mapped ? 1u : 0u) && f.released == released);
        assert(hv_agx_retained_io_manifest(&f.core, 1, &manifest) == HV_AGX_RETAINED_STATE);
        zero_manifest(&manifest);
        assert(hv_agx_retained_io_prepare(&f.core, 1) == HV_AGX_RETAINED_STATE);
        assert(hv_agx_retained_close(&f.core, 1, 0) == HV_AGX_RETAINED_STATE);
        assert(f.core.Inventory.MappingCount == 201 + mapped && f.released == released);
        seen = 0;
        for (slot = 0; slot < 25; ++slot) {
            if (!expected[slot].Size)
                continue;
            assert(AppleAgxUatResolvePage(0, &f.core.Roots,
                expected[slot].Virt & ~(PAGE - 1), &f.core.Inventory, &pa, &descriptor) ==
                (seen < mapped ? AppleAgxUatResultOk : AppleAgxUatResultNotMapped));
            if (seen < mapped)
                assert(pa == (expected[slot].Phys & ~(PAGE - 1)));
            ++seen;
        }
        f.core.Inventory.MappingCapacity = capacity;
        f.fail_at = 0;
        check_windows(&f, 1);
        prefix(&f);
        assert(hv_agx_retained_close(&f.core, 1, 1) == 0 && !f.live);
        start_epoch(&f, 2);
        assert(hv_agx_retained_io_prepare(&f.core, 2) == 0);
        check_io(&f);
        assert(hv_agx_retained_close(&f.core, 2, 1) == 0 && !f.live);
    }
}

static APPLE_AGX_UAT_PAGE *page_at(struct fixture *f, unsigned long long pa)
{
    unsigned int i;
    for (i = 0; i < f->core.Inventory.PageCount; ++i)
        if (f->core.Pages[i].PhysicalAddress == pa)
            return &f->core.Pages[i];
    assert(0);
    return NULL;
}

static void discard_fixture(struct fixture *f)
{
    unsigned int i;
    for (i = 0; i < f->core.Inventory.PageCount; ++i)
        if (f->core.Pages[i].Entries != f->retained)
            free(f->core.Pages[i].Entries);
    if (f->core.SystemPage.Entries)
        free(f->core.SystemPage.Entries);
}

static void require_tainted(struct fixture *f, unsigned int operation)
{
    AGX_FW_IO_MANIFEST manifest;
    unsigned int live = f->live, released = f->released;
    unsigned int mappings = f->core.Inventory.MappingCount;
    if (operation == 0)
        assert(hv_agx_retained_io_prepare(&f->core, 1) == HV_AGX_RETAINED_TAINTED);
    else if (operation == 1) {
        memset(&manifest, 0xa5, sizeof(manifest));
        assert(hv_agx_retained_io_manifest(&f->core, 1, &manifest) == HV_AGX_RETAINED_TAINTED);
        zero_manifest(&manifest);
    } else
        assert(hv_agx_retained_close(&f->core, 1, 1) == HV_AGX_RETAINED_TAINTED);
    assert(f->live == live && f->released == released);
    assert(f->core.Inventory.MappingCount == mappings && f->core.MappingCount == 1);
    prefix(f);
}

/* Missing validation of any IO leaf, native protection bit, or guard would
 * publish or retire a corrupt mapping. Exercise every one of the 77 leaves. */
static void corrupted_io(void)
{
    unsigned int operation, slot, n;
    for (operation = 0; operation < 3; ++operation)
    for (slot = 0; slot < 25; ++slot) {
        unsigned long long length = (expected[slot].Size +
            (expected[slot].Phys & (PAGE - 1)) + PAGE - 1) & ~(PAGE - 1);
        if (!expected[slot].Size)
            continue;
        for (n = 0; n <= length / PAGE; ++n) {
            struct fixture f;
            APPLE_AGX_UAT_PAGE *l1, *l2;
            unsigned long long h, va = (expected[slot].Virt & ~(PAGE - 1)) + n * PAGE;
            unsigned long long *entry, corrupt;
            start(&f);
            assert(hv_agx_retained_map(&f.core, 1, VA, IPA, PAGE, &h) == 0);
            assert(hv_agx_retained_io_prepare(&f.core, 1) == 0);
            l1 = page_at(&f, f.retained[2] & 0xffffffc000ULL);
            l2 = page_at(&f, l1->Entries[(va >> 25) & 2047] & 0xffffffc000ULL);
            entry = &l2->Entries[(va >> 14) & 2047];
            if (n == length / PAGE)
                corrupt = 3; /* Guard must be zero, including invalid PA leaves. */
            else if (n % 3 == 0)
                corrupt = *entry ^ 4; /* Device -> cached is forbidden. */
            else if (n % 3 == 1)
                corrupt = *entry & ~1ULL;
            else
                corrupt = *entry ^ PAGE;
            *entry = corrupt;
            require_tainted(&f, operation);
            assert(*entry == corrupt);
            discard_fixture(&f);
        }
    }
    for (operation = 0; operation < 3; ++operation)
    for (n = 0; n < 4; ++n) {
        struct fixture f;
        APPLE_AGX_UAT_PAGE *l1;
        unsigned long long h, *entry, corrupt;
        start(&f);
        assert(hv_agx_retained_map(&f.core, 1, VA, IPA, PAGE, &h) == 0);
        assert(hv_agx_retained_io_prepare(&f.core, 1) == 0);
        l1 = page_at(&f, f.retained[2] & 0xffffffc000ULL);
        entry = &l1->Entries[52];
        corrupt = n == 0 ? *entry & ~1ULL : n == 1 ? *entry | (1ULL << 55) :
                  n == 2 ? (ROOT_PA + PAGE) | 3ULL : f.core.Roots.Ttbr0PhysicalAddress | 3ULL;
        *entry = corrupt;
        require_tainted(&f, operation);
        assert(*entry == corrupt);
        discard_fixture(&f);
    }
}

/* Initial publication must validate real PTEs too; a setup-time corruption
 * cannot become Ready even when every shared map call reported success. */
static void corrupt_before_publication(void)
{
    struct fixture f;
    AGX_FW_IO_MANIFEST manifest;
    unsigned long long h;
    start(&f);
    assert(hv_agx_retained_map(&f.core, 1, VA, IPA, PAGE, &h) == 0);
    f.corrupt_on_io_sync = 1;
    assert(hv_agx_retained_io_prepare(&f.core, 1) == HV_AGX_RETAINED_TAINTED);
    assert(!f.core.IoReady && !f.corrupt_on_io_sync);
    assert(hv_agx_retained_io_manifest(&f.core, 1, &manifest) == HV_AGX_RETAINED_TAINTED);
    zero_manifest(&manifest);
    require_tainted(&f, 2);
    discard_fixture(&f);
}

/* A partial owner still must validate its mapped leaf before stopped cleanup. */
static void corrupt_partial_close(void)
{
    struct fixture f;
    APPLE_AGX_UAT_PAGE *l1, *l2;
    unsigned long long h;
    unsigned int capacity;
    start(&f);
    assert(hv_agx_retained_map(&f.core, 1, VA, IPA, PAGE, &h) == 0);
    capacity = f.core.Inventory.MappingCapacity;
    f.core.Inventory.MappingCapacity = f.core.Inventory.MappingCount + 1;
    assert(hv_agx_retained_io_prepare(&f.core, 1) == HV_AGX_RETAINED_ALLOCATION);
    f.core.Inventory.MappingCapacity = capacity;
    assert(!f.core.IoReady && f.core.IoMappedSlots);
    l1 = page_at(&f, f.retained[2] & 0xffffffc000ULL);
    l2 = page_at(&f, l1->Entries[52] & 0xffffffc000ULL);
    l2->Entries[0] ^= PAGE;
    require_tainted(&f, 2);
    discard_fixture(&f);
}

int main(void)
{
    production_lifetime();
    manifest_and_foreign_requests();
    total_capacity();
    partial_setup();
    corrupted_io();
    corrupt_before_publication();
    corrupt_partial_close();
    return 0;
}
