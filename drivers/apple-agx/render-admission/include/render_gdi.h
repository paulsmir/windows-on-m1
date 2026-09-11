#ifndef APPLE_AGX_RENDER_GDI_H
#define APPLE_AGX_RENDER_GDI_H

#include "apple_agx_dma_shadow.h"
#include "apple_agx_gdi.h"

#define ADMISSION_GDI_PREPARED_MAGIC 0x50444752u /* "RGDP" */
#define ADMISSION_GDI_PREPARED_VERSION 1u
#define ADMISSION_GDI_COLOR_FILL_PATCH_COUNT 1u
#define ADMISSION_GDI_DESTINATION_SLOT 2u

typedef struct _ADMISSION_GDI_PATCH {
  unsigned int AllocationIndex;
  unsigned int SlotId;
  unsigned int PatchOffset;
  unsigned int SplitOffset;
} ADMISSION_GDI_PATCH;

typedef struct _ADMISSION_GDI_PREPARED {
  unsigned int Magic;
  unsigned int Version;
  unsigned int DmaOffset;
  unsigned int DmaBytes;
  unsigned int PatchCount;
  ADMISSION_GDI_PATCH Patches[ADMISSION_GDI_COLOR_FILL_PATCH_COUNT];
} ADMISSION_GDI_PREPARED;

typedef struct _ADMISSION_GDI_COLOR_FILL_INPUT {
  APPLE_AGX_GDI_RECT Destination;
  unsigned int DestinationAllocationIndex;
  unsigned int AllocationCount;
  unsigned int DestinationWritable;
  unsigned int Color;
  unsigned int DestinationPitch;
  unsigned int Rop;
  unsigned int Rop3;
  unsigned int SubRectCount;
  const APPLE_AGX_GDI_RECT *SubRects;
} ADMISSION_GDI_COLOR_FILL_INPUT;

int AdmissionGdiPrepareColorFill(
    const ADMISSION_GDI_COLOR_FILL_INPUT *Input,
    unsigned int DmaOffset, unsigned char *DmaBuffer,
    unsigned int DmaCapacity, ADMISSION_GDI_PREPARED *Prepared);

int AdmissionGdiPatchAuthorized(
    const ADMISSION_GDI_PREPARED *Prepared,
    const ADMISSION_GDI_PATCH *Patch,
    unsigned int SubmissionStart, unsigned int SubmissionEnd);

int AdmissionGdiDescribePreparedRecord(
    const unsigned char *DmaBuffer, unsigned int DmaBytes,
    unsigned int DmaOffset, ADMISSION_GDI_PREPARED *Prepared);

#endif /* APPLE_AGX_RENDER_GDI_H */
