/* SPDX-License-Identifier: MIT */
#ifndef ANS_REJECT_H
#define ANS_REJECT_H
#include "../include/apple_ans_broker_abi.h"
/* Receipt of requests the miniport refuses because it does not implement them: which SRB
 * functions, SRB_IO_CONTROL control codes and CDB opcodes Windows actually sends. Read-only
 * evidence (ANSQ-T7: what an NVMe-typed adapter is asked that a Fibre-typed one is not). */
/* ANS_REJECT_CDB code = additional sense code << 8 | opcode (20h unknown opcode, 24h refused field). */
enum { ANS_REJECT_SRB_FUNCTION = 1, ANS_REJECT_IO_CONTROL = 2, ANS_REJECT_CDB = 3 };
#define ANS_REJECT_SLOTS 16u
struct ans_reject_entry { ans_u32 kind, code; ans_u64 count; };
struct ans_rejects {
    ans_u64 total[4];  /* indexed by kind; [0] unused */
    ans_u64 overflow;  /* rejects whose (kind, code) found no free slot */
    struct ans_reject_entry entry[ANS_REJECT_SLOTS]; /* distinct (kind, code), first come */
};
void ans_reject_note(struct ans_rejects *r, ans_u32 kind, ans_u32 code);
/* SRB_IO_CONTROL, signature "APANS001", CTL_CODE(FILE_DEVICE_SCSI,0x802,METHOD_BUFFERED,
 * FILE_READ_ACCESS), exactly ANS_REJECT_IOCTL_TOTAL bytes. Payload (little-endian): version 1,
 * payload size, totals for kinds 1..3 (3 x 8), overflow (8), then 16 x {kind 4, code 4, count 8}. */
#define ANS_REJECT_IOCTL_QUERY 0x001b6008u
#define ANS_REJECT_IOCTL_PAYLOAD (40u + ANS_REJECT_SLOTS * 16u)
#define ANS_REJECT_IOCTL_TOTAL (28u + ANS_REJECT_IOCTL_PAYLOAD)
int ans_reject_ioctl_validate(const void *buffer, ans_u32 bytes);
int ans_reject_ioctl_reply(void *buffer, ans_u32 bytes, const struct ans_rejects *r);
/* Control code of a (possibly foreign) SRB_IO_CONTROL buffer; 0xffffffff when too short. */
ans_u32 ans_reject_io_control_code(const void *buffer, ans_u32 bytes);
#endif
