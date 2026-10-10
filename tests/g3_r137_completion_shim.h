#include "apple_agx_backend_runtime.h"
typedef struct {
  ADMISSION_CONTEXT *Adapter;
  struct { unsigned TaComplete,D3Complete; } Backend;
  APPLE_AGX_COMPLETION_TRANSACTION Completion;
  ADMISSION_RENDER_CONTEXT *CompletionContext;
} ADMISSION_PLATFORM_RUNTIME;
typedef struct { ADMISSION_PLATFORM_RUNTIME *Runtime; ULONG Fence,Node,Engine; } ADMISSION_COMPLETION_NOTIFICATION;
enum { AdmissionRenderPacketActive=2, AppleAgxPreemptionWaitCurrentBoundary=1, DXGK_INTERRUPT_DMA_COMPLETED=1 };
static unsigned replay_active_fence,replay_notify_count,replay_sync_fail;
static unsigned AdmissionRenderPacketState(const REPLAY_PACKET *p) {return p->State;}
static unsigned AppleAgxSchedulerActiveFence(int *s,unsigned n,unsigned e) {(void)s;(void)n;(void)e;return replay_active_fence;}
static int AppleAgxSchedulerCompleteActiveFence(int *s,unsigned n,unsigned e,unsigned f) {(void)s;(void)n;(void)e;if(f!=replay_active_fence)return 0;replay_active_fence=0;return 1;}
static int AdmissionDynamicOverlayReleaseActive(ADMISSION_PLATFORM_RUNTIME *r,unsigned f) {(void)r;(void)f;return 1;}
static int AdmissionBackendImageReleaseSubmission(ADMISSION_BACKEND_IMAGE *i,unsigned f) {(void)f;memset(i,0,sizeof(*i));return 1;}
static int AdmissionBackendImageReleaseSubmissionRestore(ADMISSION_BACKEND_IMAGE *i,ADMISSION_BACKEND_IMAGE_SNAPSHOT *s,unsigned f) {(void)s;return AdmissionBackendImageReleaseSubmission(i,f);}
static int AdmissionRenderPacketComplete(REPLAY_PACKET *p,unsigned f) {if(p->Description.Fence!=f)return 0;memset(p,0,sizeof(*p));return 1;}
static int AppleAgxSchedulerPreemptionPhase(int *s) {(void)s;return 0;}
static int AppleAgxSchedulerObserveBoundaryCompletion(int *s,unsigned n,unsigned e,unsigned f) {(void)s;(void)n;(void)e;(void)f;return 0;}
#define AdmissionRenderCorrelationNotifyAtInterruptWindows(a,f,t,q) ((void)0)
#define AdmissionRenderCorrelationSynchronizeWindows(a,f,s,r) ((void)0)
#define AdmissionGdiReceiptCompleteWindows(a,f,s,r) ((void)0)
static NTSTATUS replay_sync(HANDLE h,BOOLEAN (*fn)(PVOID),PVOID arg,ULONG m,BOOLEAN *reported) {
  (void)h;(void)m;if(replay_sync_fail){*reported=FALSE;return STATUS_DEVICE_BUSY;}
  *reported=fn(arg);return STATUS_SUCCESS;
}
static VOID replay_notify(HANDLE h,const DXGKARGCB_NOTIFY_INTERRUPT_DATA *d) {(void)h;assert(d->DmaCompleted.SubmissionFenceId==41);++replay_notify_count;}
static BOOLEAN replay_queue_dpc(HANDLE h) {(void)h;return TRUE;}
