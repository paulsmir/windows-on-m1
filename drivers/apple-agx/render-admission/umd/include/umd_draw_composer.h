#ifndef ADMISSION_UMD_DRAW_COMPOSER_H
#define ADMISSION_UMD_DRAW_COMPOSER_H

#include "agx_win32_reloc_capture.h"

/* Internal, caller-serialized UMD API. No wire layout or capability change.
 * Storage stays at a stable address until Abort/Retire succeeds. */
typedef enum {
  AdmissionDrawEmpty, AdmissionDrawSealed, AdmissionDrawCalling,
  AdmissionDrawAccepted, AdmissionDrawPostError, AdmissionDrawRetired,
  AdmissionDrawRejected, AdmissionDrawSynchronizing, AdmissionDrawRetiring
} ADMISSION_UMD_DRAW_PHASE;

typedef struct _ADMISSION_UMD_DRAW_SUBMISSION {
  ADMISSION_UMD_DRAW_PHASE Phase;
  APPLE_AGX_U64 Owner, RequestId;
  APPLE_AGX_U32 Generation, Count, CommandBytes, Fence;
  HANDLE Context;
  HRESULT RenderStatus, PostStatus;
  BOOL ResidencyHeld;
  AGX_WIN32_RELOC_ALLOCATION Identities[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
  D3DDDI_ALLOCATIONLIST Allocations[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
  APPLE_AGX_U64 Command[APPLE_AGX_WIN32_COMMAND_MAX_BYTES / sizeof(APPLE_AGX_U64)];
} ADMISSION_UMD_DRAW_SUBMISSION;

struct _ADMISSION_UMD_DEVICE;
/* Identity sidecar has one entry per typed reference. Its old AllocationIndex
 * is ignored; the composer assigns fresh indices before BuildDrawVersion. */
HRESULT AdmissionUmdDrawSeal(struct _ADMISSION_UMD_DEVICE *Device,
    APPLE_AGX_U64 RequestId, APPLE_AGX_U16 Version,
    const AGX_WIN32_DRAW_REQUEST *Request,
    const AGX_WIN32_RELOC_ALLOCATION *Identities,
    ADMISSION_UMD_DRAW_SUBMISSION *Submission);
HRESULT AdmissionUmdDrawDispatch(struct _ADMISSION_UMD_DEVICE *Device,
    ADMISSION_UMD_DRAW_SUBMISSION *Submission);
HRESULT AdmissionUmdDrawAbort(struct _ADMISSION_UMD_DEVICE *Device,
    ADMISSION_UMD_DRAW_SUBMISSION *Submission);
/* A failed event enqueue may be retried here, but Render is NEVER replayed.
 * A timeout retains ownership; only a signalled marker permits retirement. */
HRESULT AdmissionUmdDrawRetire(struct _ADMISSION_UMD_DEVICE *Device,
    ADMISSION_UMD_DRAW_SUBMISSION *Submission, DWORD TimeoutMs,
    BOOL QuiescedRetirement);

#endif
