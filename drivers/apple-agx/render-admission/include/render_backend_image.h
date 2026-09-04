#ifndef APPLE_AGX_RENDER_BACKEND_IMAGE_H
#define APPLE_AGX_RENDER_BACKEND_IMAGE_H

#include "render_memory.h"
#include "apple_agx_relocation.h"
#include "apple_agx_render_template_rebase.h"

typedef struct _ADMISSION_BACKEND_IMAGE {
  APPLE_AGX_RENDER_TEMPLATE_ROOTS Roots;
  APPLE_AGX_EXP208_RELOCATION_OBJECT
      Objects[APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT];
  void *ArenaCpuAddress;
  APPLE_AGX_U64 ArenaPhysicalAddress;
  APPLE_AGX_U64 ArenaGpuAddress;
  APPLE_AGX_U32 ArenaBytes;
  APPLE_AGX_BOOL Ready;
} ADMISSION_BACKEND_IMAGE;

APPLE_AGX_BOOL AdmissionBackendImagePrepare(
    ADMISSION_BACKEND_IMAGE *Image,
    const ADMISSION_LOCAL_MEMORY_VIEW *BackendView);

void AdmissionBackendImageReset(ADMISSION_BACKEND_IMAGE *Image);

#endif /* APPLE_AGX_RENDER_BACKEND_IMAGE_H */
