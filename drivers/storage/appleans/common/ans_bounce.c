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
