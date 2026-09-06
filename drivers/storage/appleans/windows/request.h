/* SPDX-License-Identifier: MIT */
#ifndef APPLE_ANS_REQUEST_H
#define APPLE_ANS_REQUEST_H
#include "../common/ans_scsi.h"
/* Tag must already be reserved by the caller. Only PRP metadata is written;
 * no data page is accessed. Invalid inputs leave the output unpublished. */
int ans_readonly_request(const struct ans_scsi_plan *plan,
    const struct ans_logical_prps *prps, unsigned tag, ans_u64 list_dma,
    ans_u8 list[4096], struct ans_broker_request *out);
#endif
