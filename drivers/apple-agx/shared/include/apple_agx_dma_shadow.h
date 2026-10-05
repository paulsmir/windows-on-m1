#ifndef APPLE_AGX_DMA_SHADOW_H
#define APPLE_AGX_DMA_SHADOW_H

#include "apple_agx_state.h"

#define APPLE_AGX_DMA_SHADOW_MAGIC 0x53475841u
#define APPLE_AGX_DMA_SHADOW_VERSION 3u

typedef enum _APPLE_AGX_DMA_SHADOW_STATE {
  AppleAgxDmaShadowWritable = 1,
  AppleAgxDmaShadowSealed = 2,
} APPLE_AGX_DMA_SHADOW_STATE;

typedef struct _APPLE_AGX_DMA_SHADOW_HEADER {
  APPLE_AGX_U32 Magic;
  APPLE_AGX_U32 Version;
  APPLE_AGX_U32 BytesUsed;
  APPLE_AGX_U32 Capacity;
  APPLE_AGX_U32 RecordCount;
  APPLE_AGX_U32 State;
  APPLE_AGX_U32 Fence;
} APPLE_AGX_DMA_SHADOW_HEADER;

typedef struct _APPLE_AGX_DMA_SHADOW_RECORD {
  APPLE_AGX_U32 Magic;
  APPLE_AGX_U32 Version;
  APPLE_AGX_U32 RecordBytes;
  APPLE_AGX_U32 DmaOffset;
  APPLE_AGX_U32 DmaBytes;
  APPLE_AGX_U32 Reserved;
} APPLE_AGX_DMA_SHADOW_RECORD;

typedef struct _APPLE_AGX_DMA_SHADOW {
  unsigned char *Storage;
  APPLE_AGX_U32 Capacity;
  APPLE_AGX_U32 BytesUsed;
} APPLE_AGX_DMA_SHADOW;

typedef struct _APPLE_AGX_DMA_SHADOW_VIEW {
  APPLE_AGX_U32 DmaOffset;
  APPLE_AGX_U32 DmaBytes;
  const unsigned char *Bytes;
} APPLE_AGX_DMA_SHADOW_VIEW;

void AppleAgxDmaShadowInitialize(APPLE_AGX_DMA_SHADOW *Shadow,
                                 void *Storage,
                                 APPLE_AGX_U32 Capacity);
APPLE_AGX_BOOL AppleAgxDmaShadowOpen(APPLE_AGX_DMA_SHADOW *Shadow,
                                     void *Storage,
                                     APPLE_AGX_U32 Capacity);
APPLE_AGX_BOOL AppleAgxDmaShadowSeal(APPLE_AGX_DMA_SHADOW *Shadow,
                                     APPLE_AGX_U32 Fence);
APPLE_AGX_BOOL AppleAgxDmaShadowIsSealed(const void *Storage,
                                          APPLE_AGX_U32 BytesUsed);
APPLE_AGX_BOOL AppleAgxDmaShadowIsSealedForFence(
    const void *Storage, APPLE_AGX_U32 BytesUsed, APPLE_AGX_U32 Fence);
APPLE_AGX_BOOL AppleAgxDmaShadowIsVirgin(const void *Storage,
                                         APPLE_AGX_U32 Capacity);
APPLE_AGX_BOOL AppleAgxDmaShadowExtent(const void *Storage,
                                       APPLE_AGX_U32 BytesUsed,
                                       APPLE_AGX_U32 *DmaExtent);
APPLE_AGX_BOOL AppleAgxDmaShadowAppend(APPLE_AGX_DMA_SHADOW *Shadow,
                                       APPLE_AGX_U32 DmaOffset,
                                       const void *Bytes,
                                       APPLE_AGX_U32 ByteCount);
APPLE_AGX_BOOL AppleAgxDmaShadowValidate(const void *Storage,
                                         APPLE_AGX_U32 BytesUsed);
APPLE_AGX_BOOL AppleAgxDmaShadowFind(const void *Storage,
                                     APPLE_AGX_U32 BytesUsed,
                                     APPLE_AGX_U32 DmaOffset,
                                     APPLE_AGX_U32 DmaBytes,
                                     APPLE_AGX_DMA_SHADOW_VIEW *View);
APPLE_AGX_BOOL AppleAgxDmaShadowPatchU64(void *Storage,
                                         APPLE_AGX_U32 BytesUsed,
                                         APPLE_AGX_U32 PatchOffset,
                                         APPLE_AGX_U64 Value);
APPLE_AGX_BOOL AppleAgxDmaShadowMatchesU64(
    const void *Storage, APPLE_AGX_U32 BytesUsed,
    APPLE_AGX_U32 PatchOffset, APPLE_AGX_U64 Value);
APPLE_AGX_BOOL AppleAgxDmaShadowMatchesWritableU64(
    const void *Storage, APPLE_AGX_U32 BytesUsed,
    APPLE_AGX_U32 PatchOffset, APPLE_AGX_U64 Value);
APPLE_AGX_BOOL AppleAgxDmaShadowCopySubmission(
    const void *Storage, APPLE_AGX_U32 BytesUsed,
    APPLE_AGX_U32 SubmissionStart, APPLE_AGX_U32 SubmissionEnd,
    void *Destination, APPLE_AGX_U32 DestinationCapacity,
    APPLE_AGX_U32 *BytesCopied);
APPLE_AGX_BOOL AppleAgxDmaShadowCoversSubmission(
    const void *Storage, APPLE_AGX_U32 BytesUsed,
    APPLE_AGX_U32 SubmissionStart, APPLE_AGX_U32 SubmissionEnd);

#endif /* APPLE_AGX_DMA_SHADOW_H */
