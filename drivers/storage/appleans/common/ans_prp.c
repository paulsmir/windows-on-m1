/* SPDX-License-Identifier: MIT */
#include "ans_scsi.h"
#include <string.h>

enum ans_prp_error ans_sg_to_prps(const struct ans_sg_span *sg, size_t count,
    uint32_t bytes, struct ans_logical_prps *out)
{
    struct ans_logical_prps p = {0};
    uint64_t total = 0;
    uint32_t left = 0;
    uint64_t expected = 0;
    if (!out) return ANS_PRP_INVALID;
    memset(out, 0, sizeof(*out));
    if (bytes > ANS_MAX_TRANSFER || (count && !sg) || count > ANS_MAX_TRANSFER)
        return ANS_PRP_INVALID;
    if (!bytes) return count ? ANS_PRP_LENGTH : ANS_PRP_OK;
    if (!count) return ANS_PRP_LENGTH;
    if (sg[0].address & 3) return ANS_PRP_INVALID;
    for (size_t i = 0; i < count; ++i) {
        if (!sg[i].length || sg[i].address > UINT64_MAX - sg[i].length)
            return ANS_PRP_RANGE;
        total += sg[i].length;
        if (total > bytes) return ANS_PRP_LENGTH;
    }
    if (total != bytes) return ANS_PRP_LENGTH;
    for (size_t i = 0; i < count; ++i) {
        uint64_t addr = sg[i].address;
        uint32_t remain = sg[i].length;
        while (remain) {
            if (!left) {
                if (p.count == ANS_MAX_DATA_PAGES) return ANS_PRP_RANGE;
                if (p.count && (addr & (ANS_PRP_PAGE_SIZE - 1)))
                    return ANS_PRP_FRAGMENT;
                for (uint32_t j = 0; j < p.count; ++j)
                    if ((p.page[j] & ~(uint64_t)(ANS_PRP_PAGE_SIZE - 1)) ==
                        (addr & ~(uint64_t)(ANS_PRP_PAGE_SIZE - 1)))
                        return ANS_PRP_FRAGMENT;
                p.page[p.count++] = addr;
                left = ANS_PRP_PAGE_SIZE - (uint32_t)(addr & (ANS_PRP_PAGE_SIZE - 1));
            } else if (addr != expected) {
                return ANS_PRP_FRAGMENT;
            }
            uint32_t take = remain < left ? remain : left;
            addr += take;
            expected = addr;
            remain -= take;
            left -= take;
        }
    }
    p.bytes = bytes;
    *out = p;
    return ANS_PRP_OK;
}
