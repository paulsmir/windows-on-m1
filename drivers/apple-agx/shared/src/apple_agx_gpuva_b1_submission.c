#include "apple_agx_gpuva_b1_submission.h"
#include <string.h>
APPLE_AGX_BACKEND_BOOL AppleAgxGpuvaB1PrepareSubmission(
    void *storage, APPLE_AGX_BACKEND_U32 capacity,
    const unsigned char *dma, APPLE_AGX_BACKEND_U32 dma_bytes,
    APPLE_AGX_BACKEND_U32 fence, APPLE_AGX_BACKEND_U32 slot,
    APPLE_AGX_BACKEND_SUBMISSION *sub)
{
  APPLE_AGX_DMA_SHADOW shadow;
  if (!sub || !storage || !dma || !dma_bytes || !fence ||
      !slot || slot >= 63u) return APPLE_AGX_BACKEND_FALSE;
  memset(sub,0,sizeof(*sub));
  AppleAgxDmaShadowInitialize(&shadow,storage,capacity);
  if (!AppleAgxDmaShadowAppend(&shadow,0u,dma,dma_bytes) ||
      !AppleAgxDmaShadowSeal(&shadow,fence))
    return APPLE_AGX_BACKEND_FALSE;
  sub->Submission.Kind=AppleAgxSubmissionGdi;
  sub->Submission.Fence=fence;
  sub->Submission.NodeOrdinal=0u;
  sub->Submission.EngineOrdinal=0u;
  sub->Submission.DmaBytes=dma_bytes;
  sub->ContextIdentity=slot;
  sub->PrivateData=storage;
  /* Open validates the declared capacity, while the submission range ends at
   * the used byte count.  Conflating these rejects the job before firmware. */
  sub->PrivateDataBytes=capacity;
  sub->PrivateDataStart=0u;
  sub->PrivateDataEnd=shadow.BytesUsed;
  sub->DmaSubmissionStart=0u;
  sub->DmaSubmissionEnd=dma_bytes;
  return APPLE_AGX_BACKEND_TRUE;
}
