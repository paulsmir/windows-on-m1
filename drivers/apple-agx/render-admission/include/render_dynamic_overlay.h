#ifndef APPLE_AGX_RENDER_DYNAMIC_OVERLAY_H
#define APPLE_AGX_RENDER_DYNAMIC_OVERLAY_H

#include "apple_agx_dynamic_job.h"
#include "render_backend_image.h"

#define ADMISSION_DYNAMIC_OVERLAY_MAGIC 0x4f444741u /* AGDO */
#define ADMISSION_DYNAMIC_OVERLAY_VERSION 1u
#define ADMISSION_DYNAMIC_OVERLAY_MAX_ENTRIES 9u

typedef enum _ADMISSION_DYNAMIC_OVERLAY_RESULT {
  AdmissionDynamicOverlaySuccess = 0,
  AdmissionDynamicOverlayArgument,
  AdmissionDynamicOverlayLayout,
  AdmissionDynamicOverlayRange,
  AdmissionDynamicOverlayState,
  AdmissionDynamicOverlayOccupied,
  AdmissionDynamicOverlayContent,
} ADMISSION_DYNAMIC_OVERLAY_RESULT;

typedef struct _ADMISSION_DYNAMIC_OVERLAY_ENTRY {
  APPLE_AGX_U32 ReferenceIndex;
  APPLE_AGX_U32 Role;
  APPLE_AGX_U32 ObjectIndex;
  APPLE_AGX_U32 ObjectOffset;
  APPLE_AGX_U64 SourceOffset;
  APPLE_AGX_U32 Bytes;
  APPLE_AGX_U32 Reserved;
  APPLE_AGX_U64 GpuVirtualAddress;
} ADMISSION_DYNAMIC_OVERLAY_ENTRY;

typedef struct _ADMISSION_DYNAMIC_OVERLAY_PLAN {
  APPLE_AGX_U32 Magic;
  APPLE_AGX_U32 Version;
  APPLE_AGX_U32 Generation;
  APPLE_AGX_U32 EntryCount;
  ADMISSION_DYNAMIC_OVERLAY_ENTRY
      Entries[ADMISSION_DYNAMIC_OVERLAY_MAX_ENTRIES];
} ADMISSION_DYNAMIC_OVERLAY_PLAN;

typedef struct _ADMISSION_DYNAMIC_OVERLAY_STATE {
  APPLE_AGX_U32 Magic;
  APPLE_AGX_U32 Version;
  APPLE_AGX_U32 Applied;
  APPLE_AGX_U32 Fence;
  APPLE_AGX_U32 Generation;
  APPLE_AGX_U32 EntryCount;
  APPLE_AGX_U32 Reserved[2];
  APPLE_AGX_U64 MaterializedHash;
} ADMISSION_DYNAMIC_OVERLAY_STATE;

void AdmissionDynamicOverlayStateInitialize(
    ADMISSION_DYNAMIC_OVERLAY_STATE *State);
ADMISSION_DYNAMIC_OVERLAY_RESULT AdmissionDynamicOverlayPlan(
    const ADMISSION_BACKEND_IMAGE *Image,
    const APPLE_AGX_WIN32_COMMAND_VIEW *View,
    ADMISSION_DYNAMIC_OVERLAY_PLAN *Plan);
ADMISSION_DYNAMIC_OVERLAY_RESULT AdmissionDynamicOverlayResolve(
    const ADMISSION_DYNAMIC_OVERLAY_PLAN *Plan,
    APPLE_AGX_U32 ReferenceIndex, APPLE_AGX_U64 ReferenceOffset,
    APPLE_AGX_U32 Bytes, APPLE_AGX_U64 *GpuVirtualAddress);
ADMISSION_DYNAMIC_OVERLAY_RESULT AdmissionDynamicOverlayApply(
    ADMISSION_BACKEND_IMAGE *Image,
    const ADMISSION_DYNAMIC_OVERLAY_PLAN *Plan,
    const APPLE_AGX_DYNAMIC_JOB *Job, const void *Storage,
    APPLE_AGX_U32 StorageBytes, APPLE_AGX_U32 Fence,
    ADMISSION_DYNAMIC_OVERLAY_STATE *State);
ADMISSION_DYNAMIC_OVERLAY_RESULT AdmissionDynamicOverlayRelease(
    ADMISSION_BACKEND_IMAGE *Image,
    const ADMISSION_DYNAMIC_OVERLAY_PLAN *Plan,
    const APPLE_AGX_DYNAMIC_JOB *Job, const void *Storage,
    APPLE_AGX_U32 StorageBytes, APPLE_AGX_U32 Fence,
    ADMISSION_DYNAMIC_OVERLAY_STATE *State);

#endif
