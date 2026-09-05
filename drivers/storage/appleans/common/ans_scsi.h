/* SPDX-License-Identifier: MIT */
#ifndef APPLE_ANS_SCSI_H
#define APPLE_ANS_SCSI_H
#include <stddef.h>
#include <stdint.h>
#include "../include/apple_ans_broker_abi.h"

#define ANS_SCSI_METADATA_MAX 64u
struct ans_sense { uint8_t key, asc, ascq; };
struct ans_namespace {
    uint64_t blocks;
    uint32_t block_size; /* V1 accepts exactly ANS_PRP_PAGE_SIZE. */
    uint32_t nsid;
    uint8_t ready, read_only;
    /* Must be derived from identified/validated backend behavior, never guessed. */
    uint8_t cache_known, write_cache_enabled, read_cache_disabled;
    char vendor[8], product[16], revision[4]; /* Already space padded. */
};
enum ans_scsi_action { ANS_SCSI_COMPLETE, ANS_SCSI_CHECK, ANS_SCSI_SUBMIT };
struct ans_scsi_plan {
    enum ans_scsi_action action;
    struct ans_sense sense;
    uint8_t nvme_opcode, direction;
    uint16_t nvme_nlb; /* Zero based; valid only for SUBMIT READ/WRITE. */
    uint32_t nsid, nvme_cdw12, transfer_bytes;
    uint64_t lba;
    uint8_t metadata[ANS_SCSI_METADATA_MAX];
    size_t metadata_length; /* Already truncated to CDB allocation + buffer. */
};
/* Pure planner. Caller supplies the SRB transfer buffer length. Payload is never
 * accessed. pending_sense is a snapshot; wrapper owns autosense/consumption. */
struct ans_scsi_plan ans_scsi_prepare(const struct ans_namespace *ns,
    const uint8_t *cdb, size_t cdb_length, uint32_t buffer_length,
    const struct ans_sense *pending_sense);
void ans_scsi_fixed_sense(struct ans_sense sense, uint8_t out[18]);
/* NVMe CQ status including phase bit. Unknown failures stay failures. */
struct ans_sense ans_scsi_nvme_sense(uint16_t status, int writing);

enum ans_prp_error { ANS_PRP_OK, ANS_PRP_INVALID, ANS_PRP_RANGE,
                     ANS_PRP_LENGTH, ANS_PRP_FRAGMENT };
struct ans_sg_span { uint64_t address; uint32_t length; };
struct ans_logical_prps {
    uint64_t page[ANS_MAX_DATA_PAGES]; /* page[0] includes initial offset. */
    uint32_t count, bytes;
};
/* Input addresses are guest DMA addresses. No IPA->PA authorization here.
 * Output is all zero on failure; repeated data pages are rejected. */
enum ans_prp_error ans_sg_to_prps(const struct ans_sg_span *sg, size_t count,
    uint32_t transfer_bytes, struct ans_logical_prps *out);
#endif
