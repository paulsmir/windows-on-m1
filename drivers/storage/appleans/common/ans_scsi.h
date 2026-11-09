/* SPDX-License-Identifier: MIT */
#ifndef APPLE_ANS_SCSI_H
#define APPLE_ANS_SCSI_H
#include "../include/apple_ans_broker_abi.h"
#if !defined(_KERNEL_MODE)
#include <stddef.h>
#endif

#define ANS_SCSI_METADATA_MAX 64u
struct ans_sense { ans_u8 key, asc, ascq; };
struct ans_namespace {
    ans_u64 blocks;
    ans_u32 block_size; /* V1 accepts exactly ANS_PRP_PAGE_SIZE. */
    ans_u32 nsid;
    ans_u8 ready, read_only;
    /* Set only when the wrapper enables concurrent tagged task management.
     * SNT1.5 6.1.1 CMDQUE; no ACA/abort/VPD task-attribute claims implied. */
    ans_u8 tagged_queuing;
    /* Must be derived from identified/validated backend behavior, never guessed. */
    ans_u8 cache_known, write_cache_enabled, read_cache_disabled;
    /* Identify Controller ONCS bit 2 (Dataset Management) on a transport that can
     * carry the command. Zero keeps UNMAP rejected and VPD B0h/B2h absent. */
    ans_u8 deallocate;
    char vendor[8], product[16], revision[4]; /* Already space padded. */
};
/* UNMAP limits, reported in VPD B0h and enforced by ans_scsi_unmap_resolve. One
 * NVMe Dataset Management command carries at most 256 ranges in one 4 KiB page. */
#define ANS_NVME_DSM 9
#define ANS_UNMAP_MAX_DESCRIPTORS 256u
#define ANS_UNMAP_MAX_LBA_COUNT 0xffffffffu
#define ANS_UNMAP_MAX_LIST (8u + ANS_UNMAP_MAX_DESCRIPTORS * 16u)
enum ans_scsi_action { ANS_SCSI_COMPLETE, ANS_SCSI_CHECK, ANS_SCSI_SUBMIT };
struct ans_scsi_plan {
    enum ans_scsi_action action;
    struct ans_sense sense;
    ans_u8 nvme_opcode, direction;
    ans_u16 nvme_nlb; /* Zero based; valid only for SUBMIT READ/WRITE. */
    ans_u32 nsid, nvme_cdw12, transfer_bytes;
    ans_u32 dsm_ranges; /* Set only by ans_scsi_unmap_resolve; 1..256 for SUBMIT DSM. */
    ans_u64 lba;
    ans_u8 metadata[ANS_SCSI_METADATA_MAX];
    size_t metadata_length; /* Already truncated to CDB allocation + buffer. */
};
/* Pure planner. Caller supplies the SRB transfer buffer length. Payload is never
 * accessed. pending_sense is a snapshot; wrapper owns autosense/consumption.
 * UNMAP (only with ns->deallocate) is planned from the CDB alone as SUBMIT with
 * nvme_opcode ANS_NVME_DSM and transfer_bytes = PARAMETER LIST LENGTH; the wrapper
 * must then call ans_scsi_unmap_resolve before the plan is used. */
struct ans_scsi_plan ans_scsi_prepare(const struct ans_namespace *ns,
    const ans_u8 *cdb, size_t cdb_length, ans_u32 buffer_length,
    const struct ans_sense *pending_sense);
/* Second UNMAP stage; the only code that reads a data-out buffer. list is the
 * caller-mapped parameter list of plan->transfer_bytes bytes; every field is read
 * once. dsm is a private 4 KiB page the caller will name in PRP1. On return the
 * plan is one of: SUBMIT with dsm_ranges set and dsm holding that many NVMe range
 * entries followed by zeros; COMPLETE (no blocks named: GOOD without a command);
 * CHECK with sense. Unless SUBMIT, dsm is all zero. Nothing is unmapped unless the
 * whole list is valid. */
void ans_scsi_unmap_resolve(const struct ans_namespace *ns, struct ans_scsi_plan *plan,
    const ans_u8 *list, ans_u8 dsm[ANS_PRP_PAGE_SIZE]);
void ans_scsi_fixed_sense(struct ans_sense sense, ans_u8 out[18]);
/* CDBs whose SUBMIT plan carries a payload: the wrapper must build PRPs for them. */
static inline int ans_scsi_cdb_has_payload_prps(ans_u8 op)
{ return op == 0x28 || op == 0x88 || op == 0x2a || op == 0x8a; }
/* NVMe CQ status including phase bit. Unknown failures stay failures. */
struct ans_sense ans_scsi_nvme_sense(ans_u16 status, int writing);

enum ans_prp_error { ANS_PRP_OK, ANS_PRP_INVALID, ANS_PRP_RANGE,
                     ANS_PRP_LENGTH, ANS_PRP_FRAGMENT };
struct ans_sg_span { ans_u64 address; ans_u32 length; };
struct ans_logical_prps {
    ans_u64 page[ANS_MAX_DATA_PAGES]; /* page[0] includes initial offset. */
    ans_u32 count, bytes;
};
/* Input addresses are guest DMA addresses. No IPA->PA authorization here.
 * Output is all zero on failure; repeated data pages are rejected. */
enum ans_prp_error ans_sg_to_prps(const struct ans_sg_span *sg, size_t count,
    ans_u32 transfer_bytes, struct ans_logical_prps *out);
/* ANS-DIRECT: the Windows memory manager maps one physical "dummy" page several times into
 * clustered paging-read MDLs (ANS012-R3: 48 FRAGMENT rejects, 8..29 SG elements). NVMe PRP lists
 * may name a page more than once; only the EL2 broker forbade it (pin bookkeeping). Every other
 * PRP shape rule is unchanged. */
enum ans_prp_error ans_sg_to_prps_ex(const struct ans_sg_span *sg, size_t count,
    ans_u32 transfer_bytes, struct ans_logical_prps *out, int allow_repeated_pages);
#endif
