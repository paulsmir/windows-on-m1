#ifndef APPLE_AGX_RENDER_DYNAMIC_DMA_H
#define APPLE_AGX_RENDER_DYNAMIC_DMA_H

#include "render_dynamic_overlay.h"
#include "render_gdi.h"

#define ADMISSION_DYNAMIC_DMA_MAGIC 0x4d444741u /* AGDM */
#define ADMISSION_DYNAMIC_DMA_VERSION 2u
#define ADMISSION_DYNAMIC_DMA_MAX_BYTES 4096u

typedef enum _ADMISSION_DYNAMIC_DMA_RESULT {
  AdmissionDynamicDmaSuccess = 0,
  AdmissionDynamicDmaArgument,
  AdmissionDynamicDmaCapacity,
  AdmissionDynamicDmaLayout,
  AdmissionDynamicDmaHash,
  AdmissionDynamicDmaJob,
} ADMISSION_DYNAMIC_DMA_RESULT;

typedef struct _ADMISSION_DYNAMIC_DMA_HEADER {
  APPLE_AGX_U32 Magic;
  APPLE_AGX_U32 Version;
  APPLE_AGX_U32 HeaderBytes;
  APPLE_AGX_U32 TotalBytes;
  APPLE_AGX_U32 Generation;
  APPLE_AGX_U32 Flags;
  APPLE_AGX_U32 JobOffset;
  APPLE_AGX_U32 JobBytes;
  APPLE_AGX_U32 StorageOffset;
  APPLE_AGX_U32 StorageBytes;
  APPLE_AGX_U64 CommandHash;
  APPLE_AGX_U64 DestinationGpuVa;
  APPLE_AGX_U64 ContentHash;
  ADMISSION_DYNAMIC_OVERLAY_BINDINGS Bindings;
  APPLE_AGX_U32 BackgroundColor;
  APPLE_AGX_U32 DestinationAllocationIndex;
  APPLE_AGX_U32 Reserved;
} ADMISSION_DYNAMIC_DMA_HEADER;

typedef struct _ADMISSION_DYNAMIC_DMA_VIEW {
  const ADMISSION_DYNAMIC_DMA_HEADER *Header;
  const ADMISSION_DYNAMIC_OVERLAY_BINDINGS *Bindings;
  const APPLE_AGX_DYNAMIC_JOB *Job;
  const unsigned char *Storage;
  APPLE_AGX_U32 StorageBytes;
} ADMISSION_DYNAMIC_DMA_VIEW;

APPLE_AGX_U64 AppleAgxDynamicDmaBytesHash(const void *Bytes,
                                          APPLE_AGX_U32 ByteCount);
APPLE_AGX_U64 AppleAgxDynamicDmaRecordHash(const void *Bytes,
                                           APPLE_AGX_U32 ByteCount);
APPLE_AGX_U32 AdmissionDynamicDmaDestinationPatchOffset(void);
ADMISSION_DYNAMIC_DMA_RESULT AdmissionDynamicDmaPatchDestination(
    void *Bytes, APPLE_AGX_U32 ByteCount,
    APPLE_AGX_U64 DestinationGpuVa);
ADMISSION_DYNAMIC_DMA_RESULT AdmissionDynamicDmaBuild(
    APPLE_AGX_U32 Generation, APPLE_AGX_U64 CommandHash,
    APPLE_AGX_U64 DestinationGpuVa,
    APPLE_AGX_U32 DestinationAllocationIndex,
    APPLE_AGX_U32 BackgroundColor,
    const ADMISSION_DYNAMIC_OVERLAY_BINDINGS *Bindings,
    const APPLE_AGX_DYNAMIC_JOB *Job, const void *Storage,
    APPLE_AGX_U32 StorageBytes, void *Destination,
    APPLE_AGX_U32 DestinationCapacity, APPLE_AGX_U32 *BytesWritten);
ADMISSION_DYNAMIC_DMA_RESULT AdmissionDynamicDmaOpen(
    const void *Bytes, APPLE_AGX_U32 ByteCount,
    ADMISSION_DYNAMIC_DMA_VIEW *View);
int AdmissionDynamicDmaDescribePreparedRecord(
    const unsigned char *DmaBuffer, APPLE_AGX_U32 DmaBytes,
    APPLE_AGX_U32 DmaOffset, ADMISSION_GDI_PREPARED *Prepared);

#endif
