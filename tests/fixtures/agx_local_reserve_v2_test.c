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
    /* M2-f2c-2: J313 selects 384 MiB (segment 2 = 312 MiB; EXP1252-EXP1254-B
     * measured a 172 MiB peak). The checks below follow the selected size. */
    const uint64_t S = HV_AGX_LOCAL_BYTES;
    const uint64_t SEG = S - HV_AGX_LOCAL_PRIVATE_BYTES - HV_AGX_LOCAL_BACKEND_BYTES;
    assert(S == 0x18000000ULL && SEG == 0x13800000ULL);
    assert(HV_AGX_LOCAL_ABI_VERSION == 3);
    for (unsigned i = 0; i < 2; ++i) {
        uint64_t b = starts[i];
        struct mapping m = {0};
        assert(hv_agx_local_select_detailed(b, S, 0, 0, translate, &m, &r, &f));
        assert(r.guest_ipa == b && r.host_pa == b && r.bytes == S);
        assert(m.calls == 2 * (S / 0x1000));
        const uint64_t offsets[] = {0, 0x1000, 0x1fff, S - 0x1000, S - 1};
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
        assert(!hv_agx_local_select(b, S - 1, 0, 0, translate, &m, &r));
        assert(!hv_agx_local_select(UINT64_MAX-0xffff, S, 0, 0, translate, &m, &r));
    }
    assert(hv_agx_local_carveout_allows(0x880000000ULL, 0x800000000ULL, 0x100000000ULL, true));
    assert(!hv_agx_local_carveout_allows(0x880000000ULL, 0x890000000ULL, 0x4000, false));
    assert(hv_agx_local_carveout_allows(0x880000000ULL, 0x880000000ULL + S, 0x4000, false));
    assert(!hv_agx_local_carveout_allows(0x880000000ULL, 0x880000000ULL + S - 0x4000, 0x4000, false));
    assert(!hv_agx_local_carveout_allows(0x880000000ULL, 0x800000000ULL, 0x80000000, true));
    assert(!hv_agx_local_carveout_allows(0x880000000ULL, UINT64_MAX-1, 0x4000, false));
    r = (struct hv_agx_local_receipt){0x8e0000000ULL,0x8e0000000ULL,S};
    assert(hv_agx_local_scanout_allows(&r,r.host_pa,0x3800000));
    /* M2-f2c: scanout stays inside segment 2 (reserve - private - backend). */
    assert(hv_agx_local_scanout_allows(&r,r.host_pa+SEG-0x4000,0x4000));
    assert(!hv_agx_local_scanout_allows(&r,r.host_pa+SEG,0x4000));
    assert(!hv_agx_local_scanout_allows(&r,r.host_pa+SEG-0x4000,0x8000));
    assert(!hv_agx_local_scanout_allows(&r,UINT64_MAX-1,0x4000));
    r.bytes=0; assert(!hv_agx_local_scanout_allows(&r,r.host_pa,0x4000));
    return 0;
}
