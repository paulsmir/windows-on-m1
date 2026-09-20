#ifndef AGX_WIN32_ASAHI_BATCH_H
#define AGX_WIN32_ASAHI_BATCH_H
#include "agx_win32_asahi_pipeline.h"
#include <stdint.h>

struct agx_batch;
struct agx_context;
struct pipe_draw_info;
struct pipe_draw_start_count_bias;
struct pipe_draw_indirect_info;
struct drm_asahi_cmd_render;
/* Runtime-owned adapter transaction. These callbacks reuse the UMD composer;
 * no command submission, GPU allocation or Linux synchronization lives here. */
typedef struct {
  void *(*Create)(void *Owner, APPLE_AGX_U64 *Request);
  int32_t (*Submit)(void *Owner,void *Transaction,AGX_WIN32_RELOC_CAPTURE *,
      const APPLE_AGX_WIN32_DRAW_PAYLOAD *,const APPLE_AGX_WIN32_NATIVE_BATCH_METADATA *,int *Entered);
  int32_t (*Retire)(void *Owner,void *Transaction,APPLE_AGX_U32 Timeout);
  int (*Destroy)(void *Owner,void *Transaction);
} AGX_WIN32_ASAHI_BATCH_OPS;

typedef struct {
  AGX_WIN32_ASAHI_CAPTURE Capture;
  AGX_WIN32_ASAHI_ENCODER_ROOT Root;
  AGX_WIN32_ASAHI_ENCODER_ROOT ComputeRoot;
  APPLE_AGX_WIN32_DRAW_PAYLOAD Draw;
  APPLE_AGX_WIN32_NATIVE_BATCH_METADATA Render;
  struct agx_batch *Native;
  const AGX_WIN32_ASAHI_BATCH_OPS *Ops;
  void *Owner,*Transaction;
  APPLE_AGX_U64 Request;
  int32_t Status;
  unsigned Entered,ComputeEntered,ComputePrepared,Submitted,Retired,Rejected,DrawCount;
} AGX_WIN32_ASAHI_BATCH;

int AgxWin32AsahiBatchConfigure(AGX_WIN32_ASAHI_BACKEND *,const AGX_WIN32_ASAHI_BATCH_OPS *,void *);
void AgxWin32AsahiBatchTraceDraw(struct agx_context *, struct agx_batch *, unsigned);
int AgxWin32AsahiBatchBegin(struct agx_batch *);
int AgxWin32AsahiBatchEnter(struct agx_batch *);
int AgxWin32AsahiBatchLeave(struct agx_batch *);
int AgxWin32AsahiBatchComputeEnter(struct agx_batch *);
int AgxWin32AsahiBatchComputeLeave(struct agx_batch *);
int AgxWin32AsahiBatchComputeFinalize(struct agx_batch *,const void *);
int AgxWin32AsahiBatchDrawAllowed(struct agx_context *,const struct pipe_draw_info *,
    unsigned,const struct pipe_draw_indirect_info *,const struct pipe_draw_start_count_bias *,unsigned);
int AgxWin32AsahiBatchFinish(struct agx_batch *,const struct drm_asahi_cmd_render *);
int AgxWin32AsahiBatchPoll(struct agx_batch *,APPLE_AGX_U32);
int AgxWin32AsahiBatchAbort(struct agx_batch *);
int AgxWin32AsahiBatchRelease(struct agx_batch *);
#endif
