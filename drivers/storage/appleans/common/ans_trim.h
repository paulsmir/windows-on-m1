/* SPDX-License-Identifier: MIT */
#ifndef ANS_TRIM_H
#define ANS_TRIM_H
#include "ans_scsi.h"
/* UNMAP policy and receipt (investigation/analysis/ANS-TRIM-UNMAP.md, "Qualification gate").
 * The namespace is shared with other operating systems, so a translated UNMAP reaches the
 * controller only in DEALLOCATE mode and only for ranges inside a configured LBA fence.
 * Everything Windows asked for is recorded whether or not it is sent. */
enum ans_trim_mode {
    ANS_TRIM_OFF = 0,       /* UNMAP not advertised (VPD B0h/B2h absent, opcode rejected). */
    ANS_TRIM_OBSERVE = 1,   /* Advertised, validated, translated, recorded; never sent. */
    ANS_TRIM_DEALLOCATE = 2 /* As OBSERVE, and fenced ranges are sent as Dataset Management. */
};
#define ANS_TRIM_ENVELOPES 16u
#define ANS_TRIM_RING 32u
struct ans_trim_config {
    ans_u32 mode;                    /* Effective mode, already made safe. */
    ans_u64 fence_start, fence_end;  /* Namespace LBAs, end exclusive; 0/0 without a fence. */
};
struct ans_trim_extent { ans_u64 start, end; };
struct ans_trim_last { ans_u64 lba; ans_u32 count, sequence; };
struct ans_trim {
    struct ans_trim_config config;
    ans_u32 oncs;
    /* Per UNMAP command that reached the resolver. commands = accepted + empty + rejected. */
    ans_u64 commands, accepted, empty, rejected;
    /* Per validated range, before mode and fence are applied. */
    ans_u64 ranges, blocks;
    /* Disposition: observed = accepted commands not sent because of the mode; fenced_* =
     * ranges dropped by the fence in DEALLOCATE mode; queued/completed_* / not_sent follow one
     * Dataset Management command through the queue. */
    ans_u64 observed, fenced_ranges, fenced_blocks, sent_ranges, sent_blocks;
    ans_u64 queued, completed_ok, completed_error, not_sent;
    ans_u32 last_status, last_reject; /* NVMe status (no phase bit); key<<16|asc<<8|ascq. */
    ans_u64 min_lba, max_end;         /* Hull of every validated range. */
    /* Disjoint ascending extents whose union contains every validated range. When more than
     * ANS_TRIM_ENVELOPES would be needed the two nearest are joined: the union only grows. */
    ans_u32 envelope_count, ring_total;
    struct ans_trim_extent envelope[ANS_TRIM_ENVELOPES + 1];
    struct ans_trim_last ring[ANS_TRIM_RING]; /* Most recent validated ranges. */
};
/* have: bit 0 mode, bit 1 fence start, bit 2 fence end were read from configuration.
 * Missing or unknown mode is OBSERVE. DEALLOCATE without a non-empty fence is OBSERVE. */
struct ans_trim_config ans_trim_config_make(unsigned have, ans_u32 mode,
    ans_u32 fence_start, ans_u32 fence_end);
/* Call once for every UNMAP plan after ans_scsi_unmap_resolve, with the same range page.
 * Records the outcome; a SUBMIT plan stays SUBMIT only in DEALLOCATE mode and then names only
 * ranges that lie entirely inside the fence (page compacted, tail zero). Otherwise the plan
 * becomes COMPLETE (GOOD, no command) and the page is all zero. */
void ans_trim_gate(struct ans_trim *t, struct ans_scsi_plan *plan, ans_u8 dsm[ANS_PRP_PAGE_SIZE]);
void ans_trim_queued(struct ans_trim *t);
void ans_trim_not_sent(struct ans_trim *t);
void ans_trim_done(struct ans_trim *t, ans_u32 nvme_status);

/* Read-only receipt query: SRB_IO_CONTROL, signature "APANS001", this control code
 * (CTL_CODE(FILE_DEVICE_SCSI,0x801,METHOD_BUFFERED,FILE_READ_ACCESS)), exactly
 * ANS_TRIM_IOCTL_TOTAL bytes. Little-endian fields at fixed offsets (TELEMETRY.md). */
#define ANS_TRIM_IOCTL_QUERY 0x001b6004u
#define ANS_TRIM_IOCTL_VERSION 1u
#define ANS_TRIM_IOCTL_PAYLOAD (192u + ANS_TRIM_ENVELOPES * 16u + ANS_TRIM_RING * 16u)
#define ANS_TRIM_IOCTL_TOTAL (28u + ANS_TRIM_IOCTL_PAYLOAD)
int ans_trim_ioctl_validate(const void *buffer, ans_u32 bytes);
int ans_trim_ioctl_reply(void *buffer, ans_u32 bytes, const struct ans_trim *t,
                         ans_u64 namespace_blocks);
#endif
