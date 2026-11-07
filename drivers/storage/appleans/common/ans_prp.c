/* SPDX-License-Identifier: MIT */
#include "ans_scsi.h"
/* WDK kernel builds use the kernel memory API, never user-mode CRT headers. */
#if defined(_KERNEL_MODE)
#define ans_zero_memory(destination, length) RtlZeroMemory((destination), (length))
#else
#include <string.h>
#define ans_zero_memory(destination, length) memset((destination), 0, (length))
#endif

enum ans_prp_error ans_sg_to_prps(const struct ans_sg_span *sg, size_t count,
    ans_u32 bytes, struct ans_logical_prps *out)
{
    return ans_sg_to_prps_ex(sg, count, bytes, out, 0);
}

enum ans_prp_error ans_sg_to_prps_ex(const struct ans_sg_span *sg, size_t count,
    ans_u32 bytes, struct ans_logical_prps *out, int allow_repeated_pages)
{
    struct ans_logical_prps p = {0};
    ans_u64 total = 0;
    ans_u32 left = 0;
    ans_u64 expected = 0;
    if (!out) return ANS_PRP_INVALID;
    ans_zero_memory(out, sizeof(*out));
    if (bytes > ANS_MAX_TRANSFER || (count && !sg) || count > ANS_MAX_TRANSFER)
        return ANS_PRP_INVALID;
    if (!bytes) return count ? ANS_PRP_LENGTH : ANS_PRP_OK;
    if (!count) return ANS_PRP_LENGTH;
    if (sg[0].address & 3) return ANS_PRP_INVALID;
    for (size_t i = 0; i < count; ++i) {
        if (!sg[i].length || sg[i].address > ANS_U64_MAX - sg[i].length)
            return ANS_PRP_RANGE;
        total += sg[i].length;
        if (total > bytes) return ANS_PRP_LENGTH;
    }
    if (total != bytes) return ANS_PRP_LENGTH;
    for (size_t i = 0; i < count; ++i) {
        ans_u64 addr = sg[i].address;
        ans_u32 remain = sg[i].length;
        while (remain) {
            if (!left) {
                if (p.count == ANS_MAX_DATA_PAGES) return ANS_PRP_RANGE;
                if (p.count && (addr & (ANS_PRP_PAGE_SIZE - 1)))
                    return ANS_PRP_FRAGMENT;
                for (ans_u32 j = 0; j < p.count && !allow_repeated_pages; ++j)
                    if ((p.page[j] & ~(ans_u64)(ANS_PRP_PAGE_SIZE - 1)) ==
                        (addr & ~(ans_u64)(ANS_PRP_PAGE_SIZE - 1)))
                        return ANS_PRP_FRAGMENT;
                p.page[p.count++] = addr;
                left = ANS_PRP_PAGE_SIZE - (ans_u32)(addr & (ANS_PRP_PAGE_SIZE - 1));
            } else if (addr != expected) {
                return ANS_PRP_FRAGMENT;
            }
            ans_u32 take = remain < left ? remain : left;
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
