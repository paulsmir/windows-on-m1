/* SPDX-License-Identifier: MIT */
#ifndef APPLE_ANS_REQUEST_H
#define APPLE_ANS_REQUEST_H
#include "../common/ans_scsi.h"
/* Tag must already be reserved by the caller. Only PRP metadata is written;
 * no data page is accessed. Invalid inputs leave the output unpublished.
 * NVMe WRITE is admitted only when writable is nonzero (the broker advertised
 * an EL2 write window and the package was built write-capable).
 * Dataset Management (plan resolved by ans_scsi_unmap_resolve into list) needs writable,
 * list and list_dma; the SQE names list_dma in PRP1 and prps is not consulted. */
int ans_io_request(const struct ans_scsi_plan *plan,
    const struct ans_logical_prps *prps, unsigned tag, ans_u64 list_dma,
    ans_u8 list[4096], int writable, struct ans_broker_request *out);
#endif
