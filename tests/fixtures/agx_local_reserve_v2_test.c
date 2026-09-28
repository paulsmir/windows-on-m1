#include <assert.h>
#include <stdint.h>
#include "hv_agx_local_reserve.h"
struct mapping { uint64_t hole, alias, delta, calls; };
static bool translate(void *ctx, uint64_t ipa, uint64_t *pa) {
    struct mapping *m = ctx;
    ++m->calls;
    if (ipa == m->hole) return false;
    *pa = ipa + m->delta + (ipa == m->alias ? 0x4000 : 0);
    return true;
}
int main(void) {
    const uint64_t starts[] = {0x8e0000000ULL, 0x880010000ULL};
    struct hv_agx_local_receipt r = {0};
    struct hv_agx_local_failure f = {0};
    assert(HV_AGX_LOCAL_BYTES == 0x40000000ULL);
    assert(HV_AGX_LOCAL_ABI_VERSION == 2);
    for (unsigned i = 0; i < 2; ++i) {
        uint64_t b = starts[i];
        struct mapping m = {0};
        assert(hv_agx_local_select_detailed(b, 0x40000000, 0, 0, translate, &m, &r, &f));
        assert(r.guest_ipa == b && r.host_pa == b && r.bytes == 0x40000000);
        assert(m.calls == 2 * (0x40000000 / 0x1000));
        const uint64_t offsets[] = {0, 0x1000, 0x1fff, 0x3ffff000, 0x3fffffff};
        for (unsigned j = 0; j < sizeof(offsets)/sizeof(offsets[0]); ++j) {
            m = (struct mapping){.hole = b + offsets[j]};
            assert(!hv_agx_local_select_detailed(b, 0x80000000, 0, 0, translate, &m, &r, &f));
            assert(r.bytes == 0 && f.reason == HV_AGX_LOCAL_UNMAPPED);
            m = (struct mapping){.alias = b + offsets[j]};
            assert(!hv_agx_local_validate(b, 0, 0, translate, &m, &r));
            assert(r.bytes == 0);
        }
        m = (struct mapping){.delta = 0x40000000};
        assert(!hv_agx_local_validate(b, 0, 0, translate, &m, &r));
        m = (struct mapping){0};
        struct hv_agx_local_range excluded = {b + 0x1000, 1};
        assert(!hv_agx_local_validate(b, &excluded, 1, translate, &m, &r));
        assert(!hv_agx_local_validate(b + 0x4000, 0, 0, translate, &m, &r));
        assert(!hv_agx_local_select(b, 0x3fffffff, 0, 0, translate, &m, &r));
        assert(!hv_agx_local_select(UINT64_MAX-0xffff, 0x40000000, 0, 0, translate, &m, &r));
    }
    assert(hv_agx_local_carveout_allows(0x880000000ULL, 0x800000000ULL, 0x100000000ULL, true));
    assert(!hv_agx_local_carveout_allows(0x880000000ULL, 0x890000000ULL, 0x4000, false));
    assert(hv_agx_local_carveout_allows(0x880000000ULL, 0x890000000ULL + 0x40000000, 0x4000, false));
    assert(!hv_agx_local_carveout_allows(0x880000000ULL, 0x800000000ULL, 0x80000000, true));
    assert(!hv_agx_local_carveout_allows(0x880000000ULL, UINT64_MAX-1, 0x4000, false));
    return 0;
}
