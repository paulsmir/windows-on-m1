/* SPDX-License-Identifier: MIT */
/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * The namespace also holds other operating systems. These tests pin the two properties the
 * hardware qualification relies on: (1) no Dataset Management range is ever emitted outside
 * DEALLOCATE mode or outside the configured LBA fence, whatever Windows asks for; (2) the
 * receipt's extents contain every range Windows asked for, so an out-of-partition request
 * cannot go unnoticed. */
#include "../ans_trim.h"
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static struct ans_namespace ns = {.blocks = 1000000, .block_size = 4096, .nsid = 1,
    .ready = 1, .deallocate = 1};
static void put_be(uint8_t *p, uint64_t v, size_t n) { while (n) { p[--n] = (uint8_t)v; v >>= 8; } }
static uint64_t get_le(const uint8_t *p, size_t n)
{ uint64_t v = 0; while (n--) v = (v << 8) | p[n]; return v; }
static struct ans_scsi_plan unmap(struct ans_trim *t, const uint64_t *lba, const uint32_t *count,
    size_t n, uint8_t *dsm)
{
    static uint8_t list[8 + 256 * 16];
    uint8_t c[10] = {0x42};
    size_t bytes = 8 + n * 16;
    memset(list, 0, sizeof list);
    put_be(list, n * 16 + 6, 2); put_be(list + 2, n * 16, 2);
    for (size_t i = 0; i < n; i++) {
        put_be(list + 8 + i * 16, lba[i], 8);
        put_be(list + 8 + i * 16 + 8, count[i], 4);
    }
    put_be(c + 7, bytes, 2);
    struct ans_scsi_plan p = ans_scsi_prepare(&ns, c, 10, (ans_u32)bytes, NULL);
    assert(p.action == ANS_SCSI_SUBMIT);
    ans_scsi_unmap_resolve(&ns, &p, list, dsm);
    ans_trim_gate(t, &p, dsm);
    return p;
}
static int all_zero(const uint8_t *p, size_t n) { while (n--) if (*p++) return 0; return 1; }
static void config_tests(void)
{
    struct ans_trim_config c = ans_trim_config_make(0, 0, 0, 0);
    assert(c.mode == ANS_TRIM_OBSERVE && !c.fence_start && !c.fence_end);
    assert(ans_trim_config_make(1, 0, 0, 0).mode == ANS_TRIM_OFF);
    assert(ans_trim_config_make(1, 1, 0, 0).mode == ANS_TRIM_OBSERVE);
    assert(ans_trim_config_make(1, 3, 0, 0).mode == ANS_TRIM_OBSERVE);
    assert(ans_trim_config_make(1, 0xffffffffu, 10, 20).mode == ANS_TRIM_OBSERVE);
    /* DEALLOCATE needs a complete, non-empty fence. */
    assert(ans_trim_config_make(1, 2, 10, 20).mode == ANS_TRIM_OBSERVE);
    assert(ans_trim_config_make(3, 2, 10, 20).mode == ANS_TRIM_OBSERVE);
    assert(ans_trim_config_make(5, 2, 10, 20).mode == ANS_TRIM_OBSERVE);
    assert(ans_trim_config_make(7, 2, 20, 20).mode == ANS_TRIM_OBSERVE);
    assert(ans_trim_config_make(7, 2, 21, 20).mode == ANS_TRIM_OBSERVE);
    c = ans_trim_config_make(7, 2, 10, 20);
    assert(c.mode == ANS_TRIM_DEALLOCATE && c.fence_start == 10 && c.fence_end == 20);
    /* The value 2 without the mode bit is not a mode. */
    assert(ans_trim_config_make(6, 2, 10, 20).mode == ANS_TRIM_OBSERVE);
}
static void observe_tests(void)
{
    static uint8_t dsm[ANS_PRP_PAGE_SIZE];
    struct ans_trim t = {0};
    uint64_t lba[3] = {100, 5000, 999999}; uint32_t count[3] = {10, 0, 1};
    for (unsigned mode = 0; mode < 2; mode++) {
        memset(&t, 0, sizeof t);
        t.config = ans_trim_config_make(7, mode, 0, 1000000);
        struct ans_scsi_plan p = unmap(&t, lba, count, 3, dsm);
        assert(p.action == ANS_SCSI_COMPLETE && !p.nvme_opcode && !p.dsm_ranges);
        assert(p.direction == ANS_DMA_NONE && p.transfer_bytes == 8 + 3 * 16 && !p.metadata_length);
        assert(all_zero(dsm, sizeof dsm));
        assert(t.commands == 1 && t.accepted == 1 && t.observed == 1 && !t.sent_ranges);
        assert(t.ranges == 2 && t.blocks == 11 && t.min_lba == 100 && t.max_end == 1000000);
        assert(t.envelope_count == 2 && t.envelope[0].start == 100 && t.envelope[0].end == 110);
        assert(t.envelope[1].start == 999999 && t.envelope[1].end == 1000000);
        assert(t.ring_total == 2 && t.ring[1].lba == 999999 && t.ring[1].count == 1);
    }
    /* Rejected and empty commands are counted and never become commands. */
    lba[0] = 999999; count[0] = 2;
    struct ans_scsi_plan p = unmap(&t, lba, count, 1, dsm);
    assert(p.action == ANS_SCSI_CHECK && p.sense.asc == 0x21);
    assert(t.commands == 2 && t.rejected == 1 && t.last_reject == 0x052100 && t.ranges == 2);
    count[0] = 0; p = unmap(&t, lba, count, 1, dsm);
    assert(p.action == ANS_SCSI_COMPLETE && t.commands == 3 && t.empty == 1 && t.accepted == 1);
    /* A plan that is not a resolved UNMAP never leaves the gate as SUBMIT. */
    p = (struct ans_scsi_plan){.action = ANS_SCSI_SUBMIT, .nvme_opcode = 1, .dsm_ranges = 1};
    ans_trim_gate(&t, &p, dsm); assert(p.action == ANS_SCSI_CHECK);
    p = (struct ans_scsi_plan){.action = ANS_SCSI_SUBMIT, .nvme_opcode = 9, .dsm_ranges = 257};
    ans_trim_gate(&t, &p, dsm); assert(p.action == ANS_SCSI_CHECK);
    p = (struct ans_scsi_plan){.action = ANS_SCSI_SUBMIT, .nvme_opcode = 9, .dsm_ranges = 1};
    ans_trim_gate(NULL, &p, dsm); assert(p.action == ANS_SCSI_COMPLETE);
    p = (struct ans_scsi_plan){.action = ANS_SCSI_SUBMIT, .nvme_opcode = 9, .dsm_ranges = 1};
    ans_trim_gate(&t, &p, NULL); assert(p.action == ANS_SCSI_COMPLETE);
}
static void fence_tests(void)
{
    static uint8_t dsm[ANS_PRP_PAGE_SIZE];
    struct ans_trim t = {0};
    t.config = ans_trim_config_make(7, 2, 1000, 2000);
    /* Below, straddling the start, inside, touching the end, straddling the end, above. */
    uint64_t lba[7] = {10, 990, 1000, 1500, 1990, 1995, 2000};
    uint32_t count[7] = {5, 20, 1, 100, 10, 6, 1};
    struct ans_scsi_plan p = unmap(&t, lba, count, 7, dsm);
    assert(p.action == ANS_SCSI_SUBMIT && p.nvme_opcode == 9 && p.dsm_ranges == 3);
    assert(get_le(dsm + 8, 8) == 1000 && get_le(dsm + 4, 4) == 1);
    assert(get_le(dsm + 24, 8) == 1500 && get_le(dsm + 20, 4) == 100);
    assert(get_le(dsm + 40, 8) == 1990 && get_le(dsm + 36, 4) == 10);
    assert(all_zero(dsm + 48, sizeof dsm - 48));
    assert(t.ranges == 7 && t.sent_ranges == 3 && t.sent_blocks == 111);
    assert(t.fenced_ranges == 4 && t.fenced_blocks == 32 && !t.observed);
    assert(t.min_lba == 10 && t.max_end == 2001);
    /* Everything outside: GOOD without a command, page zero. */
    lba[0] = 0; count[0] = 1000; lba[1] = 2000; count[1] = 5;
    p = unmap(&t, lba, count, 2, dsm);
    assert(p.action == ANS_SCSI_COMPLETE && !p.dsm_ranges && all_zero(dsm, sizeof dsm));
    /* Randomised: nothing outside the fence is ever left in the page. */
    srand(1);
    for (unsigned round = 0; round < 2000; round++) {
        uint64_t l[256]; uint32_t c[256]; size_t n = 1 + (size_t)(rand() % 256);
        for (size_t i = 0; i < n; i++) {
            l[i] = (uint64_t)(rand() % 3000); c[i] = (uint32_t)(rand() % 300);
        }
        p = unmap(&t, l, c, n, dsm);
        unsigned kept = p.action == ANS_SCSI_SUBMIT ? p.dsm_ranges : 0;
        assert(p.action == ANS_SCSI_SUBMIT || p.action == ANS_SCSI_COMPLETE);
        for (unsigned i = 0; i < 256; i++) {
            uint64_t s = get_le(dsm + i * 16 + 8, 8), k = get_le(dsm + i * 16 + 4, 4);
            if (i >= kept) { assert(!s && !k && !get_le(dsm + i * 16, 4)); continue; }
            assert(k && s >= 1000 && s + k <= 2000);
        }
    }
    assert(t.sent_ranges + t.fenced_ranges == t.ranges);
    assert(t.sent_blocks + t.fenced_blocks == t.blocks);
    ans_trim_queued(&t); ans_trim_done(&t, 0); ans_trim_queued(&t); ans_trim_done(&t, 0x80);
    ans_trim_not_sent(&t);
    assert(t.queued == 2 && t.completed_ok == 1 && t.completed_error == 1 && t.last_status == 0x80);
    assert(t.not_sent == 1);
}
static void envelope_tests(void)
{
    static uint8_t dsm[ANS_PRP_PAGE_SIZE];
    struct ans_trim t = {0};
    t.config = ans_trim_config_make(0, 0, 0, 0);
    /* Adjacent and overlapping ranges coalesce. */
    uint64_t lba[4] = {100, 110, 300, 105}; uint32_t count[4] = {10, 10, 5, 200};
    (void)unmap(&t, lba, count, 2, dsm);
    assert(t.envelope_count == 1 && t.envelope[0].start == 100 && t.envelope[0].end == 120);
    (void)unmap(&t, lba + 2, count + 2, 1, dsm);
    assert(t.envelope_count == 2);
    (void)unmap(&t, lba + 3, count + 3, 1, dsm);
    assert(t.envelope_count == 1 && t.envelope[0].start == 100 && t.envelope[0].end == 305);
    /* Randomised superset property with forced joins. */
    static uint8_t touched[1000000 / 100];
    memset(&t, 0, sizeof t); t.config = ans_trim_config_make(0, 0, 0, 0);
    srand(7);
    for (unsigned round = 0; round < 400; round++) {
        uint64_t l[64]; uint32_t c[64]; size_t n = 1 + (size_t)(rand() % 64);
        for (size_t i = 0; i < n; i++) {
            l[i] = (uint64_t)(rand() % 9990) * 100; c[i] = 100 * (uint32_t)(1 + rand() % 3);
            for (uint32_t k = 0; k < c[i] / 100; k++) touched[l[i] / 100 + k] = 1;
        }
        (void)unmap(&t, l, c, n, dsm);
        assert(t.envelope_count && t.envelope_count <= ANS_TRIM_ENVELOPES);
        for (unsigned i = 0; i < t.envelope_count; i++) {
            assert(t.envelope[i].start < t.envelope[i].end);
            if (i) assert(t.envelope[i - 1].end < t.envelope[i].start);
        }
        assert(t.envelope[0].start == t.min_lba);
        assert(t.envelope[t.envelope_count - 1].end == t.max_end);
    }
    for (size_t b = 0; b < sizeof touched; b++) {
        if (!touched[b]) continue;
        int inside = 0;
        for (unsigned i = 0; i < t.envelope_count; i++)
            inside |= t.envelope[i].start <= b * 100 && b * 100 + 100 <= t.envelope[i].end;
        assert(inside);
    }
}
static void ioctl_tests(void)
{
    static uint8_t dsm[ANS_PRP_PAGE_SIZE];
    uint8_t raw[ANS_TRIM_IOCTL_TOTAL + 2], *b = raw + 1; /* unaligned, guarded */
    struct ans_trim t = {0};
    t.config = ans_trim_config_make(7, 2, 1000, 2000); t.oncs = 0x5f;
    uint64_t lba[2] = {1200, 5}; uint32_t count[2] = {7, 3};
    (void)unmap(&t, lba, count, 2, dsm);
    memset(raw, 0xee, sizeof raw); memset(b, 0, ANS_TRIM_IOCTL_TOTAL);
    b[0] = 28; memcpy(b + 4, "APANS001", 8); b[12] = 10;
    b[16] = 0x04; b[17] = 0x60; b[18] = 0x1b;
    b[24] = (uint8_t)ANS_TRIM_IOCTL_PAYLOAD; b[25] = (uint8_t)(ANS_TRIM_IOCTL_PAYLOAD >> 8);
    b[28] = 1; b[32] = b[24]; b[33] = b[25];
    assert(ANS_TRIM_IOCTL_TOTAL == 28 + 192 + 16 * 16 + 32 * 16);
    assert(ans_trim_ioctl_validate(b, ANS_TRIM_IOCTL_TOTAL));
    assert(!ans_trim_ioctl_validate(b, ANS_TRIM_IOCTL_TOTAL - 1));
    assert(!ans_trim_ioctl_validate(NULL, ANS_TRIM_IOCTL_TOTAL));
    b[16] = 0; assert(!ans_trim_ioctl_validate(b, ANS_TRIM_IOCTL_TOTAL)); b[16] = 4;
    b[4] = 'B'; assert(!ans_trim_ioctl_validate(b, ANS_TRIM_IOCTL_TOTAL)); b[4] = 'A';
    b[12] = 0; assert(!ans_trim_ioctl_validate(b, ANS_TRIM_IOCTL_TOTAL)); b[12] = 10;
    b[28] = 2; assert(!ans_trim_ioctl_reply(b, ANS_TRIM_IOCTL_TOTAL, &t, ns.blocks)); b[28] = 1;
    b[20] = 9;
    assert(ans_trim_ioctl_reply(b, ANS_TRIM_IOCTL_TOTAL, &t, ns.blocks));
    assert(raw[0] == 0xee && raw[sizeof raw - 1] == 0xee);
    const uint8_t *p = b + 28;
    assert(get_le(b + 20, 4) == 0 && get_le(p, 4) == 1 && get_le(p + 4, 4) == ANS_TRIM_IOCTL_PAYLOAD);
    assert(get_le(p + 8, 4) == 2 && get_le(p + 12, 4) == 0x5f);
    assert(get_le(p + 16, 8) == 1000 && get_le(p + 24, 8) == 2000 && get_le(p + 32, 8) == ns.blocks);
    assert(get_le(p + 40, 8) == 1 && get_le(p + 48, 8) == 1 && get_le(p + 72, 8) == 2);
    assert(get_le(p + 80, 8) == 10 && get_le(p + 96, 8) == 1 && get_le(p + 104, 8) == 3);
    assert(get_le(p + 112, 8) == 1 && get_le(p + 120, 8) == 7);
    assert(get_le(p + 168, 8) == 5 && get_le(p + 176, 8) == 1207);
    assert(get_le(p + 184, 4) == 2 && get_le(p + 188, 4) == 2);
    assert(get_le(p + 192, 8) == 5 && get_le(p + 200, 8) == 8);
    assert(get_le(p + 208, 8) == 1200 && get_le(p + 216, 8) == 1207);
    const uint8_t *r = p + 192 + 16 * 16;
    assert(get_le(r, 8) == 1200 && get_le(r + 8, 4) == 7 && get_le(r + 12, 4) == 0);
    assert(get_le(r + 16, 8) == 5 && get_le(r + 24, 4) == 3 && get_le(r + 28, 4) == 1);
}
int main(void)
{
    config_tests(); observe_tests(); fence_tests(); envelope_tests(); ioctl_tests();
    puts("ANS UNMAP policy, fence and receipt tests PASS");
    return 0;
}
