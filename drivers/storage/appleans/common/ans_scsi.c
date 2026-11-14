/* SPDX-License-Identifier: MIT */
#include "ans_scsi.h"
/* WDK kernel builds use the kernel memory API, never user-mode CRT headers. */
#if defined(_KERNEL_MODE)
#define ans_zero_memory(destination, length) RtlZeroMemory((destination), (length))
#define ans_copy_metadata(destination, source, length) RtlCopyMemory((destination), (source), (length))
#else
#include <string.h>
#define ans_zero_memory(destination, length) memset((destination), 0, (length))
#define ans_copy_metadata(destination, source, length) memcpy((destination), (source), (length))
#endif

static ans_u64 be(const ans_u8 *p, size_t n)
{
    ans_u64 v = 0;
    while (n--) v = (v << 8) | *p++;
    return v;
}
static void put_be(ans_u8 *p, ans_u64 v, size_t n)
{
    while (n) { p[--n] = (ans_u8)v; v >>= 8; }
}
static void put_le(ans_u8 *p, ans_u64 v, size_t n)
{
    while (n--) { *p++ = (ans_u8)v; v >>= 8; }
}
static int zero(const ans_u8 *p, size_t n)
{
    while (n--) if (*p++) return 0;
    return 1;
}
static struct ans_scsi_plan check(ans_u8 key, ans_u8 asc, ans_u8 ascq)
{
    struct ans_scsi_plan p = {0};
    p.action = ANS_SCSI_CHECK;
    p.sense = (struct ans_sense){key, asc, ascq};
    return p;
}
static struct ans_scsi_plan invalid(void) { return check(5, 0x24, 0); }
static struct ans_scsi_plan data(struct ans_scsi_plan p, size_t n,
                               ans_u32 alloc, ans_u32 buffer)
{
    if (n > alloc) n = alloc;
    if (n > buffer) n = buffer;
    p.metadata_length = n;
    p.transfer_bytes = (ans_u32)n;
    p.direction = n ? ANS_DMA_FROM_DEVICE : ANS_DMA_NONE;
    return p;
}
void ans_scsi_fixed_sense(struct ans_sense sense, ans_u8 out[18])
{
    ans_zero_memory(out, 18);
    out[0] = 0x70;
    out[2] = sense.key;
    out[7] = 10;
    out[12] = sense.asc;
    out[13] = sense.ascq;
}
struct ans_scsi_plan ans_scsi_prepare(const struct ans_namespace *ns,
    const ans_u8 *c, size_t len, ans_u32 buffer,
    const struct ans_sense *pending)
{
    struct ans_scsi_plan p = {0};
    ans_u32 blocks, alloc;
    ans_u64 lba;
    int long_cdb, writing;
    if (!ns || !c || !len) return invalid();
    switch (c[0]) {
    case 0x00: case 0x03: case 0x12: case 0x1a: case 0x1b:
        if (len != 6) return invalid();
        break;
    case 0x25: case 0x28: case 0x2a: case 0x35: case 0x5a:
        if (len != 10) return invalid();
        break;
    case 0x42: /* UNMAP exists only with Dataset Management (Identify ONCS). */
        if (!ns->deallocate) return check(5, 0x20, 0);
        if (len != 10) return invalid();
        break;
    case 0xa0:
        if (len != 12) return invalid();
        break;
    case 0x88: case 0x8a: case 0x91: case 0x9e:
        if (len != 16) return invalid();
        break;
    default: return check(5, 0x20, 0);
    }
    if (c[len - 1]) return invalid(); /* No linked command/NACA/vendor control. */
    switch (c[0]) {
    case 0x12: /* Standard inquiry and VPD pages 00h/B0h/B1h/B2h, no identities invented. */
        if ((c[1] & ~1u) || (!(c[1] & 1) && c[2])) return invalid();
        alloc = (ans_u32)be(c + 3, 2);
        if (c[1] & 1) {
            if (c[2] == 0xb1) {
                /* Block Device Characteristics (SBC-3; NVMe-SCSI translation):
                 * ANS is NAND, so MEDIUM ROTATION RATE is 0001h, non-rotating.
                 * The Windows class driver derives the seek-penalty property
                 * from this field and accepts only PAGE LENGTH 003Ch. Form
                 * factor and every other field stay "not reported". */
                p.metadata[1] = 0xb1;
                p.metadata[3] = 0x3c;
                p.metadata[5] = 1;
                return data(p, 64, alloc, buffer);
            }
            if (c[2] == 0xb0 && ns->deallocate) {
                /* Block Limits (SBC-3; SNT 1.5 6.1.6): only the UNMAP limits that
                 * ans_scsi_unmap_resolve enforces. MAXIMUM UNMAP BLOCK DESCRIPTOR
                 * COUNT 0100h is the NVMe range limit; granularity is one logical
                 * block, alignment not reported (UGAVALID 0). Transfer lengths and
                 * WRITE SAME stay "not reported". */
                p.metadata[1] = 0xb0;
                p.metadata[3] = 0x3c;
                put_be(p.metadata + 20, ANS_UNMAP_MAX_LBA_COUNT, 4);
                put_be(p.metadata + 24, ANS_UNMAP_MAX_DESCRIPTORS, 4);
                put_be(p.metadata + 28, 1, 4);
                return data(p, 64, alloc, buffer);
            }
            if (c[2] == 0xb2 && ns->deallocate) {
                /* Logical Block Provisioning: LBPU only. No WRITE SAME unmap, no
                 * ANCHOR, no thresholds, no provisioning type, and no LBPRZ: reads
                 * of deallocated blocks are not proven to return zeros. */
                p.metadata[1] = 0xb2;
                p.metadata[3] = 4;
                p.metadata[5] = 0x80;
                return data(p, 8, alloc, buffer);
            }
            if (c[2]) return invalid();
            /* Supported pages, ascending. B0h/B2h only with a translated UNMAP. */
            if (ns->deallocate) {
                p.metadata[3] = 4;
                p.metadata[5] = 0xb0;
                p.metadata[6] = 0xb1;
                p.metadata[7] = 0xb2;
                return data(p, 8, alloc, buffer);
            }
            p.metadata[3] = 2;
            p.metadata[5] = 0xb1;
            return data(p, 6, alloc, buffer);
        }
        p.metadata[2] = 6; /* SPC-4 response format, subset command implementation. */
        p.metadata[3] = 2;
        p.metadata[4] = 31;
        p.metadata[7] = ns->tagged_queuing ? 2 : 0;
        ans_copy_metadata(p.metadata + 8, ns->vendor, 8);
        ans_copy_metadata(p.metadata + 16, ns->product, 16);
        ans_copy_metadata(p.metadata + 32, ns->revision, 4);
        return data(p, 36, alloc, buffer);
    case 0xa0:
        if (c[1] || c[3] || c[4] || c[5] || c[10] || c[2] > 2)
            return invalid();
        alloc = (ans_u32)be(c + 6, 4);
        /* One ordinary LUN (0), no well-known LUNs. */
        if (c[2] == 1) return data(p, 8, alloc, buffer);
        p.metadata[3] = 8;
        return data(p, 16, alloc, buffer);
    case 0x03:
        if ((c[1] & ~1u) || c[2] || c[3]) return invalid();
        if (c[1] & 1) {
            p.metadata[0] = 0x72;
            p.metadata[1] = pending ? pending->key : 0;
            p.metadata[2] = pending ? pending->asc : 0;
            p.metadata[3] = pending ? pending->ascq : 0;
            return data(p, 8, c[4], buffer);
        }
        ans_scsi_fixed_sense(pending ? *pending : (struct ans_sense){0}, p.metadata);
        return data(p, 18, c[4], buffer);
    case 0x1b:
        /* No implemented controller power/flush/stop lifecycle. Never claim success. */
        return check(5, 0x20, 0);
    default: break;
    }
    if (!ns->ready || !ns->blocks || ns->block_size != ANS_PRP_PAGE_SIZE || !ns->nsid)
        return check(2, 4, 0); /* NOT READY, cause not reportable. */
    p.nsid = ns->nsid;
    switch (c[0]) {
    case 0x00:
        if (!zero(c + 1, 4)) return invalid();
        return p;
    case 0x25:
        if (!zero(c + 1, 8)) return invalid(); /* PMI=0, LBA=0 only. */
        put_be(p.metadata, ns->blocks - 1 > ((ans_u32)~0u) ? ((ans_u32)~0u) : ns->blocks - 1, 4);
        put_be(p.metadata + 4, ns->block_size, 4);
        return data(p, 8, 8, buffer);
    case 0x9e:
        if (c[1] != 0x10 || !zero(c + 2, 8) || c[14]) return invalid();
        put_be(p.metadata, ns->blocks - 1, 8);
        put_be(p.metadata + 8, ns->block_size, 4);
        return data(p, 32, (ans_u32)be(c + 10, 4), buffer);
    case 0x1a: case 0x5a: {
        size_t header = c[0] == 0x1a ? 4 : 8;
        size_t descriptor = (c[1] & 8) ? 0 : 8;
        ans_u8 page = c[2] & 0x3f;
        ans_u8 pc = c[2] >> 6;
        if ((c[1] & ~8u) || c[3] || (page != 8 && page != 0x3f) || pc > 1)
            return invalid(); /* No saved/default pages, LLBAA or subpages. */
        if (header == 8 && !zero(c + 4, 3)) return invalid();
        if (!ns->cache_known) return check(5, 0x20, 0);
        alloc = header == 4 ? c[4] : (ans_u32)be(c + 7, 2);
        size_t n = header + descriptor + 20;
        if (header == 4) {
            p.metadata[0] = (ans_u8)(n - 1);
            p.metadata[2] = (ans_u8)((ns->read_only ? 0x80 : 0) | 0x10); /* WP, DPOFUA */
            p.metadata[3] = (ans_u8)descriptor;
        } else {
            put_be(p.metadata, n - 2, 2);
            p.metadata[3] = (ans_u8)((ns->read_only ? 0x80 : 0) | 0x10);
            p.metadata[7] = (ans_u8)descriptor;
        }
        if (descriptor) {
            put_be(p.metadata + header + 1, ns->blocks > 0xffffff ? 0xffffff : ns->blocks, 3);
            put_be(p.metadata + header + 5, ns->block_size, 3);
        }
        p.metadata[header + descriptor] = 8;
        p.metadata[header + descriptor + 1] = 18;
        if (!pc) p.metadata[header + descriptor + 2] =
            (ns->write_cache_enabled ? 4 : 0) | (ns->read_cache_disabled ? 1 : 0);
        return data(p, n, alloc, buffer);
    }
    case 0x28: case 0x2a: case 0x88: case 0x8a:
        long_cdb = len == 16;
        writing = c[0] == 0x2a || c[0] == 0x8a;
        /* FUA (bit 3) becomes the NVMe FUA bit; DPO (bit 4) is advisory cache policy with no
         * NVMe equivalent and is ignored (SNT 1.5). classpnp sets FUA on every write-through
         * write while the write cache is on, whatever MODE SENSE says about DPOFUA. */
        if ((c[1] & ~0x18u) || c[long_cdb ? 14 : 6]) return invalid();
        lba = be(c + 2, long_cdb ? 8 : 4);
        blocks = (ans_u32)be(c + (long_cdb ? 10 : 7), long_cdb ? 4 : 2);
        if (blocks > ANS_MAX_TRANSFER / ANS_PRP_PAGE_SIZE) return invalid();
        if (buffer != blocks * ANS_PRP_PAGE_SIZE) return invalid();
        /* Zero READ/WRITE(10/16) means no transfer, not 65536 blocks. */
        if (!blocks) return p;
        if (lba >= ns->blocks || blocks > ns->blocks - lba) return check(5, 0x21, 0);
        if (writing && ns->read_only) return check(7, 0x27, 0);
        p.action = ANS_SCSI_SUBMIT;
        p.nvme_opcode = writing ? 1 : 2;
        p.direction = writing ? ANS_DMA_TO_DEVICE : ANS_DMA_FROM_DEVICE;
        p.lba = lba;
        p.nvme_nlb = (ans_u16)(blocks - 1);
        p.nvme_cdw12 = p.nvme_nlb | ((c[1] & 0x08) ? ANS_NVME_RW_FUA : 0);
        p.transfer_bytes = blocks * ANS_PRP_PAGE_SIZE;
        return p;
    case 0x42:
        /* ANCHOR and GROUP NUMBER are not supported. */
        if (c[1] || !zero(c + 2, 5)) return invalid();
        alloc = (ans_u32)be(c + 7, 2);
        if (buffer != alloc) return invalid();
        if (!alloc) return p; /* No parameter list: nothing to unmap, not an error. */
        if (alloc < 8) return check(5, 0x1a, 0); /* PARAMETER LIST LENGTH ERROR */
        if (alloc > ANS_UNMAP_MAX_LIST) return invalid();
        if (ns->read_only) return check(7, 0x27, 0);
        /* Descriptors are validated by ans_scsi_unmap_resolve. */
        p.action = ANS_SCSI_SUBMIT;
        p.nvme_opcode = ANS_NVME_DSM;
        p.direction = ANS_DMA_TO_DEVICE;
        p.transfer_bytes = alloc;
        return p;
    case 0x35: case 0x91:
        long_cdb = len == 16;
        if (c[1] || c[long_cdb ? 14 : 6] || buffer) return invalid();
        lba = be(c + 2, long_cdb ? 8 : 4);
        blocks = (ans_u32)be(c + (long_cdb ? 10 : 7), long_cdb ? 4 : 2);
        if (lba >= ns->blocks || (blocks && blocks > ns->blocks - lba))
            return check(5, 0x21, 0);
        /* Full namespace flush subsumes any valid range. Complete only on CQE. */
        p.action = ANS_SCSI_SUBMIT;
        p.nvme_opcode = 0;
        return p;
    default: return check(5, 0x20, 0);
    }
}
static struct ans_sense unmap_ranges(const struct ans_namespace *ns,
    const struct ans_scsi_plan *plan, const ans_u8 *list, ans_u8 *dsm, ans_u32 *ranges)
{
    const struct ans_sense unsupported = {5, 0x20, 0}, length_error = {5, 0x1a, 0};
    const struct ans_sense list_error = {5, 0x26, 0}, range_error = {5, 0x21, 0};
    const struct ans_sense protect = {7, 0x27, 0}, good = {0, 0, 0};
    ans_u32 length, bytes, i, n = 0;
    ans_u64 total = 0;
    if (!ns->deallocate || !list || plan->action != ANS_SCSI_SUBMIT ||
        plan->nvme_opcode != ANS_NVME_DSM) return unsupported;
    if (ns->read_only) return protect;
    length = plan->transfer_bytes;
    if (length < 8 || length > ANS_UNMAP_MAX_LIST) return length_error;
    bytes = (ans_u32)be(list + 2, 2); /* UNMAP BLOCK DESCRIPTOR DATA LENGTH */
    if (bytes > length - 8) return length_error; /* List shorter than its header says. */
    if ((bytes & 15) || be(list, 2) != (ans_u64)bytes + 6 || !zero(list + 4, 4))
        return list_error;
    for (i = 0; i < bytes / 16; i++) {
        const ans_u8 *d = list + 8 + (size_t)i * 16;
        ans_u64 lba = be(d, 8);
        ans_u32 count = (ans_u32)be(d + 8, 4);
        if (!zero(d + 12, 4)) return list_error;
        if (lba > ns->blocks || count > ns->blocks - lba) return range_error;
        if (!count) continue; /* Unmaps nothing; never sent as a zero-length range. */
        total += count;
        if (total > ANS_UNMAP_MAX_LBA_COUNT) return list_error;
        /* NVMe range: context attributes 0, length in blocks, starting LBA. */
        put_le(dsm + (size_t)n * 16 + 4, count, 4);
        put_le(dsm + (size_t)n * 16 + 8, lba, 8);
        n++;
    }
    *ranges = n;
    return good;
}
void ans_scsi_unmap_resolve(const struct ans_namespace *ns, struct ans_scsi_plan *plan,
    const ans_u8 *list, ans_u8 *dsm)
{
    struct ans_sense sense = {5, 0x20, 0};
    ans_u32 ranges = 0;
    if (!plan) return;
    if (dsm) ans_zero_memory(dsm, ANS_PRP_PAGE_SIZE);
    if (ns && dsm) sense = unmap_ranges(ns, plan, list, dsm, &ranges);
    if (sense.key) {
        if (dsm) ans_zero_memory(dsm, ANS_PRP_PAGE_SIZE);
        *plan = check(sense.key, sense.asc, sense.ascq);
    } else if (!ranges) {
        plan->action = ANS_SCSI_COMPLETE;
        plan->nvme_opcode = 0;
        plan->direction = ANS_DMA_NONE;
        plan->dsm_ranges = 0;
    } else plan->dsm_ranges = ranges;
}
struct ans_sense ans_scsi_nvme_sense(ans_u16 status, int writing)
{
    unsigned sc = (status >> 1) & 0xff, sct = (status >> 9) & 7;
    if (!sc && !sct) return (struct ans_sense){0};
    if (!sct) {
        switch (sc) {
        case 1: return (struct ans_sense){5, 0x20, 0};
        case 2: return (struct ans_sense){5, 0x24, 0};
        case 0x80: return (struct ans_sense){5, 0x21, 0};
        case 0x82: return (struct ans_sense){2, 4, 0};
        case 0x20: return (struct ans_sense){7, 0x27, 0};
        case 7: case 8: return (struct ans_sense){0xb, 0, 0};
        default: break;
        }
    }
    if (sct == 2 && sc == 0x80) return (struct ans_sense){3, 0x0c, 0};
    if (sct == 2 && sc == 0x81) return (struct ans_sense){3, 0x11, 0};
    (void)writing;
    return (struct ans_sense){4, 0x44, 0}; /* Unknown failure: internal target failure. */
}
