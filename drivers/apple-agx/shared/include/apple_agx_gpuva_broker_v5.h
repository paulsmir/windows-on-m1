#ifndef APPLE_AGX_GPUVA_BROKER_V5_H
#define APPLE_AGX_GPUVA_BROKER_V5_H

#include <stdint.h>

#define AGX_GPUVA_V5_OFFSET 0x700u
#define AGX_GPUVA_V5_WINDOW 0x100u
#define AGX_GPUVA_V5_DOORBELL 0xb8u
#define AGX_GPUVA_V5_RESPONSE_OFFSET 0xc0u
#define AGX_GPUVA_V5_VERSION 5u

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
};

typedef struct _AGX_GPUVA_V5_REQUEST {
    unsigned int Version, Bytes, Command, Flags;
    uint64_t Sequence, Epoch, ProcessId, ProcessGeneration;
    uint64_t TableIpa, AuxIpa, AllocationGeneration;
    uint64_t LogicalIpa[4];
    uint64_t Token;
    unsigned int Slot, Index, ValidMask, WritableMask;
} AGX_GPUVA_V5_REQUEST;

typedef struct _AGX_GPUVA_V5_RESPONSE {
    uint64_t Receipt;
    unsigned int Status, Flags;
    uint64_t Epoch, Token, RootGeneration, MapGeneration;
    uint64_t Reserved[2];
} AGX_GPUVA_V5_RESPONSE;

#endif
