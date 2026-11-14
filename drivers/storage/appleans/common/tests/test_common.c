/* SPDX-License-Identifier: MIT */
#include "../ans_scsi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct ans_namespace ns = {
    .blocks = 0x100000010ULL, .block_size = 4096, .nsid = 1,
    .ready = 1, .cache_known = 1, .write_cache_enabled = 1,
};
/* SCSI INQUIRY fields are fixed-width and not NUL-terminated. */
static void fixture_strings(void)
{
    memcpy(ns.vendor, "TEST    ", sizeof ns.vendor);
    memcpy(ns.product, "OFFLINE FIXTURE ", sizeof ns.product);
    memcpy(ns.revision, "0001", sizeof ns.revision);
}
static void put(uint8_t *p, uint64_t v, size_t n)
{ while (n) { p[--n] = (uint8_t)v; v >>= 8; } }
static void sense(struct ans_scsi_plan p, uint8_t key, uint8_t asc)
{ assert(p.action == ANS_SCSI_CHECK && p.sense.key == key && p.sense.asc == asc); }
static struct ans_scsi_plan prep(uint8_t *c, size_t n, uint32_t b)
{ return ans_scsi_prepare(&ns, c, n, b, NULL); }
static void rw_tests(void)
{
    const uint8_t ops[] = {0x28, 0x2a, 0x88, 0x8a};
    for (size_t k = 0; k < sizeof(ops); ++k) {
        uint8_t c[16] = {0};
        int is16 = ops[k] >= 0x80;
        size_t len = is16 ? 16 : 10, blockpos = is16 ? 10 : 7;
        size_t blocklen = is16 ? 4 : 2;
        c[0] = ops[k];
        put(c + 2, 0x10203040, is16 ? 8 : 4);
        put(c + blockpos, 1, blocklen);
        struct ans_scsi_plan p = prep(c, len, 4096);
        assert(p.action == ANS_SCSI_SUBMIT && p.lba == 0x10203040);
        assert(p.nvme_opcode == (k % 2 ? 1 : 2) && p.nvme_nlb == 0);
        assert(p.direction == (k % 2 ? ANS_DMA_TO_DEVICE : ANS_DMA_FROM_DEVICE));
        assert(p.transfer_bytes == 4096 && p.nsid == 1);
        for (size_t n = 0; n <= 17; ++n)
            if (n != len) sense(prep(c, n, 4096), 5, 0x24);
        for (unsigned flag = 1; flag <= 128; flag <<= 1) {
            c[1] = (uint8_t)flag;
            if (flag == 0x08 || flag == 0x10) continue; /* FUA, DPO: below */
            sense(prep(c, len, 4096), 5, 0x24);
        }
        /* FUA -> NVMe CDW12 bit 30, block count untouched; DPO accepted and ignored. Windows
         * (classpnp) sets FUA on write-through writes: a reject makes disk.sys drop FUA for the
         * boot and try to switch the write cache off. */
        c[1] = 0x08; p = prep(c, len, 4096);
        assert(p.action == ANS_SCSI_SUBMIT && p.nvme_cdw12 == 0x40000000u && p.nvme_nlb == 0);
        c[1] = 0x10; p = prep(c, len, 4096);
        assert(p.action == ANS_SCSI_SUBMIT && p.nvme_cdw12 == 0);
        c[1] = 0x18; p = prep(c, len, 4096);
        assert(p.action == ANS_SCSI_SUBMIT && p.nvme_cdw12 == 0x40000000u);
        c[1] = 0; p = prep(c, len, 4096); assert(p.nvme_cdw12 == 0);
        for (unsigned flag = 1; flag <= 128; flag <<= 1) {
            c[is16 ? 14 : 6] = (uint8_t)flag;
            sense(prep(c, len, 4096), 5, 0x24);
            c[is16 ? 14 : 6] = 0;
            c[len - 1] = (uint8_t)flag;
            sense(prep(c, len, 4096), 5, 0x24);
            c[len - 1] = 0;
        }
        put(c + blockpos, 32, blocklen);
        p = prep(c, len, ANS_MAX_TRANSFER);
        assert(p.action == ANS_SCSI_SUBMIT && p.nvme_nlb == 31);
        put(c + blockpos, 33, blocklen);
        sense(prep(c, len, 33 * 4096), 5, 0x24);
        put(c + blockpos, 0, blocklen);
        assert(prep(c, len, 0).action == ANS_SCSI_COMPLETE);
        sense(prep(c, len, 4096), 5, 0x24);
        put(c + blockpos, 1, blocklen);
        sense(prep(c, len, 4095), 5, 0x24);
        sense(prep(c, len, 4097), 5, 0x24);
        ns.blocks = 100;
        put(c + 2, 99, is16 ? 8 : 4);
        assert(prep(c, len, 4096).action == ANS_SCSI_SUBMIT);
        put(c + blockpos, 2, blocklen);
        sense(prep(c, len, 8192), 5, 0x21);
        put(c + 2, 100, is16 ? 8 : 4);
        put(c + blockpos, 1, blocklen);
        sense(prep(c, len, 4096), 5, 0x21);
        if (is16) {
            ns.blocks = UINT64_MAX;
            put(c + 2, UINT64_MAX - 1, 8);
            put(c + blockpos, 2, 4);
            sense(prep(c, len, 8192), 5, 0x21);
            put(c + blockpos, 1, 4);
            assert(prep(c, len, 4096).action == ANS_SCSI_SUBMIT);
            put(c + 2, UINT64_MAX, 8);
            sense(prep(c, len, 4096), 5, 0x21);
        }
        ns.blocks = 0x100000010ULL;
        put(c + 2, 0, is16 ? 8 : 4);
        put(c + blockpos, 1, blocklen);
        ns.read_only = 1;
        if (k % 2) sense(prep(c, len, 4096), 7, 0x27);
        else assert(prep(c, len, 4096).action == ANS_SCSI_SUBMIT);
        ns.read_only = 0;
    }
}
static void discovery_tests(void)
{
    uint8_t c[16] = {0x25};
    struct ans_scsi_plan p = prep(c, 10, 8);
    assert(p.metadata_length == 8 && !memcmp(p.metadata, "\xff\xff\xff\xff\0\0\x10\0", 8));
    ns.blocks = 1;
    p = prep(c, 10, 8);
    assert(p.metadata[0] == 0 && p.metadata[3] == 0);
    ns.blocks = 0;
    sense(prep(c, 10, 8), 2, 4);
    ns.blocks = 0x100000010ULL;
    c[0] = 0x9e; c[1] = 0x10; c[13] = 32;
    p = prep(c, 16, 32);
    assert(p.metadata_length == 32 && p.metadata[3] == 1 && p.metadata[7] == 15);
    assert(p.metadata[10] == 0x10 && p.metadata[12] == 0 && p.metadata[14] == 0);
    c[14] = 1; sense(prep(c, 16, 32), 5, 0x24); c[14] = 0;
    for (uint32_t alloc = 0; alloc < 34; ++alloc) {
        c[13] = (uint8_t)alloc;
        p = prep(c, 16, 17);
        assert(p.metadata_length == (alloc < 17 ? alloc : 17));
    }
    memset(c, 0, sizeof(c)); c[0] = 0x12; c[4] = 64;
    p = prep(c, 6, 64);
    assert(p.metadata_length == 36 && p.metadata[4] == 31 && p.metadata[7] == 0);
    assert(!memcmp(p.metadata + 8, ns.vendor, 8));
    ns.tagged_queuing = 1;
    p = prep(c, 6, 64);
    assert(p.metadata[7] == 2 && p.metadata[5] == 0 && p.metadata[6] == 0);
    ns.tagged_queuing = 0;
    c[1] = 1; p = prep(c, 6, 64);
    /* Supported pages: 00h and B1h only, ascending. */
    assert(p.metadata_length == 6 && p.metadata[1] == 0 && p.metadata[3] == 2);
    assert(p.metadata[4] == 0 && p.metadata[5] == 0xb1);
    c[2] = 0x80; sense(prep(c, 6, 64), 5, 0x24);
    /* Without the Identify DSM flag Block Limits and LB Provisioning stay absent. */
    c[2] = 0xb0; sense(prep(c, 6, 64), 5, 0x24);
    c[2] = 0xb2; sense(prep(c, 6, 64), 5, 0x24);
    /* Block Device Characteristics: 64 bytes, PAGE LENGTH 003Ch, MEDIUM ROTATION
     * RATE 0001h (non-rotating); classpnp rejects any other page length and
     * reports a seek penalty for anything but 0001h. */
    c[2] = 0xb1; p = prep(c, 6, 64);
    assert(p.action == ANS_SCSI_COMPLETE && p.metadata_length == 64);
    assert(p.direction == ANS_DMA_FROM_DEVICE && p.transfer_bytes == 64);
    assert(p.metadata[0] == 0 && p.metadata[1] == 0xb1);
    assert(p.metadata[2] == 0 && p.metadata[3] == 0x3c);
    assert(p.metadata[4] == 0 && p.metadata[5] == 1);
    for (size_t n = 6; n < 64; ++n) assert(p.metadata[n] == 0);
    c[4] = 8; assert(prep(c, 6, 64).metadata_length == 8);
    c[4] = 64; assert(prep(c, 6, 5).metadata_length == 5);
    /* The page is medium-independent: answered before the namespace is ready. */
    ns.ready = 0; assert(prep(c, 6, 64).metadata_length == 64); ns.ready = 1;
    c[1] = 0; sense(prep(c, 6, 64), 5, 0x24); /* Page code without EVPD. */
    memset(c, 0, sizeof(c)); c[0] = 0xa0; c[9] = 16;
    p = prep(c, 12, 16);
    assert(p.metadata_length == 16 && p.metadata[3] == 8 && p.metadata[8] == 0);
    c[2] = 1; p = prep(c, 12, 16);
    assert(p.metadata_length == 8 && p.metadata[3] == 0);
    c[2] = 2; assert(prep(c, 12, 16).metadata[3] == 8);
    c[2] = 3; sense(prep(c, 12, 16), 5, 0x24);
    c[2] = 0; c[9] = 15; assert(prep(c, 12, 16).metadata_length == 15);
    memset(c, 0, sizeof(c));
    assert(prep(c, 6, 0).action == ANS_SCSI_COMPLETE);
    ns.ready = 0; sense(prep(c, 6, 0), 2, 4); ns.ready = 1;
    ns.block_size = 512; sense(prep(c, 6, 0), 2, 4); ns.block_size = 4096;
    c[0] = 3; c[4] = 18;
    struct ans_sense pending = {7, 0x27, 0};
    p = ans_scsi_prepare(&ns, c, 6, 18, &pending);
    assert(p.metadata[0] == 0x70 && p.metadata[2] == 7 && p.metadata[12] == 0x27);
    p = prep(c, 6, 18); assert(p.metadata[2] == 0 && p.metadata[12] == 0);
    c[1] = 1; p = ans_scsi_prepare(&ns, c, 6, 18, &pending);
    assert(p.metadata_length == 8 && p.metadata[0] == 0x72 && p.metadata[1] == 7 && p.metadata[2] == 0x27);
    c[1] = 2; sense(prep(c, 6, 18), 5, 0x24);
    c[0] = 0x42; sense(prep(c, 10, 0), 5, 0x20); /* No ONCS.DSM flag: UNMAP rejected. */
    c[0] = 0x1b; c[1] = 0; sense(prep(c, 6, 0), 5, 0x20);
}
static void cache_tests(void)
{
    uint8_t c[16] = {0};
    for (unsigned kind = 0; kind < 2; ++kind) {
        size_t len = kind ? 16 : 10;
        c[0] = kind ? 0x91 : 0x35;
        struct ans_scsi_plan p = prep(c, len, 0);
        assert(p.action == ANS_SCSI_SUBMIT && p.nvme_opcode == 0 && p.transfer_bytes == 0);
        ns.read_only = 1;
        assert(prep(c, len, 0).action == ANS_SCSI_SUBMIT); ns.read_only = 0;
        c[1] = 2; sense(prep(c, len, 0), 5, 0x24); c[1] = 0; /* IMMED */
        c[1] = 4; sense(prep(c, len, 0), 5, 0x24); c[1] = 0; /* SYNC_NV */
        sense(prep(c, len, 1), 5, 0x24);
    }
    for (unsigned kind = 0; kind < 2; ++kind) {
        memset(c, 0, sizeof(c)); c[0] = kind ? 0x5a : 0x1a;
        size_t len = kind ? 10 : 6, h = kind ? 8 : 4;
        c[1] = 8; c[2] = 8; c[kind ? 8 : 4] = 64;
        ns.read_only = 1;
        struct ans_scsi_plan p = prep(c, len, 64);
        assert(p.metadata_length == h + 20 && p.metadata[kind ? 3 : 2] == 0x90); /* WP + DPOFUA */
        assert(p.metadata[h] == 8 && p.metadata[h + 1] == 18 && p.metadata[h + 2] == 4);
        c[2] = 0x48; p = prep(c, len, 64);
        assert(p.metadata[h + 2] == 0); /* no writable cache parameters */
        c[2] = 0xc8; sense(prep(c, len, 64), 5, 0x24);
        c[2] = 8; c[1] = 0; p = prep(c, len, 64);
        assert(p.metadata_length == h + 28 && p.metadata[h + 6] == 0x10);
        ns.cache_known = 0; sense(prep(c, len, 64), 5, 0x20); ns.cache_known = 1;
        ns.read_only = 0;
    }
}
static void prp_tests(void)
{
    struct ans_logical_prps p;
    struct ans_sg_span sg[34] = {{0x1004, 131072}};
    assert(ans_sg_to_prps(sg, 1, 131072, &p) == ANS_PRP_OK);
    assert(p.count == 33 && p.page[0] == 0x1004 && p.page[32] == 0x21000);
    sg[0] = (struct ans_sg_span){0x1004, 100};
    sg[1] = (struct ans_sg_span){0x1068, 3992};
    sg[2] = (struct ans_sg_span){0x9000, 4};
    assert(ans_sg_to_prps(sg, 3, 4096, &p) == ANS_PRP_OK && p.count == 2);
    sg[1].address++;
    assert(ans_sg_to_prps(sg, 3, 4096, &p) == ANS_PRP_FRAGMENT && p.count == 0);
    sg[1].address--;
    sg[2].address++;
    assert(ans_sg_to_prps(sg, 3, 4096, &p) == ANS_PRP_FRAGMENT);
    sg[2].address = 0x1000;
    assert(ans_sg_to_prps(sg, 3, 4096, &p) == ANS_PRP_FRAGMENT);
    /* Clustered paging read with the memory manager's dummy page D repeated (ANS012-R3). */
    sg[0] = (struct ans_sg_span){0x76c97000, 4096};
    sg[1] = (struct ans_sg_span){0x44576000, 4096};
    sg[2] = (struct ans_sg_span){0x84724000, 4096};
    sg[3] = (struct ans_sg_span){0x44576000, 4096};
    assert(ans_sg_to_prps(sg, 4, 16384, &p) == ANS_PRP_FRAGMENT && p.count == 0);
    assert(ans_sg_to_prps_ex(sg, 4, 16384, &p, 1) == ANS_PRP_OK && p.count == 4 &&
           p.page[1] == 0x44576000 && p.page[3] == 0x44576000 && p.bytes == 16384);
    /* Repeats never relax the other PRP rules: a mid-transfer element must start page aligned. */
    sg[2].address = 0x84724200;
    assert(ans_sg_to_prps_ex(sg, 4, 16384, &p, 1) == ANS_PRP_FRAGMENT && p.count == 0);
    sg[2].address = 0x84724000; sg[1].length = 2048;
    assert(ans_sg_to_prps_ex(sg, 4, 14336, &p, 1) == ANS_PRP_FRAGMENT);
    sg[0] = (struct ans_sg_span){UINT64_MAX - 3, 4};
    assert(ans_sg_to_prps(sg, 1, 4, &p) == ANS_PRP_RANGE);
    sg[0] = (struct ans_sg_span){0x1001, 4096};
    assert(ans_sg_to_prps(sg, 1, 4096, &p) == ANS_PRP_INVALID);
    sg[0] = (struct ans_sg_span){0x1000, 4096};
    assert(ans_sg_to_prps(sg, 1, 4095, &p) == ANS_PRP_LENGTH);
    assert(ans_sg_to_prps(sg, 1, 4097, &p) == ANS_PRP_LENGTH);
    assert(ans_sg_to_prps(NULL, 0, 0, &p) == ANS_PRP_OK && p.count == 0);
    assert(ans_sg_to_prps(NULL, 1, 4096, &p) == ANS_PRP_INVALID);
    assert(ans_sg_to_prps(sg, 1, 0, &p) == ANS_PRP_LENGTH);
    assert(ans_sg_to_prps(sg, 1, 131073, &p) == ANS_PRP_INVALID);
    sg[0].length = 0; assert(ans_sg_to_prps(sg, 1, 4096, &p) == ANS_PRP_RANGE);
    for (unsigned offset = 0; offset < 4096; offset += 4) {
        uint32_t bytes = 131072;
        for (unsigned i = 0; i < 33; ++i) {
            uint32_t n = i ? 4096 : 4096 - offset;
            if (n > bytes) n = bytes;
            sg[i] = (struct ans_sg_span){0x100000 + i * 8192 + (i ? 0 : offset), n};
            bytes -= n;
            if (!bytes) {
                assert(ans_sg_to_prps(sg, i + 1, 131072, &p) == ANS_PRP_OK);
                assert(p.count == i + 1 && p.bytes == 131072);
                break;
            }
        }
    }
}
static void status_tests(void)
{
    assert(ans_scsi_nvme_sense(1, 0).key == 0); /* phase is not failure */
    assert(ans_scsi_nvme_sense(0x80 << 1, 0).asc == 0x21);
    assert(ans_scsi_nvme_sense(0x20 << 1, 1).key == 7);
    assert(ans_scsi_nvme_sense((2 << 9) | (0x81 << 1), 0).asc == 0x11);
    assert(ans_scsi_nvme_sense((2 << 9) | (0x80 << 1), 1).asc == 0x0c);
    for (unsigned status = 2; status < 4096; status += 2)
        assert(ans_scsi_nvme_sense((uint16_t)status, 0).key != 0);
}
/* Every CDB that ans_scsi_prepare can plan as a payload SUBMIT needs PRPs built
 * from the SRB scatter-gather list (ANS008: WRITE(10/16) was missing). */
static void test_payload_cdbs_need_prps(void)
{
    struct ans_namespace ns={.blocks=1000,.block_size=4096,.nsid=1,.ready=1};
    for (unsigned op=0;op<256;op++) {
        ans_u8 cdb[16]={0};cdb[0]=(ans_u8)op;cdb[op>=0x80?13:8]=1;
        struct ans_scsi_plan p=ans_scsi_prepare(&ns,cdb,op>=0x80?16:10,4096,NULL);
        int payload=p.action==ANS_SCSI_SUBMIT && p.transfer_bytes;
        assert(payload==ans_scsi_cdb_has_payload_prps(cdb[0]));
    }
    assert(ans_scsi_cdb_has_payload_prps(0x2a) && ans_scsi_cdb_has_payload_prps(0x8a));
}
/* WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
 * Unbuffered I/O from a buffer that is not page-aligned (EXP2437: a managed array) reaches the
 * miniport with a first-page offset. For EVERY dword-aligned offset and for lengths that make the
 * last page partial, the PRP set must name exactly the pages the bytes live in: page[0] carries
 * the offset, every later entry is page-aligned and consecutive for a contiguous buffer, the count
 * is ceil((offset + bytes) / 4096) and never exceeds 33; whether the SG list arrives as one run
 * or as one element per page must not matter. A wrong entry here is DMA to the wrong memory. */
static void prp_offset_sweep_tests(void)
{
    static const uint32_t lengths[] = {4096, 8192, 12288, 65536, 126976, 131072};
    const uint64_t base = 0x812340000ULL;
    for (uint32_t off = 0; off < 4096; off += 4) {
        for (size_t li = 0; li < sizeof lengths / sizeof lengths[0]; ++li) {
            uint32_t bytes = lengths[li], pages = (off + bytes + 4095) / 4096;
            struct ans_logical_prps one, split;
            struct ans_sg_span run = {base + off, bytes}, sg[34];
            size_t n = 0; uint32_t left = bytes; uint64_t a = base + off;
            while (left) { /* one SG element per physical page */
                uint32_t take = 4096 - (uint32_t)(a & 4095); if (take > left) take = left;
                sg[n].address = a; sg[n].length = take; n++; a += take; left -= take;
            }
            assert(pages <= 33 && n == pages);
            assert(ans_sg_to_prps_ex(&run, 1, bytes, &one, 1) == ANS_PRP_OK);
            assert(ans_sg_to_prps_ex(sg, n, bytes, &split, 1) == ANS_PRP_OK);
            assert(one.count == pages && split.count == pages && one.bytes == bytes && split.bytes == bytes);
            assert(one.page[0] == base + off && split.page[0] == base + off);
            for (uint32_t i = 1; i < pages; ++i) {
                assert(one.page[i] == base + (uint64_t)i * 4096 && split.page[i] == one.page[i]);
            }
            /* The same buffer on scattered physical pages: order and addresses are preserved. */
            for (size_t i = 1; i < n; ++i) sg[i].address = 0x900000000ULL + (uint64_t)(n - i) * 0x10000;
            assert(ans_sg_to_prps_ex(sg, n, bytes, &split, 1) == ANS_PRP_OK && split.count == pages);
            for (size_t i = 1; i < n; ++i) assert(split.page[i] == sg[i].address);
        }
    }
    /* Not dword-aligned: refused, never rounded. A fragment that restarts mid-page: refused. */
    for (uint64_t bad = 1; bad < 4; ++bad) {
        struct ans_logical_prps p; struct ans_sg_span run = {base + bad, 4096};
        assert(ans_sg_to_prps_ex(&run, 1, 4096, &p, 1) == ANS_PRP_INVALID && !p.count);
    }
    {
        struct ans_logical_prps p; struct ans_sg_span sg[2] = {{base + 24, 4072}, {base + 0x5008, 24}};
        assert(ans_sg_to_prps_ex(sg, 2, 4096, &p, 1) == ANS_PRP_FRAGMENT && !p.count);
        /* 128 KiB + offset needs 33 pages; one more page does not fit. */
        struct ans_sg_span big = {base + 4, 131072};
        assert(ans_sg_to_prps_ex(&big, 1, 131072, &p, 1) == ANS_PRP_OK && p.count == 33);
    }
}
/* UNMAP -> NVMe Dataset Management (deallocate). investigation/analysis/ANS-TRIM-UNMAP.md.
 * The capability exists only when Identify reported ONCS.DSM (ns.deallocate). */
static uint64_t get_le(const uint8_t *p, size_t n)
{ uint64_t v = 0; while (n--) v = (v << 8) | p[n]; return v; }
static size_t unmap_list(uint8_t *list, const uint64_t *lba, const uint32_t *count, size_t n)
{
    memset(list, 0, 8 + n * 16);
    put(list, n * 16 + 6, 2); put(list + 2, n * 16, 2);
    for (size_t i = 0; i < n; ++i) {
        put(list + 8 + i * 16, lba[i], 8);
        put(list + 8 + i * 16 + 8, count[i], 4);
    }
    return 8 + n * 16;
}
static struct ans_scsi_plan unmap(uint8_t *list, size_t bytes, uint8_t *dsm)
{
    uint8_t c[16] = {0x42};
    put(c + 7, bytes, 2);
    struct ans_scsi_plan p = prep(c, 10, (uint32_t)bytes);
    if (p.action == ANS_SCSI_SUBMIT) ans_scsi_unmap_resolve(&ns, &p, list, dsm);
    return p;
}
static void unmap_tests(void)
{
    static uint8_t list[8 + 257 * 16], dsm[ANS_PRP_PAGE_SIZE];
    static uint64_t lba[257]; static uint32_t count[257];
    uint8_t c[16] = {0x12, 1};
    struct ans_scsi_plan p;
    /* Without the Identify-derived flag nothing changes: no pages, no command. */
    assert(!ns.deallocate);
    c[4] = 64; p = prep(c, 6, 64); assert(p.metadata_length == 6 && p.metadata[3] == 2);
    memset(c, 0, sizeof c); c[0] = 0x42; put(c + 7, 24, 2);
    sense(prep(c, 10, 24), 5, 0x20);
    for (size_t n = 1; n <= 17; ++n) sense(prep(c, n, 24), 5, 0x20);
    p = (struct ans_scsi_plan){.action = ANS_SCSI_SUBMIT, .nvme_opcode = 9, .transfer_bytes = 24};
    lba[0] = 0; count[0] = 1; (void)unmap_list(list, lba, count, 1);
    memset(dsm, 0xa5, sizeof dsm);
    ans_scsi_unmap_resolve(&ns, &p, list, dsm);
    sense(p, 5, 0x20);
    for (size_t i = 0; i < sizeof dsm; ++i) assert(dsm[i] == 0);

    ns.deallocate = 1;
    /* VPD 00h lists 00h, B0h, B1h, B2h in ascending order. */
    memset(c, 0, sizeof c); c[0] = 0x12; c[1] = 1; c[4] = 64;
    p = prep(c, 6, 64);
    assert(p.metadata_length == 8 && p.metadata[1] == 0 && p.metadata[3] == 4);
    assert(!memcmp(p.metadata + 4, "\x00\xb0\xb1\xb2", 4));
    /* Block Limits: exactly the limits ans_scsi_unmap_resolve enforces. */
    c[2] = 0xb0; p = prep(c, 6, 64);
    assert(p.action == ANS_SCSI_COMPLETE && p.metadata_length == 64);
    assert(p.metadata[0] == 0 && p.metadata[1] == 0xb0 && p.metadata[2] == 0 && p.metadata[3] == 0x3c);
    for (size_t n = 4; n < 20; ++n) assert(p.metadata[n] == 0); /* transfer limits not reported */
    assert(!memcmp(p.metadata + 20, "\xff\xff\xff\xff", 4));        /* MAXIMUM UNMAP LBA COUNT */
    assert(!memcmp(p.metadata + 24, "\x00\x00\x01\x00", 4));        /* ...BLOCK DESCRIPTOR COUNT */
    assert(!memcmp(p.metadata + 28, "\x00\x00\x00\x01", 4));        /* OPTIMAL UNMAP GRANULARITY */
    for (size_t n = 32; n < 64; ++n) assert(p.metadata[n] == 0);    /* UGAVALID 0, no WRITE SAME */
    ns.ready = 0; assert(prep(c, 6, 64).metadata_length == 64); ns.ready = 1;
    /* Logical Block Provisioning: LBPU and nothing else. */
    c[2] = 0xb2; p = prep(c, 6, 64);
    assert(p.action == ANS_SCSI_COMPLETE && p.metadata_length == 8);
    assert(!memcmp(p.metadata, "\x00\xb2\x00\x04\x00\x80\x00\x00", 8));
    c[2] = 0xb1; assert(prep(c, 6, 64).metadata[5] == 1);
    /* READ CAPACITY(16) keeps LBPME/LBPRZ clear: no provisioning management claimed. */
    memset(c, 0, sizeof c); c[0] = 0x9e; c[1] = 0x10; c[13] = 32;
    assert(prep(c, 16, 32).metadata[14] == 0);

    /* CDB validation. */
    memset(c, 0, sizeof c); c[0] = 0x42; put(c + 7, 24, 2);
    p = prep(c, 10, 24);
    assert(p.action == ANS_SCSI_SUBMIT && p.nvme_opcode == 9 && p.nsid == 1);
    assert(p.direction == ANS_DMA_TO_DEVICE && p.transfer_bytes == 24 && !p.dsm_ranges);
    for (size_t n = 1; n <= 17; ++n) if (n != 10) sense(prep(c, n, 24), 5, 0x24);
    for (unsigned at = 1; at <= 6; ++at)
        for (unsigned flag = 1; flag <= 128; flag <<= 1) {
            c[at] = (uint8_t)flag; sense(prep(c, 10, 24), 5, 0x24); c[at] = 0; /* ANCHOR, group */
        }
    c[9] = 1; sense(prep(c, 10, 24), 5, 0x24); c[9] = 0;
    sense(prep(c, 10, 23), 5, 0x24); sense(prep(c, 10, 25), 5, 0x24);
    put(c + 7, 0, 2); p = prep(c, 10, 0);
    assert(p.action == ANS_SCSI_COMPLETE && !p.transfer_bytes); /* no parameter list: nothing to do */
    for (unsigned n = 1; n < 8; ++n) { put(c + 7, n, 2); sense(prep(c, 10, n), 5, 0x1a); }
    put(c + 7, 8 + 256 * 16, 2);
    assert(prep(c, 10, 8 + 256 * 16).action == ANS_SCSI_SUBMIT);
    put(c + 7, 8 + 256 * 16 + 1, 2); sense(prep(c, 10, 8 + 256 * 16 + 1), 5, 0x24);
    put(c + 7, 24, 2);
    ns.ready = 0; sense(prep(c, 10, 24), 2, 4); ns.ready = 1;
    ns.read_only = 1; sense(prep(c, 10, 24), 7, 0x27); ns.read_only = 0;

    /* One descriptor -> one little-endian DSM range in an otherwise zero page. */
    lba[0] = 0x01020304ULL; count[0] = 0x0a0b0c0d;
    memset(dsm, 0xa5, sizeof dsm);
    p = unmap(list, unmap_list(list, lba, count, 1), dsm);
    assert(p.action == ANS_SCSI_SUBMIT && p.nvme_opcode == 9 && p.dsm_ranges == 1);
    assert(p.transfer_bytes == 24 && p.direction == ANS_DMA_TO_DEVICE);
    assert(get_le(dsm, 4) == 0 && get_le(dsm + 4, 4) == 0x0a0b0c0d && get_le(dsm + 8, 8) == 0x01020304ULL);
    for (size_t i = 16; i < sizeof dsm; ++i) assert(dsm[i] == 0);
    /* Zero-length descriptors unmap nothing and are not encoded. */
    lba[0] = 5; count[0] = 0; lba[1] = 7; count[1] = 3; lba[2] = ns.blocks; count[2] = 0;
    p = unmap(list, unmap_list(list, lba, count, 3), dsm);
    assert(p.action == ANS_SCSI_SUBMIT && p.dsm_ranges == 1);
    assert(get_le(dsm + 4, 4) == 3 && get_le(dsm + 8, 8) == 7 && get_le(dsm + 20, 4) == 0);
    /* Only empty descriptors, or none: GOOD without a command. */
    p = unmap(list, unmap_list(list, lba, count, 1), dsm);
    assert(p.action == ANS_SCSI_COMPLETE && !p.dsm_ranges && !p.metadata_length);
    assert(p.transfer_bytes == 24 && p.direction == ANS_DMA_NONE);
    p = unmap(list, unmap_list(list, lba, count, 0), dsm);
    assert(p.action == ANS_SCSI_COMPLETE && p.transfer_bytes == 8);
    /* 256 descriptors fill the page exactly. */
    for (unsigned i = 0; i < 257; ++i) { lba[i] = 1000 + i * 10; count[i] = 2; }
    p = unmap(list, unmap_list(list, lba, count, 256), dsm);
    assert(p.action == ANS_SCSI_SUBMIT && p.dsm_ranges == 256);
    assert(get_le(dsm + 255 * 16 + 4, 4) == 2 && get_le(dsm + 255 * 16 + 8, 8) == 1000 + 255 * 10);
    /* Range checks: the last block is valid, one past it is not. */
    lba[0] = ns.blocks - 1; count[0] = 1;
    assert(unmap(list, unmap_list(list, lba, count, 1), dsm).action == ANS_SCSI_SUBMIT);
    count[0] = 2; sense(unmap(list, unmap_list(list, lba, count, 1), dsm), 5, 0x21);
    for (size_t i = 0; i < sizeof dsm; ++i) assert(dsm[i] == 0); /* nothing left behind */
    lba[0] = ns.blocks + 1; count[0] = 0;
    sense(unmap(list, unmap_list(list, lba, count, 1), dsm), 5, 0x21);
    lba[0] = UINT64_MAX; count[0] = 1;
    sense(unmap(list, unmap_list(list, lba, count, 1), dsm), 5, 0x21);
    /* A bad descriptor after a good one fails the whole command. */
    lba[0] = 1; count[0] = 1; lba[1] = ns.blocks; count[1] = 1;
    sense(unmap(list, unmap_list(list, lba, count, 2), dsm), 5, 0x21);
    /* MAXIMUM UNMAP LBA COUNT (FFFFFFFFh) is the sum over the command. */
    lba[0] = 0; count[0] = 0xffffffffu; lba[1] = 0; count[1] = 0;
    assert(unmap(list, unmap_list(list, lba, count, 2), dsm).dsm_ranges == 1);
    count[1] = 1; sense(unmap(list, unmap_list(list, lba, count, 2), dsm), 5, 0x26);
    /* Parameter list structure. */
    lba[0] = 1; count[0] = 1; lba[1] = 9; count[1] = 1;
    size_t bytes = unmap_list(list, lba, count, 2);
    put(list + 2, 33, 2); put(list, 39, 2); sense(unmap(list, bytes + 1, dsm), 5, 0x26);
    put(list + 2, 48, 2); put(list, 54, 2); sense(unmap(list, bytes, dsm), 5, 0x1a); /* truncated */
    put(list + 2, 32, 2); put(list, 37, 2); sense(unmap(list, bytes, dsm), 5, 0x26);
    put(list, 38, 2); list[5] = 1; sense(unmap(list, bytes, dsm), 5, 0x26); list[5] = 0;
    list[8 + 16 + 13] = 1; sense(unmap(list, bytes, dsm), 5, 0x26); list[8 + 16 + 13] = 0;
    assert(unmap(list, bytes, dsm).dsm_ranges == 2);
    /* Bytes after the descriptors named by the header are ignored. */
    list[bytes] = 0xff; assert(unmap(list, bytes + 1, dsm).dsm_ranges == 2);
    /* The helper is its own gate: read-only or a foreign plan never yields ranges. */
    p = unmap(list, bytes, dsm); ns.read_only = 1;
    ans_scsi_unmap_resolve(&ns, &p, list, dsm); sense(p, 7, 0x27); ns.read_only = 0;
    p = (struct ans_scsi_plan){.action = ANS_SCSI_SUBMIT, .nvme_opcode = 1, .transfer_bytes = 40};
    ans_scsi_unmap_resolve(&ns, &p, list, dsm); sense(p, 5, 0x20);
    p = (struct ans_scsi_plan){.action = ANS_SCSI_SUBMIT, .nvme_opcode = 9, .transfer_bytes = 7};
    ans_scsi_unmap_resolve(&ns, &p, list, dsm); sense(p, 5, 0x1a);
    ans_scsi_unmap_resolve(&ns, &p, NULL, dsm); sense(p, 5, 0x20);
    /* UNMAP carries a parameter list, not SG payload: no PRPs are built for it. */
    assert(!ans_scsi_cdb_has_payload_prps(0x42));
    ns.deallocate = 0;
}
int main(void)
{
    test_payload_cdbs_need_prps();
    fixture_strings();
    rw_tests(); discovery_tests(); cache_tests(); prp_tests(); status_tests();
    unmap_tests(); prp_offset_sweep_tests();
    puts("ANS common SCSI + SG/PRP deterministic tests PASS");
    return 0;
}
