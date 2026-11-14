/* SPDX-License-Identifier: MIT */
#ifndef ANS_BOUNCE_H
#define ANS_BOUNCE_H
#include "ans_scsi.h"
/* Bounce for data buffers the controller must not be given directly.
 * Controller rule (per ANS generation; t8103/J313 ANS2): the first data address of a READ/WRITE
 * must be ANS_DATA_ALIGNMENT-aligned. EXP2437-2441: with a first address inside a page (any
 * offset tried: 16, 0x20, 0x78, 0xEE8, 0xFC0) sustained unbuffered writes made the controller
 * stop completing commands in about half of the 512 MiB runs; page-aligned buffers ran ten of
 * ten clean. Windows may hand the miniport any dword-aligned buffer (AlignmentMask 3), so such a
 * transfer is copied through driver-owned page-aligned memory instead of being refused.
 * All pool calls are made under the adapter's interrupt lock (StartIo / completion). */
#define ANS_DATA_ALIGNMENT ANS_PRP_PAGE_SIZE
#define ANS_BOUNCE_COUNT 16u
#define ANS_BOUNCE_PAGES (ANS_MAX_TRANSFER / ANS_PRP_PAGE_SIZE)
#define ANS_BOUNCE_BYTES ANS_MAX_TRANSFER
struct ans_bounce_pool {
    ans_u32 claimed;                 /* bit i: buffer i belongs to a request */
    ans_u32 high_water;              /* most buffers claimed at once */
    ans_u64 bounced_reads, bounced_writes, busy; /* busy: request deferred, pool exhausted */
};
/* Non-zero when a transfer starting at first_address must be bounced. */
int ans_bounce_needed(ans_u64 first_address);
/* Whether a transfer of this size can be bounced at all (whole pages, at most one buffer). */
int ans_bounce_size_ok(ans_u32 bytes);
/* Claims a free buffer: its index, or -1 when all are in use (counted as busy). */
int ans_bounce_acquire(struct ans_bounce_pool *pool, int writing);
/* Releases a claimed buffer. Returns 0 if the index was not claimed (nothing changes). */
int ans_bounce_release(struct ans_bounce_pool *pool, int index);
/* PRP set naming the first bytes/4096 pages of one bounce buffer; page_pa holds the controller
 * addresses of its ANS_BOUNCE_PAGES pages. Output zero and 0 returned on any invalid input. */
int ans_bounce_prps(const ans_u64 page_pa[ANS_BOUNCE_PAGES], ans_u32 bytes,
                    struct ans_logical_prps *out);
#endif
