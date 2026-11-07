/* SPDX-License-Identifier: MIT */
#ifndef APPLE_ANS_IDENTIFY_H
#define APPLE_ANS_IDENTIFY_H
#include "../common/ans_scsi.h"
/* Pinned WDK nvme.h byte offsets; output remains not ready until queues exist. */
int ans_identify_namespace(const ans_u8 controller[4096], const ans_u8 ns[4096],
                           struct ans_namespace *out);
#endif
