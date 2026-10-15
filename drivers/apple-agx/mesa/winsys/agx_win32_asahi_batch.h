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
int AgxWin32AsahiBatchPrepareDraw(struct agx_batch *,const struct pipe_draw_info *,
    const struct pipe_draw_start_count_bias *);
int AgxWin32AsahiBatchLeave(struct agx_batch *);
int AgxWin32AsahiBatchComputeEnter(struct agx_batch *);
int AgxWin32AsahiBatchComputeLeave(struct agx_batch *);
int AgxWin32AsahiBatchComputeFinalize(struct agx_batch *,const void *);
/* D3D-G10a: the screen renders this colour format (the batch gate). */
int AgxWin32AsahiRenderableFormat(const struct agx_batch *batch, unsigned format);
int AgxWin32AsahiBatchDrawAllowed(struct agx_context *,const struct pipe_draw_info *,
    unsigned,const struct pipe_draw_indirect_info *,const struct pipe_draw_start_count_bias *,unsigned);
/* Called for a draw DrawAllowed refused before any batch work; returns
 * non-zero when the context must be marked faulted. */
int AgxWin32AsahiBatchDrawRefused(struct agx_context *,const struct pipe_draw_info *,
    const struct pipe_draw_indirect_info *,const struct pipe_draw_start_count_bias *,unsigned);
/* The compute command is non-NULL exactly when the batch has a CDM encoder. */
int AgxWin32AsahiBatchFinish(struct agx_batch *,const struct drm_asahi_cmd_render *,
                             const struct drm_asahi_cmd_compute *);
int AgxWin32AsahiBatchFinishCompute(struct agx_batch *,const struct drm_asahi_cmd_compute *);
int AgxWin32AsahiBatchPoll(struct agx_batch *,APPLE_AGX_U32);
void AgxWin32AsahiBatchDiagnostic(struct agx_batch *,APPLE_AGX_U32 out[2]);
int AgxWin32AsahiBatchAbort(struct agx_batch *);
int AgxWin32AsahiBatchRelease(struct agx_batch *);
void AgxWin32AsahiMarkContextFault(struct agx_context *,unsigned);
/* EXP1195: pipe fences. The completion fence of the device's newest
 * accepted submission (0: none), whether a completion fence has signalled
 * (non-blocking), and a wait for it (1 on success). */
APPLE_AGX_U64 AgxWin32AsahiNewestFence(struct agx_context *);
/* EXP1202 receipt-only: draws and microseconds per native draw phase
 * (1 batch/index setup, 2 shader variants, 3 descriptors/uploads,
 * 4 agx_encode_state, 5 VDM emission); read and reset. */
void AgxWin32AsahiDrawPhase(unsigned);
extern int AgxWin32AsahiDrawPhasesOn; /* EXP1206: marks are skipped while 0 */
void AgxWin32AsahiDrawPhases(APPLE_AGX_U32 out[6]);
int AgxWin32AsahiFenceSignalled(struct agx_device *,APPLE_AGX_U64);
int AgxWin32AsahiFenceWait(struct agx_device *,APPLE_AGX_U64);
void AgxWin32AsahiMarkBatchFault(struct agx_context *,struct agx_batch *,unsigned);
#endif
