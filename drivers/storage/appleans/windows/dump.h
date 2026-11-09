/* SPDX-License-Identifier: MIT */
#ifndef APPLE_ANS_DUMP_H
#define APPLE_ANS_DUMP_H
/* Crash-dump hand-over record (investigation/analysis/ANS-DUMP-MODE.md). The live instance
 * seals it inside its own device extension and passes the pointer through
 * MINIPORT_DUMP_POINTERS.MiniportPrivateDumpData; the dump instance (dump_AppleAns.sys)
 * receives it as PORT_CONFIGURATION_INFORMATION.MiniportDumpData and must validate it before
 * it touches the controller or dereferences anything the record points to. */
#include "../include/apple_ans_broker_abi.h"
#define ANS_DUMP_MAGIC 0x504d4441u /* "ADMP" */
#define ANS_DUMP_VERSION 1u
/* Receipt flags, written by the dump instance into a record it has validated. */
#define ANS_DUMP_SEEN_VALID 0x01u       /* record accepted */
#define ANS_DUMP_SEEN_RANGES 0x02u      /* exactly controller + mailbox ranges supplied */
#define ANS_DUMP_SEEN_CTRL_MAPPED 0x04u
#define ANS_DUMP_SEEN_MBOX_MAPPED 0x08u
struct ans_dump_context {
    ans_u32 magic, version, bytes, checksum;
    ans_u64 common_va, common_pa; /* live uncached extension: virtual address, raw IPA */
    ans_u32 common_bytes, block_size;
    ans_u64 blocks;
    ans_u32 nsid, read_only;
    /* Receipt: outside the checksum, read back by the live instance. */
    ans_u32 calls, last_mode, flags, range_count;
};
typedef char ans_dump_context_size_check[(sizeof(struct ans_dump_context) == 72) ? 1 : -1];
struct ans_dump_geometry {
    ans_u64 common_va, common_pa, blocks;
    ans_u32 common_bytes, block_size, nsid, read_only;
};
/* Returns 0 and leaves the record unsealed (magic 0) when the geometry is unusable. */
int ans_dump_context_seal(struct ans_dump_context *c, const struct ans_dump_geometry *g);
/* expected_common_bytes is the dump image's own APPLE_ANS_DIRECT_BYTES: a live image with a
 * different queue layout is refused. NULL, misaligned and damaged records are refused. */
int ans_dump_context_valid(const struct ans_dump_context *c, ans_u32 expected_common_bytes);
void ans_dump_context_receipt(struct ans_dump_context *c, ans_u32 mode, ans_u32 flags,
                              ans_u32 range_count);
/* 24-bit System-log value: calls(8) << 16 | mode(8) << 8 | flags(8). */
ans_u32 ans_dump_context_receipt_value(const struct ans_dump_context *c);
#endif
