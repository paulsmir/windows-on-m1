#ifndef APPLE_AGX_RENDER_PRESENT_H
#define APPLE_AGX_RENDER_PRESENT_H

#include "render_allocation.h"
#include "apple_agx_gdi.h"

#define ADMISSION_PRESENT_BLT_MAGIC 0x42504152u /* RAPB */
#define ADMISSION_PRESENT_BLT_VERSION 1u
#define ADMISSION_PRESENT_BLT_DMA_MAX 4096u

typedef struct _ADMISSION_PRESENT_BLT_COMMAND {
  unsigned int Magic, Version, Bytes, RectCount;
  ADMISSION_ALLOCATION_DESCRIPTION SourceDescription;
  ADMISSION_ALLOCATION_DESCRIPTION DestinationDescription;
  APPLE_AGX_GDI_RECT SourceRect, DestinationRect;
  /* Private software-copy encoding: high byte segment id, low56 segment
   * address. These are Windows segment coordinates, never host physical PA. */
  unsigned long long SourceLocation, DestinationLocation, ContextToken;
} ADMISSION_PRESENT_BLT_COMMAND;

typedef struct _ADMISSION_PRESENT_BLT_INPUT {
  ADMISSION_PRESENT_BLT_COMMAND Command;
  const APPLE_AGX_GDI_RECT *Rects;
  unsigned int RectCount, MultipassOffset, SameAllocation;
} ADMISSION_PRESENT_BLT_INPUT;

typedef int (*ADMISSION_PRESENT_COPY_IO)(void *Context,
    unsigned long long Offset, void *Bytes, unsigned int ByteCount);

int AdmissionPresentLocationEncode(unsigned int Segment,
    unsigned long long Address, unsigned long long *Location);
int AdmissionPresentLocationDecode(unsigned long long Location,
    unsigned int *Segment, unsigned long long *Address);
int AdmissionPresentBltEncode(const ADMISSION_PRESENT_BLT_INPUT *Input,
    void *Buffer, unsigned int Capacity, unsigned int *BytesUsed,
    unsigned int *NextRect);
int AdmissionPresentBltValidate(const void *Buffer, unsigned int Bytes,
    int RequireResidency, ADMISSION_PRESENT_BLT_COMMAND *Command);
int AdmissionPresentBltScratchBytes(const ADMISSION_PRESENT_BLT_COMMAND *Command,
    unsigned int *Bytes);
int AdmissionPresentBltExecute(const void *Buffer, unsigned int Bytes,
    ADMISSION_PRESENT_COPY_IO ReadSource, ADMISSION_PRESENT_COPY_IO WriteDestination,
    void *IoContext, void *Scratch, unsigned int ScratchBytes,
    unsigned long long *BytesCopied);

#endif
