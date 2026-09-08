#ifndef APPLE_AGX_RENDER_BACKEND_IMAGE_H
#define APPLE_AGX_RENDER_BACKEND_IMAGE_H

#include "render_memory.h"
#include "render_submission.h"
#include "apple_agx_exp208_gdi.h"
#include "apple_agx_exp208_adapter.h"
#include "apple_agx_exp208_dynamic.h"
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
  APPLE_AGX_U32 ArenaCapacity;
  APPLE_AGX_EXP208_GDI_BINDING Binding;
  APPLE_AGX_EXP208_DYNAMIC_RESULT Dynamic;
  APPLE_AGX_BACKEND_JOB_IMAGE Job;
  APPLE_AGX_U32 Sequence;
  APPLE_AGX_U32 BoundFence;
  APPLE_AGX_U32 JobFence;
  APPLE_AGX_BOOL JobReady;
  APPLE_AGX_BOOL Ready;
} ADMISSION_BACKEND_IMAGE;

typedef struct _ADMISSION_BACKEND_OUTPUT_VIEW {
  void *CpuAddress;
  APPLE_AGX_U64 GpuAddress;
  APPLE_AGX_U64 PhysicalAddress;
  APPLE_AGX_U32 Bytes;
  APPLE_AGX_U32 TargetBytes;
  APPLE_AGX_U32 ExpectedColor;
  APPLE_AGX_BOOL Framebuffer;
} ADMISSION_BACKEND_OUTPUT_VIEW;

APPLE_AGX_BOOL AdmissionBackendImagePrepare(
    ADMISSION_BACKEND_IMAGE *Image,
    const ADMISSION_LOCAL_MEMORY_VIEW *BackendView);

APPLE_AGX_BOOL AdmissionBackendImageBindSubmission(
    ADMISSION_BACKEND_IMAGE *Image,
    const ADMISSION_RENDER_PACKET_DESCRIPTION *Packet,
    void *DestinationCpuAddress,
    const unsigned char *SubmissionBytes,
    APPLE_AGX_U32 SubmissionByteCount,
    APPLE_AGX_EXP208_GDI_BINDING *Binding);

APPLE_AGX_BOOL AdmissionBackendImageCaptureOutput(
    const ADMISSION_BACKEND_IMAGE *Image, APPLE_AGX_U32 Fence,
    ADMISSION_BACKEND_OUTPUT_VIEW *Output);

APPLE_AGX_BOOL AdmissionBackendImageReleaseSubmission(
    ADMISSION_BACKEND_IMAGE *Image, APPLE_AGX_U32 Fence);

APPLE_AGX_BOOL AdmissionBackendImageRestartQueueLifetime(
    ADMISSION_BACKEND_IMAGE *Image);

APPLE_AGX_BOOL AdmissionBackendImageStageJob(
    ADMISSION_BACKEND_IMAGE *Image, APPLE_AGX_U32 Fence,
    APPLE_AGX_U32 TaEvent, APPLE_AGX_U32 D3Event,
    APPLE_AGX_U32 TaExpectedDonePointer,
    APPLE_AGX_U32 D3ExpectedDonePointer,
    APPLE_AGX_BOOL IncludeInitBm,
    APPLE_AGX_BACKEND_JOB_IMAGE *Job);

void AdmissionBackendImageReset(ADMISSION_BACKEND_IMAGE *Image);

#endif /* APPLE_AGX_RENDER_BACKEND_IMAGE_H */
