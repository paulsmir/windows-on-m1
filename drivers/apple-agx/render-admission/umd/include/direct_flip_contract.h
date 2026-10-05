#ifndef APPLE_AGX_RENDER_UMD_DIRECT_FLIP_CONTRACT_H
#define APPLE_AGX_RENDER_UMD_DIRECT_FLIP_CONTRACT_H

#include "render_allocation.h"

#define ADMISSION_UMD_DIRECT_FLIP_RESOURCE_MAGIC 0x52464455u /* "UDFR" */
#define ADMISSION_UMD_DIRECT_FLIP_RESOURCE_VERSION 1u
#define ADMISSION_UMD_DIRECT_FLIP_IMMEDIATE 0x1u

typedef struct _ADMISSION_UMD_DIRECT_FLIP_RESOURCE {
  unsigned int Magic;
  unsigned int Version;
  ADMISSION_ALLOCATION_DESCRIPTION Allocation;
  unsigned int SegmentId;
  unsigned int Linear;
  unsigned int Displayable;
  unsigned int Reserved;
} ADMISSION_UMD_DIRECT_FLIP_RESOURCE;

int AdmissionUmdDirectFlipCompatible(
    const ADMISSION_UMD_DIRECT_FLIP_RESOURCE *Current,
    const ADMISSION_UMD_DIRECT_FLIP_RESOURCE *Candidate,
    unsigned int Flags, unsigned int ExpectedFormat);

#endif /* APPLE_AGX_RENDER_UMD_DIRECT_FLIP_CONTRACT_H */
