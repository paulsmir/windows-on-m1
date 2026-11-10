/* SPDX-License-Identifier: MIT */
#include "identify.h"
static ans_u64 le(const ans_u8 *p, unsigned size)
{
    ans_u64 v = 0;
    for (unsigned i = 0; i < size; i++) v |= (ans_u64)p[i] << (8 * i);
    return v;
}
int ans_identify_namespace(const ans_u8 c[4096], const ans_u8 n[4096],
                           struct ans_namespace *out)
{
    if (!c || !n || !out) return 0;
    *out = (struct ans_namespace){0};
    unsigned format = n[26] & 15;
    ans_u64 blocks = le(n, 8), capacity = le(n + 8, 8);
    /* This physical adapter is specifically Apple ANS, namespace 1. */
    if (!le(c + 516, 4) || (c[77] && c[77] < 5) || !blocks || !capacity ||
        capacity > blocks || le(n + 16, 8) > capacity ||
        (n[26] & 0xf0) || format > n[25] || n[25] > 15 ||
        (n[29] & 7) || le(n + 128 + format * 4, 2) ||
        n[130 + format * 4] != 12)
        return 0;
    out->blocks = blocks;
    out->block_size = ANS_PRP_PAGE_SIZE;
    out->nsid = 1;
    out->read_only = 1;
    const char vendor[8] = {'N','V','M','e',' ',' ',' ',' '};
    for (unsigned i = 0; i < 8; i++) out->vendor[i] = vendor[i];
    for (unsigned i = 0; i < 16; i++) out->product[i] = (char)c[24+i];
    unsigned found = 0;
    for (unsigned i = 8; i && found < 4; i--) {
        ans_u8 ch = c[64+i-1];
        if (ch >= 0x21 && ch <= 0x7e) out->revision[3-found++] = (char)ch;
    }
    if (found != 4) { *out = (struct ans_namespace){0}; return 0; }
    /* ONCS bit 2: Dataset Management. The transport decides whether it may be used. */
    out->deallocate = (ans_u8)((le(c + 520, 2) >> 2) & 1);
    /* Identify VWC is presence, not the current feature setting. */
    out->cache_known = 0;
    return 1;
}
