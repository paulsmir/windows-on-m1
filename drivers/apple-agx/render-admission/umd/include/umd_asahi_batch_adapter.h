#ifndef ADMISSION_UMD_ASAHI_BATCH_ADAPTER_H
#define ADMISSION_UMD_ASAHI_BATCH_ADAPTER_H

#include <windows.h>
#include "umd_draw_composer.h"

typedef enum _ADMISSION_UMD_ASAHI_BATCH_PHASE {
  AdmissionAsahiBatchEmpty, AdmissionAsahiBatchSealed,
  AdmissionAsahiBatchSubmitted, AdmissionAsahiBatchRetired,
  AdmissionAsahiBatchRejected
} ADMISSION_UMD_ASAHI_BATCH_PHASE;

/* Caller-serialized bridge: Capture retains native source BOs and Submission
 * retains Windows allocations. Both are released only after one fence retires.
 * Submitted means Render was entered, including a missing completion marker;
 * only Retire may retry that marker. Storage and capture remain caller-owned,
 * stable and unmodified until Abort/Retire succeeds. */
typedef struct _ADMISSION_UMD_ASAHI_BATCH {
  ADMISSION_UMD_ASAHI_BATCH_PHASE Phase;
  AGX_WIN32_RELOC_CAPTURE *Capture;
  APPLE_AGX_U64 Owner, RequestId;
  APPLE_AGX_U32 Generation;
  ADMISSION_UMD_DRAW_SUBMISSION Submission;
} ADMISSION_UMD_ASAHI_BATCH;

struct _ADMISSION_UMD_DEVICE;
#if defined(__cplusplus)
extern "C" {
#endif
HRESULT AdmissionUmdAsahiBatchSeal(struct _ADMISSION_UMD_DEVICE *,
    AGX_WIN32_RELOC_CAPTURE *, APPLE_AGX_U64, APPLE_AGX_U16,
    const APPLE_AGX_WIN32_DRAW_PAYLOAD *, ADMISSION_UMD_ASAHI_BATCH *);
HRESULT AdmissionUmdAsahiBatchDispatch(struct _ADMISSION_UMD_DEVICE *,
    ADMISSION_UMD_ASAHI_BATCH *);
HRESULT AdmissionUmdAsahiBatchRetire(struct _ADMISSION_UMD_DEVICE *,
    ADMISSION_UMD_ASAHI_BATCH *, DWORD);
HRESULT AdmissionUmdAsahiBatchAbort(struct _ADMISSION_UMD_DEVICE *,
    ADMISSION_UMD_ASAHI_BATCH *);
#if defined(__cplusplus)
}
#endif
#endif
