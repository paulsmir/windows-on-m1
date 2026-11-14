/* SPDX-License-Identifier: MIT */
/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * (1) A data address the controller cannot take must never appear in a PRP: for every
 * dword-aligned first address and length, either the address is page-aligned or the PRP set names
 * only pages of a driver-owned bounce buffer. (2) Two requests must never share a bounce buffer and
 * a buffer must never leak: a shared buffer is silent data corruption, a leak ends in permanent
 * BUSY. (3) The request builder must accept the bounce PRP set unchanged. */
#include "../ans_bounce.h"
#include "../../windows/request.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
int main(void)
{
    /* Rule: only page-aligned first addresses go to the controller as they are. */
    assert(ANS_DATA_ALIGNMENT == 4096 && ANS_BOUNCE_PAGES == 32 && ANS_BOUNCE_BYTES == 131072);
    for (uint64_t off = 0; off < 8192; off += 4)
        assert(ans_bounce_needed(0x812340000ULL + off) == ((off & 4095) != 0));
    assert(ans_bounce_needed(0x1e87e360020ULL) && ans_bounce_needed(0x1fb10087ee8ULL)); /* EXP2439/2440 stalls */
    assert(ans_bounce_needed(0x21f3b9a0fc0ULL) && !ans_bounce_needed(0x23deaa60000ULL));
    assert(ans_bounce_size_ok(4096) && ans_bounce_size_ok(131072) && !ans_bounce_size_ok(0));
    assert(!ans_bounce_size_ok(131072 + 4096) && !ans_bounce_size_ok(4095) && !ans_bounce_size_ok(6144));

    /* PRP set of a bounce buffer: whole pages of that buffer, nothing else. */
    uint64_t pa[ANS_BOUNCE_PAGES];
    for (unsigned i = 0; i < ANS_BOUNCE_PAGES; i++) pa[i] = 0x9f0000000ULL + (uint64_t)(31 - i) * 0x10000; /* scattered */
    for (uint32_t bytes = 4096; bytes <= 131072; bytes += 4096) {
        struct ans_logical_prps p; memset(&p, 0xff, sizeof p);
        assert(ans_bounce_prps(pa, bytes, &p) && p.count == bytes / 4096 && p.bytes == bytes);
        for (unsigned i = 0; i < ANS_MAX_DATA_PAGES; i++)
            assert(p.page[i] == (i < p.count ? pa[i] : 0) && !(p.page[i] & 4095));
        /* The request builder takes it as an ordinary aligned transfer. */
        uint8_t cdb[10] = {0x2a}, list[4096]; struct ans_broker_request r;
        struct ans_namespace ns = {.blocks = 1u << 20, .block_size = 4096, .nsid = 1, .ready = 1};
        cdb[5] = 9; cdb[7] = (uint8_t)((bytes / 4096) >> 8); cdb[8] = (uint8_t)(bytes / 4096);
        struct ans_scsi_plan plan = ans_scsi_prepare(&ns, cdb, 10, bytes, NULL);
        assert(plan.action == ANS_SCSI_SUBMIT && ans_io_request(&plan, &p, 5, 0x300000, list, 1, &r));
        uint64_t prp1 = 0; memcpy(&prp1, r.sqe + 24, 8); assert(prp1 == pa[0]);
    }
    {
        struct ans_logical_prps p; memset(&p, 0xff, sizeof p);
        assert(!ans_bounce_prps(pa, 4095, &p) && !p.count && !p.page[0]);
        assert(!ans_bounce_prps(pa, 135168, &p) && !ans_bounce_prps(NULL, 4096, &p) && !ans_bounce_prps(pa, 4096, NULL));
        uint64_t bad[ANS_BOUNCE_PAGES]; memcpy(bad, pa, sizeof bad); bad[3] += 8;
        assert(!ans_bounce_prps(bad, 16384, &p) && !p.count && !p.page[0] && !p.page[3]);
        bad[3] = 0; assert(!ans_bounce_prps(bad, 16384, &p));
        assert(ans_bounce_prps(bad, 12288, &p) && p.count == 3); /* pages beyond the transfer are not consulted */
    }

    /* Pool: exclusive, bounded, no leak. */
    struct ans_bounce_pool pool = {0};
    assert(ans_bounce_acquire(NULL, 0) == -1 && !ans_bounce_release(NULL, 0));
    for (unsigned i = 0; i < ANS_BOUNCE_COUNT; i++) assert(ans_bounce_acquire(&pool, i & 1) == (int)i);
    assert(pool.high_water == ANS_BOUNCE_COUNT && pool.bounced_writes == 8 && pool.bounced_reads == 8 && !pool.busy);
    assert(ans_bounce_acquire(&pool, 1) == -1 && pool.busy == 1 && pool.bounced_writes == 8);
    assert(ans_bounce_release(&pool, 5) && !ans_bounce_release(&pool, 5)); /* double release changes nothing */
    assert(!ans_bounce_release(&pool, -1) && !ans_bounce_release(&pool, (int)ANS_BOUNCE_COUNT));
    assert(ans_bounce_acquire(&pool, 1) == 5);
    for (unsigned i = 0; i < ANS_BOUNCE_COUNT; i++) assert(ans_bounce_release(&pool, (int)i));
    assert(!pool.claimed);
    /* Randomised against a reference set. */
    srand(11);
    int owned[ANS_BOUNCE_COUNT] = {0}; unsigned held = 0; uint64_t busy0 = pool.busy;
    for (unsigned step = 0; step < 200000; step++) {
        if (rand() % 3) {
            int i = ans_bounce_acquire(&pool, rand() & 1);
            if (i < 0) { assert(held == ANS_BOUNCE_COUNT); busy0++; }
            else { assert((unsigned)i < ANS_BOUNCE_COUNT && !owned[i]); owned[i] = 1; held++; }
        } else {
            int i = rand() % (int)ANS_BOUNCE_COUNT;
            assert(ans_bounce_release(&pool, i) == owned[i]);
            if (owned[i]) { owned[i] = 0; held--; }
        }
        ans_u32 bits = 0; for (unsigned i = 0; i < ANS_BOUNCE_COUNT; i++) if (owned[i]) bits |= 1u << i;
        assert(pool.claimed == bits && pool.busy == busy0 && pool.high_water >= held);
    }
    puts("ANS bounce pool and PRP tests PASS");
    return 0;
}
