/* SPDX-License-Identifier: MIT */
#include "ans_trim.h"

static ans_u64 get_le(const ans_u8 *p, unsigned n)
{
    ans_u64 v = 0;
    while (n--) v = (v << 8) | p[n];
    return v;
}
static void put_le(ans_u8 *p, ans_u64 v, unsigned n)
{
    while (n--) { *p++ = (ans_u8)v; v >>= 8; }
}
static void clear(ans_u8 *p, ans_u32 n)
{
    while (n--) *p++ = 0;
}
struct ans_trim_config ans_trim_config_make(unsigned have, ans_u32 mode,
    ans_u32 fence_start, ans_u32 fence_end)
{
    struct ans_trim_config c = {ANS_TRIM_OBSERVE, 0, 0};
    if ((have & 1u) && mode <= ANS_TRIM_DEALLOCATE) c.mode = mode;
    if ((have & 6u) == 6u && fence_start < fence_end) {
        c.fence_start = fence_start;
        c.fence_end = fence_end;
    } else if (c.mode == ANS_TRIM_DEALLOCATE) c.mode = ANS_TRIM_OBSERVE;
    return c;
}
static void envelope_add(struct ans_trim *t, ans_u64 start, ans_u64 end)
{
    struct ans_trim_extent *v = t->envelope;
    ans_u32 n = t->envelope_count, i = 0, j, k;
    if (n > ANS_TRIM_ENVELOPES) n = ANS_TRIM_ENVELOPES;
    while (i < n && v[i].end < start) i++;
    for (j = i; j < n && v[j].start <= end; j++) {
        if (v[j].start < start) start = v[j].start;
        if (v[j].end > end) end = v[j].end;
    }
    if (j == i) {
        for (k = n; k > i; k--) v[k] = v[k - 1];
        n++;
    } else if (j > i + 1) {
        for (k = j; k < n; k++) v[k - (j - i - 1)] = v[k];
        n -= j - i - 1;
    }
    v[i].start = start;
    v[i].end = end;
    if (n > ANS_TRIM_ENVELOPES) {
        ans_u32 best = 0;
        for (k = 1; k + 1 < n; k++)
            if (v[k + 1].start - v[k].end < v[best + 1].start - v[best].end) best = k;
        v[best].end = v[best + 1].end;
        for (k = best + 1; k + 1 < n; k++) v[k] = v[k + 1];
        n--;
    }
    t->envelope_count = n;
}
static void complete(struct ans_scsi_plan *plan, ans_u8 *dsm)
{
    clear(dsm, ANS_PRP_PAGE_SIZE);
    plan->action = ANS_SCSI_COMPLETE;
    plan->nvme_opcode = 0;
    plan->direction = ANS_DMA_NONE;
    plan->dsm_ranges = 0;
}
void ans_trim_gate(struct ans_trim *t, struct ans_scsi_plan *plan, ans_u8 *dsm)
{
    ans_u32 i, kept = 0, total;
    if (!plan) return;
    if (!t || !dsm) { /* No receipt, no command. */
        if (plan->action == ANS_SCSI_SUBMIT) {
            plan->action = ANS_SCSI_COMPLETE;
            plan->nvme_opcode = 0;
            plan->direction = ANS_DMA_NONE;
            plan->dsm_ranges = 0;
        }
        return;
    }
    t->commands++;
    if (plan->action == ANS_SCSI_CHECK) {
        t->rejected++;
        t->last_reject = ((ans_u32)plan->sense.key << 16) | ((ans_u32)plan->sense.asc << 8) |
                         plan->sense.ascq;
        return;
    }
    if (plan->action != ANS_SCSI_SUBMIT) { t->empty++; return; }
    total = plan->dsm_ranges;
    if (plan->nvme_opcode != ANS_NVME_DSM || !total || total > ANS_UNMAP_MAX_DESCRIPTORS) {
        /* Not a resolved UNMAP plan: never let it through as one. */
        t->rejected++;
        complete(plan, dsm);
        plan->action = ANS_SCSI_CHECK;
        plan->sense.key = 5; plan->sense.asc = 0x20; plan->sense.ascq = 0;
        t->last_reject = 0x052000;
        return;
    }
    t->accepted++;
    for (i = 0; i < total; i++) {
        ans_u32 count = (ans_u32)get_le(dsm + (ans_u32)i * 16 + 4, 4);
        ans_u64 lba = get_le(dsm + (ans_u32)i * 16 + 8, 8), end = lba + count;
        struct ans_trim_last *last = &t->ring[t->ring_total % ANS_TRIM_RING];
        if (!t->ranges || lba < t->min_lba) t->min_lba = lba;
        if (!t->ranges || end > t->max_end) t->max_end = end;
        t->ranges++;
        t->blocks += count;
        envelope_add(t, lba, end);
        last->lba = lba; last->count = count; last->sequence = t->ring_total++;
        if (t->config.mode != ANS_TRIM_DEALLOCATE) continue;
        if (lba < t->config.fence_start || end > t->config.fence_end || end < lba) {
            t->fenced_ranges++;
            t->fenced_blocks += count;
            continue;
        }
        if (kept != i) {
            put_le(dsm + (ans_u32)kept * 16, 0, 4);
            put_le(dsm + (ans_u32)kept * 16 + 4, count, 4);
            put_le(dsm + (ans_u32)kept * 16 + 8, lba, 8);
        }
        kept++;
        t->sent_ranges++;
        t->sent_blocks += count;
    }
    if (t->config.mode != ANS_TRIM_DEALLOCATE) {
        t->observed++;
        complete(plan, dsm);
        return;
    }
    if (!kept) { complete(plan, dsm); return; }
    clear(dsm + (ans_u32)kept * 16, ANS_PRP_PAGE_SIZE - kept * 16);
    plan->dsm_ranges = kept;
}
void ans_trim_queued(struct ans_trim *t) { if (t) t->queued++; }
void ans_trim_not_sent(struct ans_trim *t) { if (t) t->not_sent++; }
void ans_trim_done(struct ans_trim *t, ans_u32 nvme_status)
{
    if (!t) return;
    if (nvme_status) { t->completed_error++; t->last_status = nvme_status; }
    else t->completed_ok++;
}
int ans_trim_ioctl_validate(const void *buffer, ans_u32 bytes)
{
    static const ans_u8 signature[8] = {'A','P','A','N','S','0','0','1'};
    const ans_u8 *b = buffer;
    ans_u32 timeout, i;
    if (!b || bytes != ANS_TRIM_IOCTL_TOTAL) return 0;
    for (i = 0; i < 8; i++) if (b[4 + i] != signature[i]) return 0;
    timeout = (ans_u32)get_le(b + 12, 4);
    return get_le(b, 4) == 28 && timeout && timeout <= 60 &&
        get_le(b + 16, 4) == ANS_TRIM_IOCTL_QUERY &&
        get_le(b + 24, 4) == ANS_TRIM_IOCTL_PAYLOAD &&
        get_le(b + 28, 4) == ANS_TRIM_IOCTL_VERSION &&
        get_le(b + 32, 4) == ANS_TRIM_IOCTL_PAYLOAD;
}
int ans_trim_ioctl_reply(void *buffer, ans_u32 bytes, const struct ans_trim *t,
                         ans_u64 namespace_blocks)
{
    ans_u8 *b = buffer, *p;
    ans_u32 i, n;
    if (!t || !ans_trim_ioctl_validate(buffer, bytes)) return 0;
    put_le(b + 20, 0, 4); /* ReturnCode */
    p = b + 28;
    clear(p + 8, ANS_TRIM_IOCTL_PAYLOAD - 8);
    put_le(p + 8, t->config.mode, 4);       put_le(p + 12, t->oncs, 4);
    put_le(p + 16, t->config.fence_start, 8); put_le(p + 24, t->config.fence_end, 8);
    put_le(p + 32, namespace_blocks, 8);
    put_le(p + 40, t->commands, 8);         put_le(p + 48, t->accepted, 8);
    put_le(p + 56, t->empty, 8);            put_le(p + 64, t->rejected, 8);
    put_le(p + 72, t->ranges, 8);           put_le(p + 80, t->blocks, 8);
    put_le(p + 88, t->observed, 8);         put_le(p + 96, t->fenced_ranges, 8);
    put_le(p + 104, t->fenced_blocks, 8);   put_le(p + 112, t->sent_ranges, 8);
    put_le(p + 120, t->sent_blocks, 8);     put_le(p + 128, t->queued, 8);
    put_le(p + 136, t->completed_ok, 8);    put_le(p + 144, t->completed_error, 8);
    put_le(p + 152, t->not_sent, 8);
    put_le(p + 160, t->last_status, 4);     put_le(p + 164, t->last_reject, 4);
    n = t->envelope_count > ANS_TRIM_ENVELOPES ? ANS_TRIM_ENVELOPES : t->envelope_count;
    put_le(p + 168, t->min_lba, 8);         put_le(p + 176, t->max_end, 8);
    put_le(p + 184, n, 4);                  put_le(p + 188, t->ring_total, 4);
    p += 192;
    for (i = 0; i < n; i++) {
        put_le(p + i * 16, t->envelope[i].start, 8);
        put_le(p + i * 16 + 8, t->envelope[i].end, 8);
    }
    p += ANS_TRIM_ENVELOPES * 16u;
    for (i = 0; i < ANS_TRIM_RING; i++) {
        put_le(p + i * 16, t->ring[i].lba, 8);
        put_le(p + i * 16 + 8, t->ring[i].count, 4);
        put_le(p + i * 16 + 12, t->ring[i].sequence, 4);
    }
    return 1;
}
