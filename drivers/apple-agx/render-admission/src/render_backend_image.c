#include "render_backend_image.h"

#define ADMISSION_BACKEND_IMAGE_NULL ((void *)0)
#define ADMISSION_BACKEND_PHYSICAL_LIMIT (1ULL << 40u)

static void AdmissionBackendImageZero(
    ADMISSION_BACKEND_IMAGE *Image) {
  unsigned char *bytes = (unsigned char *)Image;
  APPLE_AGX_U32 index;
  for (index = 0u;
       index < (APPLE_AGX_U32)sizeof(*Image); ++index)
    bytes[index] = 0u;
}

APPLE_AGX_BOOL AdmissionBackendImagePrepare(
    ADMISSION_BACKEND_IMAGE *Image,
    const ADMISSION_LOCAL_MEMORY_VIEW *BackendView) {
  ADMISSION_BACKEND_IMAGE candidate;
  APPLE_AGX_U32 template_bytes;

  if (Image == ADMISSION_BACKEND_IMAGE_NULL ||
      BackendView == ADMISSION_BACKEND_IMAGE_NULL ||
      BackendView->CpuAddress == ADMISSION_BACKEND_IMAGE_NULL ||
      BackendView->HostPhysicalAddress == 0ULL ||
      BackendView->GpuVirtualAddress == 0ULL ||
      BackendView->Bytes == 0ULL || BackendView->Bytes > 0xffffffffULL ||
      (BackendView->HostPhysicalAddress & 0x3fffULL) != 0ULL ||
      (BackendView->GpuVirtualAddress &
       (APPLE_AGX_RENDER_TEMPLATE_ALIGNMENT - 1u)) != 0ULL)
    return APPLE_AGX_FALSE;
  template_bytes = AppleAgxRenderTemplateBytes();
  if (template_bytes == 0u || template_bytes > BackendView->Bytes ||
      BackendView->HostPhysicalAddress >= ADMISSION_BACKEND_PHYSICAL_LIMIT ||
      template_bytes > ADMISSION_BACKEND_PHYSICAL_LIMIT -
                           BackendView->HostPhysicalAddress)
    return APPLE_AGX_FALSE;

  AdmissionBackendImageZero(&candidate);
  if (!AppleAgxRenderTemplateMaterialize(
          BackendView->CpuAddress, (APPLE_AGX_U32)BackendView->Bytes,
          &candidate.Roots) ||
      !AppleAgxRenderTemplateBuildRelocationObjectsRebased(
          BackendView->CpuAddress, (APPLE_AGX_U32)BackendView->Bytes,
          BackendView->HostPhysicalAddress,
          BackendView->GpuVirtualAddress,
          BackendView->GpuVirtualAddress, BackendView->Bytes,
          candidate.Objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
          &candidate.Roots) ||
      !AppleAgxApplyRelocations(
          candidate.Objects,
          APPLE_AGX_RENDER_TEMPLATE_RUNTIME_OBJECT_COUNT,
          AppleAgxRenderTemplateRelocations(),
          AppleAgxRenderTemplateRelocationCount()))
    return APPLE_AGX_FALSE;

  candidate.ArenaCpuAddress = BackendView->CpuAddress;
  candidate.ArenaPhysicalAddress = BackendView->HostPhysicalAddress;
  candidate.ArenaGpuAddress = BackendView->GpuVirtualAddress;
  candidate.ArenaBytes = template_bytes;
  candidate.Ready = APPLE_AGX_TRUE;
  *Image = candidate;
  return APPLE_AGX_TRUE;
}

void AdmissionBackendImageReset(ADMISSION_BACKEND_IMAGE *Image) {
  if (Image != ADMISSION_BACKEND_IMAGE_NULL)
    AdmissionBackendImageZero(Image);
}

#undef ADMISSION_BACKEND_IMAGE_NULL
