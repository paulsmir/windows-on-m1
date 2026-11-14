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
 * All pool calls are made under the adapter's interrupt lock (StartIo / completion).
 * The pool is optional (EXP2447): it is built after the adapter has started, from up to
 * ANS_BOUNCE_COUNT separate buffers, and holds only those that could be allocated and resolved
 * to controller addresses. With no buffer the driver bounces nothing and hands every request
 * to the controller as before the bounce existed; it never fails to start for lack of a pool. */
#define ANS_DATA_ALIGNMENT ANS_PRP_PAGE_SIZE
#define ANS_BOUNCE_COUNT 16u
#define ANS_BOUNCE_PAGES (ANS_MAX_TRANSFER / ANS_PRP_PAGE_SIZE)
#define ANS_BOUNCE_BYTES ANS_MAX_TRANSFER
#define ANS_BOUNCE_DEFER_LIMIT 8u /* consecutive mapping failures of one request before it fails */
enum ans_bounce_step { /* where building one buffer failed */
    ANS_BOUNCE_STEP_NONE = 0,
    ANS_BOUNCE_STEP_ALLOCATE = 1,    /* no memory; address = allocator status */
    ANS_BOUNCE_STEP_PHYSICAL = 2,    /* a page has no physical address; address = page index */
    ANS_BOUNCE_STEP_TRANSLATE = 3,   /* not a controller-visible range; address = guest address */
    ANS_BOUNCE_STEP_LAYOUT = 4       /* not page-aligned or not contiguous; address = guest address */
};
struct ans_bounce_pool {
    ans_u32 count;                   /* usable buffers, 0..ANS_BOUNCE_COUNT; written once */
    ans_u32 failures;                /* buffers that could not be built */
    ans_u32 fail_step, fail_index;   /* first failure: enum ans_bounce_step, buffer attempt */
    ans_u64 fail_address;
    ans_u32 claimed;                 /* bit i: buffer i belongs to a request */
    ans_u32 high_water;              /* most buffers claimed at once */
    ans_u64 bounced_reads, bounced_writes;
    ans_u64 busy;                    /* requests returned BUSY because every buffer was in use */
    ans_u64 map_deferred;            /* requests returned BUSY because their data could not be mapped */
    ans_u64 map_failed;              /* requests failed after ANS_BOUNCE_DEFER_LIMIT such deferrals */
    ans_u64 map_mismatch;            /* mapped address did not belong to the request's first SG page */
    ans_u64 copies, copy_ticks, copy_ticks_max; /* copy cost in performance-counter ticks */
    ans_u64 ticks_per_second;
};
/* Non-zero when a transfer starting at first_address must be bounced. */
int ans_bounce_needed(ans_u64 first_address);
/* Whether a transfer of this size can be bounced at all (whole pages, at most one buffer). */
int ans_bounce_size_ok(ans_u32 bytes);
/* Claims a free buffer: its index, or -1 when all are in use (counted as busy). */
int ans_bounce_acquire(struct ans_bounce_pool *pool, int writing);
/* Releases a claimed buffer. Returns 0 if the index was not claimed (nothing changes). */
int ans_bounce_release(struct ans_bounce_pool *pool, int index);
/* Records one buffer that could not be built; the first one is kept for the receipt. */
void ans_bounce_build_failed(struct ans_bounce_pool *pool, ans_u32 step, ans_u32 index, ans_u64 address);
/* PRP set naming the first bytes/4096 pages of one bounce buffer; base_pa is the controller
 * address of the buffer, which is contiguous there. Output zero and 0 returned on any invalid
 * input. */
int ans_bounce_prps(ans_u64 base_pa, ans_u32 bytes, struct ans_logical_prps *out);
/* A request whose data could not be mapped: 1 = retry later (BUSY), 0 = give up (error). The
 * count lives in the request, so the limit holds whether or not the port driver prepares the
 * request again before the retry. */
int ans_bounce_map_deferred(struct ans_bounce_pool *pool, unsigned *request_defers);
void ans_bounce_copy_timed(struct ans_bounce_pool *pool, ans_u64 ticks);

/* Read-only bounce receipt: SRB_IO_CONTROL, signature "APANS001", control code
 * CTL_CODE(FILE_DEVICE_SCSI,0x803,METHOD_BUFFERED,FILE_READ_ACCESS), exactly
 * ANS_BOUNCE_IOCTL_TOTAL bytes. Payload (little-endian): 0 version = 2, 4 payload size, 8 usable
 * buffers (0 = nothing is bounced), 12 buffer bytes, 16 required alignment, 20 claimed bitmap,
 * 24 high water, 28 buffers attempted (ANS_BOUNCE_COUNT), then u64: 32 bounced reads, 40 bounced
 * writes, 48 busy (pool exhausted), 56 map deferred, 64 map failed, 72 map mismatch, 80 copies,
 * 88 copy ticks, 96 copy ticks max, 104 ticks/second; then the pool record: 112 first failing
 * step (enum ans_bounce_step), 116 its buffer attempt, 120 u64 its address, 128 buffers that
 * could not be built, 132 reserved. */
#define ANS_BOUNCE_IOCTL_QUERY 0x001b600cu
#define ANS_BOUNCE_IOCTL_VERSION 2u
#define ANS_BOUNCE_IOCTL_PAYLOAD 136u
#define ANS_BOUNCE_IOCTL_TOTAL (28u + ANS_BOUNCE_IOCTL_PAYLOAD)
int ans_bounce_ioctl_validate(const void *buffer, ans_u32 bytes);
int ans_bounce_ioctl_reply(void *buffer, ans_u32 bytes, const struct ans_bounce_pool *pool);
#endif
