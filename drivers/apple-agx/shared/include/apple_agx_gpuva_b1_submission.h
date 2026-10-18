#ifndef APPLE_AGX_GPUVA_B1_SUBMISSION_H
#define APPLE_AGX_GPUVA_B1_SUBMISSION_H
#include "apple_agx_backend_runtime.h"
#include "apple_agx_dma_shadow.h"
APPLE_AGX_BACKEND_BOOL AppleAgxGpuvaB1PrepareSubmission(
    void *ShadowStorage, APPLE_AGX_BACKEND_U32 ShadowCapacity,
    const unsigned char *Dma, APPLE_AGX_BACKEND_U32 DmaBytes,
    APPLE_AGX_BACKEND_U32 Fence, APPLE_AGX_BACKEND_U32 Slot,
    APPLE_AGX_BACKEND_SUBMISSION *Submission);
#endif
