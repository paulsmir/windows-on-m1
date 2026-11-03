#ifndef APPLE_AGX_GPUVA_BROKER_V5_H
#define APPLE_AGX_GPUVA_BROKER_V5_H


#define AGX_GPUVA_V5_OFFSET 0x700u
#define AGX_GPUVA_V5_WINDOW 0x100u
#define AGX_GPUVA_V5_DOORBELL 0xb8u
#define AGX_GPUVA_V5_RESPONSE_OFFSET 0xc0u
#define AGX_GPUVA_V5_VERSION 6u
/* Doorbell values: 1 executes the request in the MMIO window; 2 executes the
 * request at offset 0 of the attached 16-KiB mailbox page and also writes the
 * response at AGX_GPUVA_V5_MAILBOX_RESPONSE.  A broker without mailbox
 * support refuses AGX_GPUVA_V5_ATTACH_MAILBOX as INVALID. */
#define AGX_GPUVA_V5_DOORBELL_WINDOW 1u
#define AGX_GPUVA_V5_DOORBELL_MAILBOX 2u
#define AGX_GPUVA_V5_MAILBOX_BYTES 0x4000u
#define AGX_GPUVA_V5_MAILBOX_RESPONSE 0x80u

/* Optional extension 1; wire ABI remains 6. Query failure means absent.
 * Strict exclusive ordinary-RAM grants, one aligned 16-KiB leaf per call.
 * Caller pins backing until jobs retire, leaf clear/TLBI and revoke succeed. */
#define AGX_GPUVA_V5_SYSMEM_ABI 1u
#define AGX_GPUVA_V5_CAP_SYSMEM 1ULL
#define AGX_GPUVA_V5_SYSMEM_PAGE_BYTES 0x4000u

/* Q22b: optional live additive publication, negotiated per process. The
 * broker authenticates empty native descriptors and acknowledges table
 * visibility plus ASID invalidation before reporting success. */
#define AGX_GPUVA_V5_CAP_LIVE_ADDITIVE 2ULL
#define AGX_GPUVA_V5_LIVE_ABI 1u

enum {
    AGX_GPUVA_V5_CREATE = 1,
    AGX_GPUVA_V5_REGISTER_TABLE,
    AGX_GPUVA_V5_REGISTER_BACKING,
    AGX_GPUVA_V5_UPDATE_PARENT,
    AGX_GPUVA_V5_UPDATE_LEAF,
    AGX_GPUVA_V5_RELOCATE_ROOT,
    AGX_GPUVA_V5_LEASE,
    AGX_GPUVA_V5_JOB_BEGIN,
    AGX_GPUVA_V5_JOB_END,
    AGX_GPUVA_V5_RELEASE,
    AGX_GPUVA_V5_DESTROY,
    AGX_GPUVA_V5_REVOKE_BACKING,
    AGX_GPUVA_V5_REVOKE_TABLE,
    AGX_GPUVA_V5_REGISTER_SHARED_BACKING,
    AGX_GPUVA_V5_FLUSH_TLB,
    /* AuxIpa = mailbox page IPA, or 0 to detach. */
    AGX_GPUVA_V5_ATTACH_MAILBOX,
    /* All payload fields zero. Reserved[0]=caps, Reserved[1]=extension ABI. */
    AGX_GPUVA_V5_QUERY_CAPS,
    /* Process/generation, AllocationGeneration, AuxIpa only; exclusive.
     * UPDATE_LEAF and REVOKE_BACKING retain their existing semantics. */
    AGX_GPUVA_V5_REGISTER_SYSMEM_BACKING,
    /* ProcessId/ProcessGeneration identify the owner; Token is the requested
     * capability (LIVE_ADDITIVE or zero), AllocationGeneration is LIVE_ABI.
     * All other payload fields are zero. Negotiation requires no live jobs. */
    AGX_GPUVA_V5_NEGOTIATE_LIVE,
};

typedef struct _AGX_GPUVA_V5_REQUEST {
    unsigned int Version, Bytes, Command, Flags;
    unsigned long long Sequence, Epoch, ProcessId, ProcessGeneration;
    unsigned long long TableIpa, AuxIpa, AllocationGeneration;
    unsigned long long LogicalIpa[4];
    unsigned long long Token;
    unsigned int Slot, Index, ValidMask, WritableMask;
} AGX_GPUVA_V5_REQUEST;

typedef struct _AGX_GPUVA_V5_RESPONSE {
    unsigned long long Receipt;
    unsigned int Status, Flags;
    unsigned long long Epoch, Token, RootGeneration, MapGeneration;
    unsigned long long Reserved[2];
} AGX_GPUVA_V5_RESPONSE;

#endif
