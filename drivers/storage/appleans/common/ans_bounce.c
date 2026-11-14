/* SPDX-License-Identifier: MIT */
#include "ans_bounce.h"
typedef char ans_bounce_count_fits[(ANS_BOUNCE_COUNT <= 32) ? 1 : -1];
typedef char ans_bounce_pages_fit[(ANS_BOUNCE_PAGES <= ANS_MAX_DATA_PAGES) ? 1 : -1];
int ans_bounce_needed(ans_u64 first_address)
{
    return (first_address & (ANS_DATA_ALIGNMENT - 1)) != 0;
}
int ans_bounce_size_ok(ans_u32 bytes)
{
    return bytes && bytes <= ANS_BOUNCE_BYTES && !(bytes & (ANS_PRP_PAGE_SIZE - 1));
}
int ans_bounce_acquire(struct ans_bounce_pool *pool, int writing)
{
    ans_u32 used = 0;
    if (!pool) return -1;
    for (unsigned i = 0; i < ANS_BOUNCE_COUNT; i++) {
        if (pool->claimed & ((ans_u32)1 << i)) { used++; continue; }
        pool->claimed |= (ans_u32)1 << i;
        for (unsigned j = i + 1; j < ANS_BOUNCE_COUNT; j++)
            if (pool->claimed & ((ans_u32)1 << j)) used++;
        if (used + 1 > pool->high_water) pool->high_water = used + 1;
        if (writing) pool->bounced_writes++; else pool->bounced_reads++;
        return (int)i;
    }
    pool->busy++;
    return -1;
}
int ans_bounce_release(struct ans_bounce_pool *pool, int index)
{
    if (!pool || index < 0 || (unsigned)index >= ANS_BOUNCE_COUNT ||
        !(pool->claimed & ((ans_u32)1 << index))) return 0;
    pool->claimed &= ~((ans_u32)1 << index);
    return 1;
}
int ans_bounce_prps(const ans_u64 page_pa[ANS_BOUNCE_PAGES], ans_u32 bytes,
                    struct ans_logical_prps *out)
{
    if (!out) return 0;
    out->count = 0; out->bytes = 0;
    for (unsigned i = 0; i < ANS_MAX_DATA_PAGES; i++) out->page[i] = 0;
    if (!page_pa || !ans_bounce_size_ok(bytes)) return 0;
    for (unsigned i = 0; i < bytes / ANS_PRP_PAGE_SIZE; i++) {
        if (!page_pa[i] || ans_bounce_needed(page_pa[i])) {
            for (unsigned j = 0; j < ANS_MAX_DATA_PAGES; j++) out->page[j] = 0;
            return 0;
        }
        out->page[i] = page_pa[i];
    }
    out->count = bytes / ANS_PRP_PAGE_SIZE;
    out->bytes = bytes;
    return 1;
}
int ans_bounce_map_deferred(struct ans_bounce_pool *pool, unsigned *request_defers)
{
    if (!pool || !request_defers) return 0;
    if (++*request_defers > ANS_BOUNCE_DEFER_LIMIT) { pool->map_failed++; return 0; }
    pool->map_deferred++;
    return 1;
}
void ans_bounce_copy_timed(struct ans_bounce_pool *pool, ans_u64 ticks)
{
    if (!pool) return;
    pool->copies++;
    pool->copy_ticks += ticks;
    if (ticks > pool->copy_ticks_max) pool->copy_ticks_max = ticks;
}
static ans_u64 bounce_get_le(const ans_u8 *p, unsigned n)
{
    ans_u64 v = 0;
    while (n--) v = (v << 8) | p[n];
    return v;
}
static void bounce_put_le(ans_u8 *p, ans_u64 v, unsigned n)
{
    while (n--) { *p++ = (ans_u8)v; v >>= 8; }
}
int ans_bounce_ioctl_validate(const void *buffer, ans_u32 bytes)
{
    static const ans_u8 signature[8] = {'A','P','A','N','S','0','0','1'};
    const ans_u8 *b = buffer;
    ans_u32 timeout;
    if (!b || bytes != ANS_BOUNCE_IOCTL_TOTAL) return 0;
    for (unsigned i = 0; i < 8; i++) if (b[4 + i] != signature[i]) return 0;
    timeout = (ans_u32)bounce_get_le(b + 12, 4);
    return bounce_get_le(b, 4) == 28 && timeout && timeout <= 60 &&
        bounce_get_le(b + 16, 4) == ANS_BOUNCE_IOCTL_QUERY &&
        bounce_get_le(b + 24, 4) == ANS_BOUNCE_IOCTL_PAYLOAD &&
        bounce_get_le(b + 28, 4) == 1 && bounce_get_le(b + 32, 4) == ANS_BOUNCE_IOCTL_PAYLOAD;
}
int ans_bounce_ioctl_reply(void *buffer, ans_u32 bytes, const struct ans_bounce_pool *pool)
{
    ans_u8 *b = buffer, *p;
    if (!pool || !ans_bounce_ioctl_validate(buffer, bytes)) return 0;
    bounce_put_le(b + 20, 0, 4);
    p = b + 28;
    bounce_put_le(p + 8, ANS_BOUNCE_COUNT, 4);   bounce_put_le(p + 12, ANS_BOUNCE_BYTES, 4);
    bounce_put_le(p + 16, ANS_DATA_ALIGNMENT, 4); bounce_put_le(p + 20, pool->claimed, 4);
    bounce_put_le(p + 24, pool->high_water, 4);   bounce_put_le(p + 28, 0, 4);
    bounce_put_le(p + 32, pool->bounced_reads, 8); bounce_put_le(p + 40, pool->bounced_writes, 8);
    bounce_put_le(p + 48, pool->busy, 8);          bounce_put_le(p + 56, pool->map_deferred, 8);
    bounce_put_le(p + 64, pool->map_failed, 8);    bounce_put_le(p + 72, pool->map_mismatch, 8);
    bounce_put_le(p + 80, pool->copies, 8);        bounce_put_le(p + 88, pool->copy_ticks, 8);
    bounce_put_le(p + 96, pool->copy_ticks_max, 8); bounce_put_le(p + 104, pool->ticks_per_second, 8);
    return 1;
}
