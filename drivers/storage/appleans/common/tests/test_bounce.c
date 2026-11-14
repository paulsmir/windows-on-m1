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
    const uint64_t base = 0x9f0000000ULL;
    for (uint32_t bytes = 4096; bytes <= 131072; bytes += 4096) {
        struct ans_logical_prps p; memset(&p, 0xff, sizeof p);
        assert(ans_bounce_prps(base, bytes, &p) && p.count == bytes / 4096 && p.bytes == bytes);
        for (unsigned i = 0; i < ANS_MAX_DATA_PAGES; i++)
            assert(p.page[i] == (i < p.count ? base + (uint64_t)i * 4096 : 0));
        /* The request builder takes it as an ordinary aligned transfer. */
        uint8_t cdb[10] = {0x2a}, list[4096]; struct ans_broker_request r;
        struct ans_namespace ns = {.blocks = 1u << 20, .block_size = 4096, .nsid = 1, .ready = 1};
        cdb[5] = 9; cdb[7] = (uint8_t)((bytes / 4096) >> 8); cdb[8] = (uint8_t)(bytes / 4096);
        struct ans_scsi_plan plan = ans_scsi_prepare(&ns, cdb, 10, bytes, NULL);
        assert(plan.action == ANS_SCSI_SUBMIT && ans_io_request(&plan, &p, 5, 0x300000, list, 1, &r));
        uint64_t prp1 = 0; memcpy(&prp1, r.sqe + 24, 8); assert(prp1 == base);
    }
    {
        struct ans_logical_prps p; memset(&p, 0xff, sizeof p);
        assert(!ans_bounce_prps(base, 4095, &p) && !p.count && !p.page[0]);
        assert(!ans_bounce_prps(base, 135168, &p) && !ans_bounce_prps(0, 4096, &p) && !ans_bounce_prps(base, 4096, NULL));
        memset(&p, 0xff, sizeof p);
        assert(!ans_bounce_prps(base + 8, 16384, &p) && !p.count && !p.page[0] && !p.page[3]);
        assert(!ans_bounce_prps(~0ULL & ~4095ULL, 4096, &p)); /* would wrap */
    }

    /* Pool: exclusive, bounded, no leak. */
    struct ans_bounce_pool pool = {0};
    assert(ans_bounce_acquire(NULL, 0) == -1 && !ans_bounce_release(NULL, 0));
    /* No pool: nothing can be claimed or released, and that is not counted as exhaustion of a
     * pool the caller was promised (the caller never asks: count == 0 means no bounce). */
    assert(ans_bounce_acquire(&pool, 1) == -1 && !pool.claimed && !ans_bounce_release(&pool, 0));
    assert(ans_bounce_wait(&pool) == 0 && pool.direct_exhausted == 1 && !pool.busy); /* nobody to wait for */
    /* Partial pool: only the buffers that exist are handed out. */
    for (unsigned n = 1; n < ANS_BOUNCE_COUNT; n++) {
        struct ans_bounce_pool part = {0}; part.count = n;
        for (unsigned i = 0; i < n; i++) assert(ans_bounce_acquire(&part, 1) == (int)i);
        assert(ans_bounce_acquire(&part, 1) == -1 && ans_bounce_wait(&part) == 1 && part.busy == 1 && part.high_water == n);
        assert(!ans_bounce_release(&part, (int)n) && ans_bounce_release(&part, (int)n - 1));
        assert(ans_bounce_acquire(&part, 0) == (int)n - 1);
    }
    {
        struct ans_bounce_pool f = {0};
        ans_bounce_build_failed(&f, ANS_BOUNCE_STEP_TRANSLATE, 3, 0x8a0110000ULL);
        ans_bounce_build_failed(&f, ANS_BOUNCE_STEP_ALLOCATE, 4, 1);
        ans_bounce_build_failed(NULL, 1, 1, 1);
        assert(f.failures == 2 && f.fail_step == ANS_BOUNCE_STEP_TRANSLATE && f.fail_index == 3 &&
               f.fail_address == 0x8a0110000ULL);
    }
    pool = (struct ans_bounce_pool){0}; pool.count = ANS_BOUNCE_COUNT; pool.busy = 0;
    for (unsigned i = 0; i < ANS_BOUNCE_COUNT; i++) assert(ans_bounce_acquire(&pool, i & 1) == (int)i);
    assert(pool.high_water == ANS_BOUNCE_COUNT && pool.bounced_writes == 8 && pool.bounced_reads == 8 && !pool.busy);
    assert(ans_bounce_acquire(&pool, 1) == -1 && ans_bounce_wait(&pool) == 1 && pool.busy == 1 && pool.bounced_writes == 8);
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
            if (i < 0) { assert(held == ANS_BOUNCE_COUNT); }
            else { assert((unsigned)i < ANS_BOUNCE_COUNT && !owned[i]); owned[i] = 1; held++; }
        } else {
            int i = rand() % (int)ANS_BOUNCE_COUNT;
            assert(ans_bounce_release(&pool, i) == owned[i]);
            if (owned[i]) { owned[i] = 0; held--; }
        }
        ans_u32 bits = 0; for (unsigned i = 0; i < ANS_BOUNCE_COUNT; i++) if (owned[i]) bits |= 1u << i;
        assert(pool.claimed == bits && pool.busy == busy0 && pool.high_water >= held);
    }
    /* Exhaustion: BUSY at most ANS_BOUNCE_BUSY_LIMIT times between two releases, then the request
     * goes directly; a release or a successful claim starts the count again. The limit lives in
     * the pool, so it does not depend on what the port driver keeps of a resubmitted request. */
    {
        struct ans_bounce_pool q = {0}; q.count = 2;
        assert(ans_bounce_acquire(&q, 1) == 0 && ans_bounce_acquire(&q, 1) == 1);
        for (unsigned i = 0; i < ANS_BOUNCE_BUSY_LIMIT; i++) assert(ans_bounce_acquire(&q, 1) == -1 && ans_bounce_wait(&q) == 1);
        assert(q.busy == ANS_BOUNCE_BUSY_LIMIT && !q.direct_exhausted);
        assert(ans_bounce_wait(&q) == 0 && ans_bounce_wait(&q) == 0 && q.direct_exhausted == 2 && q.busy == ANS_BOUNCE_BUSY_LIMIT);
        assert(ans_bounce_release(&q, 0) && ans_bounce_wait(&q) == 1 && q.busy == ANS_BOUNCE_BUSY_LIMIT + 1);
        assert(ans_bounce_acquire(&q, 0) == 0 && q.busy_streak == 0);
        assert(ans_bounce_wait(NULL) == 0);
        q.direct_exhausted = 0; q.busy = 0; q.direct_unmapped = 1; q.direct_mismatch = 14;
        for (unsigned i = 0; i < ANS_BOUNCE_BUSY_LIMIT + 1; i++) (void)ans_bounce_wait(&q);
        assert(q.busy == ANS_BOUNCE_BUSY_LIMIT && q.direct_exhausted == 1);
        ans_bounce_copy_timed(&q, 5); ans_bounce_copy_timed(&q, 9); ans_bounce_copy_timed(&q, 2);
        assert(q.copies == 3 && q.copy_ticks == 16 && q.copy_ticks_max == 9);
        /* Receipt layout. */
        uint8_t raw[ANS_BOUNCE_IOCTL_TOTAL + 2], *b = raw + 1; memset(raw, 0xee, sizeof raw); memset(b, 0, ANS_BOUNCE_IOCTL_TOTAL);
        q.claimed = 0x8001; q.high_water = 7; q.bounced_reads = 11; q.bounced_writes = 12; q.busy = 13; q.enabled = 7;
        q.ticks_per_second = 24000000; q.count = 5;
        ans_bounce_build_failed(&q, ANS_BOUNCE_STEP_PHYSICAL, 6, 17); ans_bounce_build_failed(&q, 1, 7, 0);
        b[0] = 28; memcpy(b + 4, "APANS001", 8); b[12] = 10; b[16] = 0x0c; b[17] = 0x60; b[18] = 0x1b;
        b[24] = (uint8_t)ANS_BOUNCE_IOCTL_PAYLOAD; b[28] = ANS_BOUNCE_IOCTL_VERSION; b[32] = (uint8_t)ANS_BOUNCE_IOCTL_PAYLOAD;
        assert(ANS_BOUNCE_IOCTL_TOTAL == 164 && ans_bounce_ioctl_validate(b, 164) && !ans_bounce_ioctl_validate(b, 140));
        b[28] = 2; assert(!ans_bounce_ioctl_validate(b, 164)); b[28] = ANS_BOUNCE_IOCTL_VERSION;
        b[16] = 8; assert(!ans_bounce_ioctl_reply(b, 164, &q)); b[16] = 0x0c;
        assert(ans_bounce_ioctl_reply(b, 164, &q) && raw[0] == 0xee && raw[sizeof raw - 1] == 0xee);
        uint64_t v; uint32_t w; const uint8_t *p = b + 28;
        memcpy(&w, p + 8, 4); assert(w == 5); memcpy(&w, p + 28, 4); assert(w == 16); memcpy(&w, p + 12, 4); assert(w == 131072);
        memcpy(&w, p + 16, 4); assert(w == 4096); memcpy(&w, p + 20, 4); assert(w == 0x8001);
        memcpy(&w, p + 24, 4); assert(w == 7);
        memcpy(&v, p + 32, 8); assert(v == 11); memcpy(&v, p + 40, 8); assert(v == 12);
        memcpy(&v, p + 48, 8); assert(v == 13); memcpy(&v, p + 56, 8); assert(v == 1);
        memcpy(&v, p + 64, 8); assert(v == 1); memcpy(&v, p + 72, 8); assert(v == 14);
        memcpy(&v, p + 80, 8); assert(v == 3); memcpy(&v, p + 88, 8); assert(v == 16);
        memcpy(&v, p + 96, 8); assert(v == 9); memcpy(&v, p + 104, 8); assert(v == 24000000);
        memcpy(&w, p + 112, 4); assert(w == ANS_BOUNCE_STEP_PHYSICAL); memcpy(&w, p + 116, 4); assert(w == 6);
        memcpy(&v, p + 120, 8); assert(v == 17); memcpy(&w, p + 128, 4); assert(w == 2);
        memcpy(&w, p + 132, 4); assert(w == 1);
    }
    puts("ANS bounce pool and PRP tests PASS");
    return 0;
}
