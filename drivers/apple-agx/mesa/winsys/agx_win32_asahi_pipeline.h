#ifndef AGX_WIN32_ASAHI_PIPELINE_H
#define AGX_WIN32_ASAHI_PIPELINE_H
#include "agx_win32_asahi_capture.h"

/* Stack-local emission scope, not shared Mesa/GPU ABI. All calls are on the
 * existing caller-serialized native producer thread. No runtime submit here. */
typedef struct {
  AGX_WIN32_ASAHI_CAPTURE *Capture;
  unsigned char *Cpu;
  APPLE_AGX_U32 Capacity,Reference,Role,Failed;
  void *PreviousEmission;
} AGX_WIN32_ASAHI_PIPELINE;
int AgxWin32AsahiCaptureActivate(AGX_WIN32_ASAHI_CAPTURE *);
int AgxWin32AsahiCaptureDeactivate(AGX_WIN32_ASAHI_CAPTURE *);
int AgxWin32AsahiPipelineBegin(struct agx_device *,void *,APPLE_AGX_U64,
    APPLE_AGX_U32,AGX_WIN32_ASAHI_PIPELINE *);
/* Generic source-defined emission interval. This is intentionally not a
 * command parser: callers provide role/address/range at the emitting site. */
int AgxWin32AsahiEmissionBegin(struct agx_device *,void *,APPLE_AGX_U64,
    APPLE_AGX_U32,APPLE_AGX_U32,AGX_WIN32_ASAHI_PIPELINE *);
void AgxWin32AsahiPipelineRecord(AGX_WIN32_ASAHI_PIPELINE *,const void *End,
    APPLE_AGX_U32 Kind,APPLE_AGX_U64 Target,APPLE_AGX_U64 Bytes,
    APPLE_AGX_U32 Role);
int AgxWin32AsahiPipelineFinish(AGX_WIN32_ASAHI_PIPELINE *,const void *End);
#endif
