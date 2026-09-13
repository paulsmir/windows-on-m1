#ifndef AGX_WIN32_ASAHI_PIPELINE_H
#define AGX_WIN32_ASAHI_PIPELINE_H
#include "agx_win32_asahi_capture.h"

/* Stack-local emission scope, not shared Mesa/GPU ABI. All calls are on the
 * existing caller-serialized native producer thread. No runtime submit here. */
struct _AGX_WIN32_ASAHI_ENCODER_ROOT;
typedef struct {
  AGX_WIN32_ASAHI_CAPTURE *Capture;
  unsigned char *Cpu;
  APPLE_AGX_U32 Capacity,Reference,Role,Failed;
  void *PreviousEmission;
  struct _AGX_WIN32_ASAHI_ENCODER_ROOT *Root;
  APPLE_AGX_U32 RootOffset;
} AGX_WIN32_ASAHI_PIPELINE;
/* Zero-initialized, caller-owned batch/request storage, never a draw-local
 * variable. Begin takes the sole Encoder capture reference and leaves detached.
 * Enter/Leave bracket native calls; all borrowed children must finish before
 * Leave. Finalize requires detached state and the exact end including native
 * termination bytes. Capture abort/retire owns the BO hold, not this structure.
 * Caller must finalize before Seal and preserve/freeze bytes until consumption. */
typedef struct _AGX_WIN32_ASAHI_ENCODER_ROOT {
  AGX_WIN32_ASAHI_PIPELINE Scope;
  AGX_WIN32_RELOC_ALLOCATION Identity;
  APPLE_AGX_U64 Request, Address, AllocationOffset;
  APPLE_AGX_U32 CompletedEnd, Finalized;
} AGX_WIN32_ASAHI_ENCODER_ROOT;
int AgxWin32AsahiEncoderRootBegin(struct agx_device *,void *,APPLE_AGX_U64,
    APPLE_AGX_U32,AGX_WIN32_ASAHI_ENCODER_ROOT *);
int AgxWin32AsahiEncoderRootEnter(struct agx_device *,void *,APPLE_AGX_U64,
    APPLE_AGX_U32,AGX_WIN32_ASAHI_ENCODER_ROOT *);
int AgxWin32AsahiEncoderRootLeave(AGX_WIN32_ASAHI_ENCODER_ROOT *);
int AgxWin32AsahiEncoderRootFinalize(AGX_WIN32_ASAHI_ENCODER_ROOT *,const void *);
/* Native draw reserve before agx_ensure_cmdbuf_has_space: includes its link
 * length and 0x800 overread allowance, rejects rollover before alloc/jump. An
 * active Windows capture requires an entered root; ordinary native use passes. */
int AgxWin32AsahiEncoderDrawPreflight(struct agx_device *,struct agx_bo *,
    const void *Current,const void *End,APPLE_AGX_U64 DrawBytes);
/* Encoder emission borrows the entered root when present; otherwise the
 * explicit controlled standalone interval path keeps generic overlap checks. */
int AgxWin32AsahiEncoderEmissionBeginCpu(struct agx_device *,void *,APPLE_AGX_U32,
    AGX_WIN32_ASAHI_PIPELINE *);
int AgxWin32AsahiCaptureActivate(AGX_WIN32_ASAHI_CAPTURE *);
int AgxWin32AsahiCaptureDeactivate(AGX_WIN32_ASAHI_CAPTURE *);
int AgxWin32AsahiPipelineBegin(struct agx_device *,void *,APPLE_AGX_U64,
    APPLE_AGX_U32,AGX_WIN32_ASAHI_PIPELINE *);
/* Generic source-defined emission interval. This is intentionally not a
 * command parser: callers provide role/address/range at the emitting site. */
int AgxWin32AsahiEmissionBegin(struct agx_device *,void *,APPLE_AGX_U64,
    APPLE_AGX_U32,APPLE_AGX_U32,AGX_WIN32_ASAHI_PIPELINE *);
int AgxWin32AsahiEmissionBeginCpu(struct agx_device *,void *,APPLE_AGX_U32,
    APPLE_AGX_U32,AGX_WIN32_ASAHI_PIPELINE *);
void AgxWin32AsahiPipelineRecord(AGX_WIN32_ASAHI_PIPELINE *,const void *End,
    APPLE_AGX_U32 Kind,APPLE_AGX_U64 Target,APPLE_AGX_U64 Bytes,
    APPLE_AGX_U32 Role);
/* A completed native USC/PPP interval supplies its exact recorded length.
 * Never infer a span from the next pool allocation or the containing BO. */
void AgxWin32AsahiPipelineRecordCaptured(AGX_WIN32_ASAHI_PIPELINE *,const void *End,
    APPLE_AGX_U32 Kind,APPLE_AGX_U64 Target,APPLE_AGX_U32 Role);
void AgxWin32AsahiPipelineRecordRange(AGX_WIN32_ASAHI_PIPELINE *,const void *End,
    APPLE_AGX_U32 Kind,APPLE_AGX_U64 Target,APPLE_AGX_U64 Bytes,
    APPLE_AGX_U32 Role);
struct agx_batch;
/* Defined alongside native state by the full lifecycle source projection. */
int AgxWin32AsahiCaptureUniformBlock(struct agx_batch *,void *,APPLE_AGX_U64,
    APPLE_AGX_U32 Table);
int AgxWin32AsahiPipelineFinish(AGX_WIN32_ASAHI_PIPELINE *,const void *End);
#endif
