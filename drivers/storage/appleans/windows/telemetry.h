/* SPDX-License-Identifier: MIT */
#ifndef APPLE_ANS_TELEMETRY_H
#define APPLE_ANS_TELEMETRY_H
#include "../include/apple_ans_broker_abi.h"
#define ANS_IOCTL_SIGNATURE "APANS001"
/* CTL_CODE(FILE_DEVICE_SCSI,0x800,METHOD_BUFFERED,FILE_READ_ACCESS). */
#define ANS_IOCTL_QUERY 0x001b6000u
#define ANS_IOCTL_VERSION 1u
#define ANS_IOCTL_WINDOWS 1u
#define ANS_IOCTL_BROKER 2u
struct ans_io_control {
    ans_u32 header_length;
    ans_u8 signature[8];
    ans_u32 timeout, control_code, return_code, length;
};
struct ans_telemetry_query {
    ans_u32 version, size, sources, reserved;
};
#define ANS_IOCTL_PAYLOAD_BYTES (16u + 2u * 232u)
#define ANS_IOCTL_TOTAL_BYTES (28u + ANS_IOCTL_PAYLOAD_BYTES)
void ans_stats_init(struct ans_telemetry *t);
void ans_stats_add(struct ans_telemetry *t, ans_u64 field, ans_u64 value);
void ans_stats_submit(struct ans_telemetry *t, unsigned queue, unsigned opcode);
void ans_stats_complete(struct ans_telemetry *t, unsigned queue, unsigned opcode,
                        ans_u32 bytes, int success);
void ans_stats_uncertain(struct ans_telemetry *t, unsigned queue, unsigned opcode);
void ans_stats_stopped(struct ans_telemetry *t, ans_u64 generation);
int ans_stats_ioctl_validate(const void *buffer, ans_u32 bytes, ans_u32 *sources);
int ans_stats_ioctl_reply(void *buffer, ans_u32 bytes, const struct ans_telemetry *windows,
                          const struct ans_telemetry *broker);
#endif
