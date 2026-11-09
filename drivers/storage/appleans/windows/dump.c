/* SPDX-License-Identifier: MIT */
#include "dump.h"
#include <stddef.h>
#define SEALED_BYTES ((ans_u32)(sizeof(struct ans_dump_context) - 4u * sizeof(ans_u32)))
static ans_u32 sum(const struct ans_dump_context *c)
{
    /* FNV-1a over the sealed part with the checksum word read as zero. */
    const ans_u8 *p = (const ans_u8 *)c;
    ans_u32 h = 2166136261u;
    for (ans_u32 i = 0; i < SEALED_BYTES; i++) {
        ans_u32 b = (i >= 12u && i < 16u) ? 0u : (ans_u32)p[i];
        h = (h ^ b) * 16777619u;
    }
    return h;
}
static int geometry_ok(ans_u64 va, ans_u64 pa, ans_u32 bytes, ans_u32 block_size,
                       ans_u64 blocks, ans_u32 nsid)
{
    return va && pa && bytes && va + bytes > va && pa + bytes > pa &&
        block_size == 4096u && blocks && nsid == 1u;
}
int ans_dump_context_seal(struct ans_dump_context *c, const struct ans_dump_geometry *g)
{
    if (!c) return 0;
    c->magic = 0;
    if (!g || !geometry_ok(g->common_va, g->common_pa, g->common_bytes, g->block_size,
                           g->blocks, g->nsid)) return 0;
    c->version = ANS_DUMP_VERSION;c->bytes = sizeof(*c);
    c->common_va = g->common_va;c->common_pa = g->common_pa;
    c->common_bytes = g->common_bytes;c->block_size = g->block_size;
    c->blocks = g->blocks;c->nsid = g->nsid;c->read_only = g->read_only ? 1u : 0u;
    c->magic = ANS_DUMP_MAGIC;
    c->checksum = sum(c);
    return 1;
}
int ans_dump_context_valid(const struct ans_dump_context *c, ans_u32 expected_common_bytes)
{
    if (!c || ((size_t)(const void *)c & 7u)) return 0;
    if (c->magic != ANS_DUMP_MAGIC || c->version != ANS_DUMP_VERSION ||
        c->bytes != sizeof(*c) || c->checksum != sum(c)) return 0;
    return c->common_bytes == expected_common_bytes && c->read_only <= 1u &&
        geometry_ok(c->common_va, c->common_pa, c->common_bytes, c->block_size,
                    c->blocks, c->nsid);
}
void ans_dump_context_receipt(struct ans_dump_context *c, ans_u32 mode, ans_u32 flags,
                              ans_u32 range_count)
{
    c->calls++;c->last_mode = mode;c->flags |= flags;c->range_count = range_count;
}
ans_u32 ans_dump_context_receipt_value(const struct ans_dump_context *c)
{
    return ((c->calls > 255u ? 255u : c->calls) << 16) | ((c->last_mode & 0xffu) << 8) |
        (c->flags & 0xffu);
}
