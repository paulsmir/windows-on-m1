/* SPDX-License-Identifier: MIT */
#include "../ans_scsi.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static struct ans_namespace ns = {
    .blocks = 0x100000010ULL, .block_size = 4096, .nsid = 1,
    .ready = 1, .cache_known = 1, .write_cache_enabled = 1,
    .vendor = "TEST    ", .product = "OFFLINE FIXTURE ", .revision = "0001"
};
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
            sense(prep(c, len, 4096), 5, 0x24);
        }
        c[1] = 0;
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
    assert(p.metadata_length == 5 && p.metadata[3] == 1 && p.metadata[4] == 0);
    c[2] = 0x80; sense(prep(c, 6, 64), 5, 0x24);
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
    c[0] = 0x42; sense(prep(c, 10, 0), 5, 0x20); /* UNMAP never accepted. */
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
        assert(p.metadata_length == h + 20 && p.metadata[kind ? 3 : 2] == 0x80);
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
int main(void)
{
    rw_tests(); discovery_tests(); cache_tests(); prp_tests(); status_tests();
    puts("ANS common SCSI + SG/PRP deterministic tests PASS");
    return 0;
}
