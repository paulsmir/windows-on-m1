#ifndef AGX_WIN32_ASAHI_CAPTURE_H
#define AGX_WIN32_ASAHI_CAPTURE_H
#include "agx_win32_asahi_bo.h"

typedef struct {
  AGX_WIN32_RELOC_CAPTURE Capture;
  AGX_WIN32_ASAHI_BACKEND *Backend;
  APPLE_AGX_U32 Count;
  struct agx_bo *Bos[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
  AGX_WIN32_RELOC_ALLOCATION Identities[APPLE_AGX_WIN32_COMMAND_MAX_REFERENCES];
} AGX_WIN32_ASAHI_CAPTURE;

/* Per-request table, not device-lifetime slots. Caller holds the native BO
 * while registering it; capture takes its own reference until abort/retire. */
AGX_WIN32_RELOC_RESULT AgxWin32AsahiCaptureBegin(AGX_WIN32_ASAHI_CAPTURE *,
    AGX_WIN32_ASAHI_BACKEND *,APPLE_AGX_U64 Owner,APPLE_AGX_U32 Generation,
    APPLE_AGX_U64 Request);
AGX_WIN32_RELOC_RESULT AgxWin32AsahiCaptureReference(AGX_WIN32_ASAHI_CAPTURE *,
    struct agx_bo *,APPLE_AGX_U32 Role,APPLE_AGX_U32 Access,
    APPLE_AGX_U64 Offset,APPLE_AGX_U64 Bytes,APPLE_AGX_U32 *Index);
AGX_WIN32_RELOC_RESULT AgxWin32AsahiCaptureAddress(AGX_WIN32_ASAHI_CAPTURE *,
    APPLE_AGX_U64 Address,APPLE_AGX_U64 Bytes,APPLE_AGX_U32 Role,
    APPLE_AGX_U32 Access,APPLE_AGX_U32 *Index);
#endif
