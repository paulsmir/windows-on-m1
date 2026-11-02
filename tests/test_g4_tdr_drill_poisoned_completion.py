"""rc97: the G4-r3 TDR drill reset must retire the drilled job (EXP2030).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2030 (drill 1, build-ahead off): DxgkDdiResetEngine twice, then
DxgkDdiResetFromTimeout, each returned c0000483 at once (quiesce = poll,
wait_ms 0) with backend_phase 3, provider_phase 1 and PollFailure
provider_poll / drain_events / drain guard 10 (ApplyEvent) / ingest guard 0;
Windows then bugchecked 0x116.

Cause: AdmissionResetQuiesceActive poisons the drilled job's context
(GpuvaG3Poisoned) before the quiescence poll. Since RC40 (6fbe9ce6, after
the last drill PASS EXP1303) every G4 job carries a timestamp publication,
and AdmissionGpuvaG3CompleteJobTimestamp refuses to publish into a poisoned
context, so AdmissionBackendComplete refused the job's completion. The event
batch was already acknowledged to the firmware, so every later poll retried
the same refused apply and failed at once: no reset could ever succeed.

INVARIANT: a fence whose GPU work finished is never withheld because its
context was poisoned (same contract as EXP997 for a poisoned process). The
completion skips the timestamp publication of a poisoned context (nothing is
written into it), retires the job, and the reset quiescence then ends with
the drilled fence as LastAbortedFence. An unpoisoned completion still
publishes the firmware end before it reports the fence (RC40).

The real AdmissionResetQuiesceActive, AdmissionPlatformRuntimeQuiesceActive,
AdmissionBackendComplete, AdmissionNotifyCompletionAtInterrupt,
AdmissionGpuTimestampEndNanoseconds, AdmissionG3PublishTimestampLocked and
AdmissionGpuvaG3CompleteJobTimestamp run with the real platform provider poll
and event drain, the real composer, the real backend runtime and the real
shared scheduler. Modeled: WDK services, the render packet container, the
G3 graph/PTE lookups, and the queue provider's message decode (one
observation per event message).
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest
from test_notifier_flight import retire_source

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'
RENDER = ROOT / 'drivers/apple-agx/render-admission'


def extract(source, name):
    match = None
    for match in re.finditer(
            r'(?m)^(?:_Use_decl_annotations_\s+)?(?:static\s+)?'
            r'[A-Za-z_][A-Za-z0-9_ ]*\**\s*\b' + name + r'\s*\([^;{]*?\)\s*\{',
            source, re.S):
        pass
    if match is None:
        raise AssertionError(f'missing production function {name}')
    depth, pos = 1, match.end()
    while depth:
        if source[pos] == '{':
            depth += 1
        elif source[pos] == '}':
            depth -= 1
        pos += 1
    return source[source.rfind('\n', 0, match.start()) + 1:pos]


class TdrDrillPoisonedCompletion(unittest.TestCase):
    def test_reset_retires_drilled_job_of_poisoned_context(self):
        backend = (RENDER / 'src/backend_platform_windows.c').read_text()
        gpuva = (RENDER / 'src/gpuva_g3_windows.c').read_text()
        scheduler = (RENDER / 'src/scheduler_windows.c').read_text()
        provider = (SHARED / 'src/apple_agx_platform_provider.c').read_text()
        notification = re.search(r'typedef struct _ADMISSION_COMPLETION_NOTIFICATION \{.*?'
                                 r'} ADMISSION_COMPLETION_NOTIFICATION;', backend, re.S)[0]
        program = '\n'.join(
            [SHIM, notification] +
            [extract(provider, n) for n in (
                'AppleAgxPlatformZero', 'AppleAgxPlatformCopy',
                'AppleAgxPlatformTransportValid', 'AppleAgxPlatformProviderDrainEvents',
                'AppleAgxPlatformProviderPoll')] +
            [extract(gpuva, n) for n in (
                'AdmissionG3PublishTimestampLocked', 'AdmissionGpuvaG3CompleteJobTimestamp')] +
            [extract(backend, n) for n in (
                'AdmissionGpuTimestampEndNanoseconds', 'AdmissionNotifyCompletionAtInterrupt',
                'AdmissionBackendComplete', 'AdmissionPostResumePollByte',
                'AdmissionPostResumePollFailure', 'AdmissionPlatformRuntimeQuiesceActive')] +
            [extract(scheduler, 'AdmissionResetQuiesceActive'), CASES])
        with tempfile.TemporaryDirectory(prefix='rc97-drill-') as tmp:
            path = Path(tmp) / 'drill.c'
            path.write_text(program)
            binary = Path(tmp) / 'drill'
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                            '-Werror', '-Wno-unused-function', '-Wno-unused-parameter',
                            '-fsanitize=address,undefined',
                            '-I', str(SHARED / 'include'), '-I', str(RENDER / 'include'),
                            str(path), str(SHARED / 'src/apple_agx_backend_runtime.c'),
                            str(SHARED / 'src/apple_agx_platform_composer.c'),
                            str(SHARED / 'src/apple_agx_scheduler.c'),
                            str(SHARED / 'src/apple_agx_submission.c'),
                            '-o', str(binary)], check=True)
            result = subprocess.run([str(binary)], capture_output=True, text=True, timeout=20)
            if result.returncode:
                raise AssertionError(result.stdout + result.stderr)

    def test_reset_poisons_before_quiescence(self):
        # The replay above relies on this order: the drilled context is
        # poisoned before its completion can be consumed by the reset poll.
        quiesce = extract((RENDER / 'src/scheduler_windows.c').read_text(),
                          'AdmissionResetQuiesceActive')
        self.assertLess(quiesce.index('owner->GpuvaG3Poisoned = TRUE'),
                        quiesce.index('AdmissionPlatformRuntimeQuiesceActive('))


SHIM = retire_source() + r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_scheduler.h"
#include "apple_agx_submission.h"
#include "apple_agx_backend_runtime.h"
#include "apple_agx_platform_provider.h"
#include "apple_agx_exp208_relocation.h"
#include "j313_agx_g2.generated.h"
#include "render_gpu_fault.h"
#include "render_job_window.h"
#include "render_rc_progress.h"
/* rc102: the receipt of the wired depth-2 window (no successor here). */
void AdmissionJobWindowReceiptReported(ADMISSION_JOB_WINDOW_RECEIPT *Receipt,
                                       unsigned int Count) { (void)Receipt; (void)Count; }
#define APPLE_AGX_GPUVA_G3_QUALIFICATION 1
#define PLATFORM_NULL ((void *)0)
#define _Use_decl_annotations_
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000Du)
#define STATUS_DEVICE_BUSY ((NTSTATUS)0x80000011u)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xC0000184u)
#define STATUS_DEVICE_HARDWARE_ERROR ((NTSTATUS)0xC0000483u)
#define STATUS_IO_TIMEOUT ((NTSTATUS)0xC00000B5u)
#define NT_SUCCESS(x) ((NTSTATUS)(x) >= 0)
#define PASSIVE_LEVEL 0
#define Executive 0
#define KernelMode 0
#define IO_NO_INCREMENT 0
#define RtlZeroMemory(p, n) memset((p), 0, (n))
#define RtlCopyMemory memcpy
#define ADMISSION_RESET_QUIESCE_TIMEOUT_MS 1000u
#define ADMISSION_FORCE_CHANNEL_AFTER_MS 100u
#define DXGK_INTERRUPT_DMA_COMPLETED 1
#define DXGK_INTERRUPT_DMA_PAGE_FAULTED 9
#define DXGK_RENDER_PIPELINE_STAGE_UNKNOWN 0
#define DXGK_PAGE_FAULT_WRITE 1
#define AdmissionRenderPacketActive 2
#define AdmissionJobPhaseComplete 1
#define AdmissionJobPhaseJobEnd 2
#define AdmissionJobPhaseNotify 3
#define AdmissionJobSubReleased 7
#define APPLE_AGX_GPUVA_G3_VALID 1u
#define APPLE_AGX_GPUVA_G3_WRITE 2u
#define ADMISSION_MEMORY_LOCAL_SEGMENT 1u
#define ADMISSION_MEMORY_POOL_SEGMENT 3u
#define ADMISSION_G3_POISON(p, n) ((p)->Poisoned = 1)
#define APPLE_AGX_RTKIT_RUNTIME_DRAIN_LIMIT 8u
#define AdmissionGpuRecoverySnapshot(r, s) ((void)(r), memset((s), 0, sizeof(*(s))))
typedef void VOID;
typedef void *PVOID, *HANDLE;
typedef int BOOLEAN, KIRQL;
typedef int32_t LONG, NTSTATUS;
typedef int64_t LONGLONG, LONG64;
typedef unsigned UINT, ULONG, DXGK_PAGE_FAULT_FLAGS;
typedef uint64_t UINT64, ULONGLONG;
typedef uintptr_t ULONG_PTR;
typedef unsigned char UCHAR, *PUCHAR;
typedef size_t SIZE_T;
typedef union { LONGLONG QuadPart; } LARGE_INTEGER;
typedef struct { int Signaled; } KEVENT;
typedef int FAST_MUTEX;
typedef unsigned APPLE_AGX_RTKIT_U32;
typedef enum { AppleAgxRtkitSessionResultOk = 0 } APPLE_AGX_RTKIT_SESSION_RESULT;
typedef struct {
  unsigned InterruptType;
  struct { unsigned SubmissionFenceId, NodeOrdinal, EngineOrdinal; } DmaCompleted;
  struct {
    unsigned FaultedFenceId;
    UINT64 FaultedPrimitiveAPISequenceNumber;
    unsigned FaultedPipelineStage, FaultedBindTableEntry, PageFaultFlags;
    UINT64 FaultedVirtualAddress;
    unsigned NodeOrdinal, EngineOrdinal, PageTableLevel;
    struct { unsigned IsDeviceSpecificCode, DeviceSpecificCode; } FaultErrorCode;
    void *FaultedProcessHandle;
  } DmaPageFaulted;
} DXGKARGCB_NOTIFY_INTERRUPT_DATA;
/* G3 graph/PTE model (the RC40 replay owns the lookup coverage). */
typedef struct { ULONGLONG GuestIpa, Allocation; unsigned Flags, SegmentId; } APPLE_AGX_GPUVA_G3_LOGICAL_PTE;
typedef struct { void *CpuAddress; ULONGLONG GuestIpaAddress, Bytes; } ADMISSION_SCANOUT_MEMORY_VIEW;
typedef struct { int JobInFlight, Uncertain; ULONGLONG LeaseToken; } GRAPH;
typedef struct _ADMISSION_G3_PRIVATE_SCENE {
  struct _ADMISSION_G3_PRIVATE_SCENE *Next; unsigned Queued, Started, Fence, GpuDone;
} ADMISSION_G3_PRIVATE_SCENE;
typedef struct { GRAPH Graph; int Poisoned, CpuOnlyMappings; ADMISSION_G3_PRIVATE_SCENE *PrivateScenes; } ADMISSION_G3_PROCESS;
typedef struct {
  FAST_MUTEX Lock; KEVENT JobEvent; ADMISSION_G3_PROCESS *ActiveProcess; void *ActiveContext;
  ULONG ActiveFence, LastCompletedFence, PrivateCompletionFence;
  void *SuccessorContext; ULONG SuccessorFence; /* rc102: none here */
} ADMISSION_G3_STATE;
typedef struct _ADMISSION_RENDER_CONTEXT {
  struct { unsigned FenceOutstanding; } Object;
  BOOLEAN GpuvaG3Poisoned;
  void *GpuvaG3Process;
} ADMISSION_RENDER_CONTEXT;
typedef struct {
  unsigned State;
  struct { unsigned Fence; UINT64 ContextToken; } Description;
} PACKET;
typedef struct _ADMISSION_CONTEXT {
  BOOLEAN InterfaceValid;
  struct {
    void *DeviceHandle;
    void (*DxgkCbNotifyInterrupt)(void *, DXGKARGCB_NOTIFY_INTERRUPT_DATA *);
    BOOLEAN (*DxgkCbQueueDpc)(void *);
    NTSTATUS (*DxgkCbSynchronizeExecution)(void *, BOOLEAN (*)(PVOID), PVOID, unsigned, BOOLEAN *);
  } Interface;
  APPLE_AGX_SCHEDULER Scheduler;
  unsigned SchedulerLock, BackendImage, BackendSnapshot;
  PACKET RenderPacket;
  LONG RenderDpcFence, SchedulerDpcPending, SchedulerFaulted;
  void *GpuvaG3State;
  void *PlatformRuntime;
  ADMISSION_GPU_RESET_RECEIPT GpuReset;
  /* rc102: the pending head a kicked successor would be (none here) */
  struct { unsigned State, Fence; ADMISSION_RENDER_CONTEXT *Context; } G4Pending[2];
  ULONG G4PendingHead, G4PendingCount;
} ADMISSION_CONTEXT;
typedef struct {
  ADMISSION_CONTEXT *Adapter;
  APPLE_AGX_COMPLETION_TRANSACTION Completion;
  ADMISSION_RENDER_CONTEXT *CompletionContext;
  APPLE_AGX_BACKEND_RUNTIME Backend;
  APPLE_AGX_PLATFORM_PROVIDER Provider;
  unsigned LastFirmwareRecoveryKind;
  ADMISSION_GPU_FAULT_RECEIPT GpuFault;
  unsigned AheadReleasedFence, AheadFinishedFence;
  void *AheadManager;
  BOOLEAN AheadG4Native, AheadFinishedDropped;
  /* rc102 depth-2 window (none kicked in this replay) */
  unsigned Depth2KickedFence, Depth2BatchedFence, Depth2LastReported;
  ADMISSION_JOB_WINDOW_RECEIPT Depth2Receipt;
  APPLE_AGX_EXP208_RELOCATION_OBJECT QueueObjects[30];
  BOOLEAN BackendStarted, RecoverChannelOnStall;
  LONG Stopping, Resetting, FirmwareRecoveries;
  KEVENT WorkIdle;
  ADMISSION_GPU_POST_RESUME_RECEIPT PostResume;
  int Rtkit, AscIo;
  struct { APPLE_AGX_RENDER_SHARED_MEMORY_OWNER RenderSharedMemory; } Initdata;
  ADMISSION_RC_SAMPLE RcCurrent;
} ADMISSION_PLATFORM_RUNTIME;
/* Read-only GPU diagnostic is tested with real mapped words in test_render_rc_progress. */
static void AdmissionRcSampleWindows(ADMISSION_PLATFORM_RUNTIME *r, unsigned stage,
                                     unsigned fence, unsigned status) {
 (void)r; (void)stage; (void)fence; (void)status;
}

static unsigned notifications, lock_depth, end_calls, published;
static ULONGLONG now_ms = 506748u;
static DXGKARGCB_NOTIFY_INTERRUPT_DATA observed;
static ADMISSION_G3_STATE *live_state;
static APPLE_AGX_GPUVA_G3_LOGICAL_PTE pte;
static unsigned char local_bytes[0x2000];
static void KeAcquireSpinLock(unsigned *l, KIRQL *irql) { (void)l; *irql = 0; assert(!lock_depth++); }
static void KeReleaseSpinLock(unsigned *l, KIRQL irql) { (void)l; (void)irql; assert(lock_depth-- == 1); }
static LONG InterlockedExchange(LONG *p, LONG v) { LONG old = *p; *p = v; return old; }
static LONG InterlockedCompareExchange(LONG *p, LONG v, LONG c) { LONG old = *p; if (old == c) *p = v; return old; }
static UINT64 KeQueryInterruptTime(void) { return 1; }
static unsigned KeGetCurrentIrql(void) { return PASSIVE_LEVEL; }
static ULONGLONG AdmissionPlatformNowMs(void) { return now_ms; }
static NTSTATUS KeWaitForSingleObject(KEVENT *e, int r, int m, int a, LARGE_INTEGER *t) {
  (void)r; (void)m; (void)a; (void)t; assert(e->Signaled); return STATUS_SUCCESS; }
static NTSTATUS KeDelayExecutionThread(int m, int a, LARGE_INTEGER *t) { (void)m; (void)a; (void)t; ++now_ms; return 0; }
static LARGE_INTEGER KeQueryPerformanceCounter(LARGE_INTEGER *f) {
  if (f) f->QuadPart = 24000000; return (LARGE_INTEGER){1}; }
static void ExAcquireFastMutex(FAST_MUTEX *lock) { assert(!*lock); *lock = 1; }
static void ExReleaseFastMutex(FAST_MUTEX *lock) { assert(*lock); *lock = 0; }
static void KeMemoryBarrier(void) { assert(live_state->Lock); }
static void KeSetEvent(KEVENT *e, int i, int w) { (void)i; (void)w; e->Signaled = 1; }
static unsigned AdmissionRenderPacketState(PACKET *p) { return p->State; }
static int AdmissionRenderPacketComplete(PACKET *p, unsigned f) {
  assert(p->State == 2 && p->Description.Fence == f); p->State = 0; return 1; }
static void AdmissionContextRetireFence(ADMISSION_RENDER_CONTEXT *c, unsigned f) {
  assert(c->Object.FenceOutstanding == f); c->Object.FenceOutstanding = 0; }
static void AdmissionJobTimingMarkWindows(void *r, unsigned f, unsigned p) { (void)r; (void)f; (void)p; }
static void AdmissionJobTimingCpuWindows(void *r, unsigned f, int c) { (void)r; (void)f; (void)c; }
static void AdmissionGpuCompletionCaptureWindows(ADMISSION_PLATFORM_RUNTIME *r, unsigned f,
    APPLE_AGX_BACKEND_COMPLETION_STATUS s) { (void)r; (void)f; (void)s; }
static void AdmissionJobTimingFirmwareWindows(void *r, unsigned f) { (void)r; (void)f; }
static void AdmissionGpuPerfSampleWindows(void *r) { (void)r; }
static unsigned long long AdmissionJobQpc(void) { return 1u; }
static void AdmissionJobTimingSubAtWindows(void *r, unsigned f, unsigned s, unsigned long long q) {
  (void)r; (void)f; (void)s; (void)q; }
static int AdmissionSaveG4Manager(void *r, unsigned f, APPLE_AGX_BACKEND_COMPLETION_STATUS status) { (void)r; (void)f; (void)status; return 1; }
static int AdmissionDynamicOverlayReleaseActive(void *r, unsigned f) { (void)r; (void)f; return 1; }
static int AdmissionBackendImageReleaseSubmissionRestore(void *a, void *s, unsigned f) {
  (void)a; (void)s; (void)f; return 1; }
static int AdmissionGpuvaG3PrivateReported(void *a, void *c, unsigned f) { (void)a; (void)c; (void)f; return 1; }
static void AdmissionRenderCorrelationNotifyAtInterruptWindows(void *a, unsigned f, UINT64 t, BOOLEAN q) {
  (void)a; (void)f; (void)t; (void)q; }
static void AdmissionRenderCorrelationSynchronizeWindows(void *a, unsigned f, NTSTATUS s, BOOLEAN r) {
  (void)a; (void)f; (void)s; (void)r; }
static void AdmissionGdiReceiptCompleteWindows(void *a, unsigned f, ULONG s, BOOLEAN r) {
  (void)a; (void)f; (void)s; (void)r; }
static void AdmissionPublishFirmwareRecovery(ADMISSION_PLATFORM_RUNTIME *r) { (void)r; }
static void AdmissionCapturePostResume(ADMISSION_PLATFORM_RUNTIME *r, ULONG f, ULONG w,
    ADMISSION_GPU_POST_RESUME_RECEIPT *p) { (void)r; p->Fence = f; p->WaitMs = w; }
static void AdmissionPublishPostResume(ADMISSION_PLATFORM_RUNTIME *r) { (void)r; }
static APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionDrainRuntime(int *s, int *io,
    unsigned limit, APPLE_AGX_RTKIT_U32 *n) { (void)s; (void)io; (void)limit; *n = 0; return AppleAgxRtkitSessionResultOk; }
APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeForceChannelRecovery(
    APPLE_AGX_G13_QUEUE_RUNTIME *r, APPLE_AGX_BACKEND_U32 *issued) {
  (void)r; (void)issued; assert(0 && "RecoverChannel is opt-in and off"); return 0; }
/* The KMD's G3 lookups; translation and graph coverage live in the RC40 replay. */
static int AdmissionMemoryRuntimeLocalView(ADMISSION_CONTEXT *a, ADMISSION_SCANOUT_MEMORY_VIEW *v) {
  (void)a; *v = (ADMISSION_SCANOUT_MEMORY_VIEW){local_bytes, 0x100000, sizeof(local_bytes)}; return 0; }
static int AdmissionMemoryPoolView(ADMISSION_CONTEXT *a, ADMISSION_SCANOUT_MEMORY_VIEW *v) {
  (void)a; (void)v; return -1; }
static const APPLE_AGX_GPUVA_G3_LOGICAL_PTE *AdmissionG3CopyPte(const ADMISSION_G3_PROCESS *p, ULONGLONG va) {
  (void)p; (void)va; assert(live_state->Lock); return &pte; }
static int AppleAgxGpuvaG3GraphContainsRangeAccess(GRAPH *g, ULONGLONG va, unsigned b, int w) {
  (void)va; (void)b; (void)w; return g->JobInFlight; }
static int AppleAgxGpuvaG3GraphTranslateVa(GRAPH *g, ULONGLONG va, ULONGLONG *ipa) {
  (void)g; *ipa = 0x100000 + (va & 0xfffULL); return 1; }
static int AppleAgxGpuvaG3TableSpanWithinLocal(ULONGLONG base, ULONGLONG bytes, ULONGLONG at, ULONGLONG count) {
  return at >= base && at - base <= bytes && count <= bytes - (at - base); }
static int AppleAgxGpuvaG3GraphEndJob(GRAPH *g) {
  ULONGLONG value; assert(live_state->Lock); memcpy(&value, local_bytes + 8, 8);
  published = value != 0; ++end_calls; g->JobInFlight = 0; return 1; }
/* Firmware boot is not part of this replay (apple_agx_backend_runtime.c). */
void AppleAgxFirmwareInitialize(APPLE_AGX_FIRMWARE *f) { (void)f; }
APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareStart(APPLE_AGX_FIRMWARE *f, const APPLE_AGX_FIRMWARE_IO *io) {
  (void)f; (void)io; assert(0); return (APPLE_AGX_FIRMWARE_RESULT)0; }
APPLE_AGX_FIRMWARE_RESULT AppleAgxFirmwareRollback(APPLE_AGX_FIRMWARE *f, const APPLE_AGX_FIRMWARE_IO *io) {
  (void)f; (void)io; assert(0); return (APPLE_AGX_FIRMWARE_RESULT)0; }
/* Queue provider decode model: one observation per event message
 * (word 0 queue, 1 event, 2 stamp, 3 done pointer, 4 completed fence). */
APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderIngestEvent(APPLE_AGX_G13_QUEUE_PROVIDER *p,
    const unsigned char *m, APPLE_AGX_BACKEND_U32 bytes, APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *b) {
  APPLE_AGX_BACKEND_U32 w[5];
  assert(bytes == APPLE_AGX_G13_EVENT_MESSAGE_SIZE);
  memcpy(w, m, sizeof(w)); memset(b, 0, sizeof(*b));
  b->ObservationCount = 1u;
  b->Observations[0].Queue = (APPLE_AGX_BACKEND_QUEUE)w[0];
  b->Observations[0].Event = w[1]; b->Observations[0].Stamp = w[2];
  b->Observations[0].DonePointer = w[3];
  b->Observations[0].Status = AppleAgxBackendObservationComplete;
  b->CompletedFence = w[4];
  p->LastIngestGuard = AppleAgxG13QueueProviderIngestGuardOk;
  p->Phase = AppleAgxG13QueueProviderCreated;
  return APPLE_AGX_BACKEND_TRUE;
}
APPLE_AGX_BACKEND_BOOL AppleAgxG13QueueProviderCheckTimeout(APPLE_AGX_G13_QUEUE_PROVIDER *p,
    APPLE_AGX_BACKEND_U64 now, APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH *b) {
  (void)p; (void)now; memset(b, 0, sizeof(*b)); return APPLE_AGX_BACKEND_TRUE; }
static APPLE_AGX_BACKEND_BOOL t_flush(void *c, const void *a, APPLE_AGX_BACKEND_U32 n) { (void)c; (void)a; (void)n; return 1; }
static void t_barrier(void *c) { (void)c; }
static APPLE_AGX_BACKEND_BOOL t_publish(void *c, volatile APPLE_AGX_BACKEND_U32 *a, APPLE_AGX_BACKEND_U32 v) { (void)c; *a = v; return 1; }
static APPLE_AGX_BACKEND_BOOL t_read(void *c, const volatile APPLE_AGX_BACKEND_U32 *a, APPLE_AGX_BACKEND_U32 *v) { (void)c; *v = *a; return 1; }
static APPLE_AGX_BACKEND_BOOL t_doorbell(void *c, APPLE_AGX_BACKEND_U32 d) { (void)c; (void)d; return 1; }
static APPLE_AGX_BACKEND_BOOL t_quiesce(void *c, APPLE_AGX_BACKEND_U32 f) { (void)c; (void)f; return 1; }
static APPLE_AGX_BACKEND_U64 t_now(void *c) { (void)c; return now_ms * 24000u; }
static void notify(void *a, DXGKARGCB_NOTIFY_INTERRUPT_DATA *d) { (void)a; observed = *d; ++notifications; }
static BOOLEAN dpc(void *a) { (void)a; return TRUE; }
static NTSTATUS sync_callback(void *a, BOOLEAN (*fn)(PVOID), PVOID p, unsigned n, BOOLEAN *r) {
  (void)a; (void)n; assert(!lock_depth); *r = fn(p); return 0; }
'''

CASES = r'''
#define DRILL_FENCE 0x1d659u
#define TA_EVENT 0x21u
#define D3_EVENT 0x22u
static ADMISSION_CONTEXT a;
static ADMISSION_PLATFORM_RUNTIME r;
static ADMISSION_G3_STATE s;
static ADMISSION_G3_PROCESS process;
static ADMISSION_RENDER_CONTEXT drilled;
static unsigned char event_state[J313_AGX_G2_CHANNEL_STATE_STRIDE];
static unsigned char event_ring[APPLE_AGX_PLATFORM_EVENT_RING_ENTRY_COUNT * APPLE_AGX_G13_EVENT_MESSAGE_SIZE];
static ULONGLONG firmware_end = 24000001ULL; /* ticks: 1000000041 ns */
static unsigned char arena[64];

static APPLE_AGX_BACKEND_BOOL flush_cpu(void *c, const void *p, APPLE_AGX_BACKEND_U32 n) {
  (void)c; (void)p; (void)n; return 1; }

static void push(APPLE_AGX_BACKEND_U32 index, APPLE_AGX_BACKEND_U32 queue, APPLE_AGX_BACKEND_U32 event,
                 APPLE_AGX_BACKEND_U32 done, APPLE_AGX_BACKEND_U32 fence) {
  APPLE_AGX_BACKEND_U32 w[5] = {queue, event, 7u, done, fence};
  memcpy(event_ring + index * APPLE_AGX_G13_EVENT_MESSAGE_SIZE, w, sizeof(w));
}

/* EXP2030 at DxgkDdiResetEngine entry: drill 1 left job 0x1d659 with the
 * firmware (SchedulerFaulted 0x4132e); the job ended at once, so its TA and
 * 3D completions wait unread in the event ring; no worker; backend
 * Submitted (3), queue provider Created (1). */
static void setup(void) {
  APPLE_AGX_BACKEND_U32 *pointers = (APPLE_AGX_BACKEND_U32 *)event_state;
  memset(&a, 0, sizeof(a)); memset(&r, 0, sizeof(r)); memset(&s, 0, sizeof(s));
  memset(&process, 0, sizeof(process)); memset(&drilled, 0, sizeof(drilled));
  memset(local_bytes, 0, sizeof(local_bytes)); memset(event_state, 0, sizeof(event_state));
  notifications = lock_depth = end_calls = published = 0; now_ms = 506748u;
  a.InterfaceValid = 1; a.Interface.DxgkCbNotifyInterrupt = notify;
  a.Interface.DxgkCbQueueDpc = dpc; a.Interface.DxgkCbSynchronizeExecution = sync_callback;
  AppleAgxSchedulerInitialize(&a.Scheduler);
  a.Scheduler.CompletedFence = a.Scheduler.LastSubmittedFence = DRILL_FENCE - 1u;
  assert(AppleAgxSchedulerQueueFence(&a.Scheduler, 0, 0, DRILL_FENCE));
  assert(AppleAgxSchedulerActivateFence(&a.Scheduler, 0, 0, DRILL_FENCE));
  a.RenderPacket.State = AdmissionRenderPacketActive;
  a.RenderPacket.Description.Fence = DRILL_FENCE;
  a.RenderPacket.Description.ContextToken = (UINT64)(uintptr_t)&drilled;
  a.SchedulerFaulted = 0x4132e;
  a.GpuvaG3State = &s; a.PlatformRuntime = &r; live_state = &s;
  drilled.Object.FenceOutstanding = DRILL_FENCE; drilled.GpuvaG3Process = &process;
  s.ActiveProcess = &process; s.ActiveFence = DRILL_FENCE;
  process.Graph.JobInFlight = 1; process.Graph.LeaseToken = 1;
  pte = (APPLE_AGX_GPUVA_G3_LOGICAL_PTE){0x100000, 1, APPLE_AGX_GPUVA_G3_VALID | APPLE_AGX_GPUVA_G3_WRITE,
                                         ADMISSION_MEMORY_LOCAL_SEGMENT};

  r.Adapter = &a; r.BackendStarted = 1; r.WorkIdle.Signaled = 1;
  r.QueueObjects[29].Data = (unsigned char *)&firmware_end; r.QueueObjects[29].Size = sizeof(firmware_end);
  r.Backend.Phase = AppleAgxBackendRuntimeSubmitted;
  r.Backend.Io.Context = &r;
  r.Backend.Io.Complete = AdmissionBackendComplete;
  r.Backend.Io.Memory.FlushForCpu = flush_cpu;
  r.Backend.ArenaCpuAddress = arena; r.Backend.ArenaBytes = sizeof(arena);
  r.Initdata.RenderSharedMemory.NotifierFlight=1;
  r.Initdata.RenderSharedMemory.Flights[0].Fence=DRILL_FENCE;
  r.Initdata.RenderSharedMemory.Flights[0].Visible=1;
  r.Backend.PendingSubmission.Submission.Fence = DRILL_FENCE;
  r.Backend.PendingJob.Kind = AppleAgxBackendJobRender;
  r.Backend.PendingJob.TaEvent = TA_EVENT; r.Backend.PendingJob.D3Event = D3_EVENT;
  r.Backend.PendingJob.TaExpectedStamp = r.Backend.PendingJob.D3ExpectedStamp = 7u;
  r.Backend.PendingJob.TaExpectedDonePointer = 1206u;
  r.Backend.PendingJob.D3ExpectedDonePointer = 1206u;
  /* Every G4 render since RC40 asks the KMD to publish its fragment end. */
  r.Backend.PendingJob.TimestampPublishVa = 0x1234500008ULL;

  r.Provider.Initialized = 1;
  r.Provider.Runtime = &r.Backend;
  r.Provider.Composer.Initialized = 1;
  r.Provider.Composer.Runtime = &r.Backend;
  r.Provider.Composer.QueueProvider = &r.Provider.QueueProvider;
  r.Provider.QueueProvider.Phase = AppleAgxG13QueueProviderCreated;
  r.Provider.Transport = (APPLE_AGX_PLATFORM_TRANSPORT_IO){&r, t_flush, t_flush, t_barrier,
      t_publish, t_read, t_doorbell, t_quiesce, t_now};
  r.Provider.Channels.Event.StateCpuAddress = event_state;
  r.Provider.Channels.Event.RingCpuAddress = event_ring;
  pointers[APPLE_AGX_PLATFORM_CHANNEL_READ_POINTER_OFFSET / 4u] = 0x40u;
  pointers[APPLE_AGX_PLATFORM_CHANNEL_WRITE_POINTER_OFFSET / 4u] = 0x42u;
  push(0x40u, AppleAgxBackendQueueTa, TA_EVENT, 1206u, 0u);
  push(0x41u, AppleAgxBackendQueue3d, D3_EVENT, 1206u, DRILL_FENCE);
}

static NTSTATUS reset_quiesce(const char *entry) {
  NTSTATUS status;
  RtlZeroMemory(&a.GpuReset, sizeof(a.GpuReset));
  { ULONG unsubmitted = 0u;
    status = AdmissionResetQuiesceActive(&a, &unsubmitted);
    assert(unsubmitted == 0u); /* rc102: the drilled job reached the firmware */ }
  if (status != STATUS_SUCCESS)
    printf("RED %s: status %#x quiesce %u wait_ms %u poll_failure %#x backend_phase %u "
           "provider_phase %u poll_guard %u drain_guard %u ingest_guard %u packet %u\n",
           entry, (unsigned)status, a.GpuReset.Quiesce, a.GpuReset.WaitMs,
           r.PostResume.PollFailure, (unsigned)r.Backend.Phase,
           (unsigned)r.Provider.QueueProvider.Phase, r.Provider.LastPollGuard,
           r.Provider.LastDrainGuard, r.Provider.QueueProvider.LastIngestGuard,
           a.RenderPacket.State);
  return status;
}

int main(void) {
  APPLE_AGX_U32 aborted = 0u;
  ULONGLONG written = 0u;
  NTSTATUS first, second, third;
  int failed = 0;

  /* Control (RC40): a healthy context's completion publishes the firmware
   * end before Windows sees the fence. */
  setup();
  {
    APPLE_AGX_BACKEND_U32 drained = 0u, completed = 0u;
    assert(AppleAgxPlatformProviderPoll(&r.Provider, 64u, &drained, &completed));
    assert(drained == 2u && completed == DRILL_FENCE && published && end_calls == 1u);
    memcpy(&written, local_bytes + 8, 8);
    assert(written == 1000000041ULL);
    assert(notifications == 1u && observed.InterruptType == DXGK_INTERRUPT_DMA_COMPLETED);
  }

  /* EXP2030: ResetEngine, ResetEngine, ResetFromTimeout on the drilled job. */
  setup();
  first = reset_quiesce("ResetEngine#1");
  second = first == STATUS_SUCCESS ? STATUS_SUCCESS : reset_quiesce("ResetEngine#2");
  third = second == STATUS_SUCCESS ? STATUS_SUCCESS : reset_quiesce("ResetFromTimeout");
  failed = third != STATUS_SUCCESS;
  if (failed) {
    /* The exact receipt state: every wait fails at its first poll. */
    assert(a.GpuReset.Quiesce == AdmissionGpuResetQuiescePoll && a.GpuReset.WaitMs == 0u);
    assert(r.PostResume.PollFailure == ((ADMISSION_GPU_POST_RESUME_POLL_PROVIDER << 24) |
           (AppleAgxPlatformDrainGuardApplyEvent << 8) | AppleAgxPlatformPollGuardDrainEvents));
    assert(r.Backend.Phase == AppleAgxBackendRuntimeSubmitted);
    assert(r.Provider.QueueProvider.Phase == AppleAgxG13QueueProviderCreated);
    printf("the drilled completion was refused: the reset can never succeed\n");
    return 1;
  }
  assert(drilled.GpuvaG3Poisoned);
  assert(a.GpuReset.Quiesce == AdmissionGpuResetQuiesceEnded && a.GpuReset.WaitMs == 0u);
  assert(!r.PostResume.PollFailure);
  assert(r.Backend.Phase == AppleAgxBackendRuntimeReady);
  /* The job retired exactly once, and nothing was written into the
   * poisoned context. */
  assert(notifications == 1u && observed.InterruptType == DXGK_INTERRUPT_DMA_COMPLETED &&
         observed.DmaCompleted.SubmissionFenceId == DRILL_FENCE);
  assert(!r.Initdata.RenderSharedMemory.Flights[0].Fence);
  assert(end_calls == 1u && !published && s.LastCompletedFence == DRILL_FENCE && !s.ActiveProcess);
  memcpy(&written, local_bytes + 8, 8);
  assert(written == 0u);
  assert(a.RenderPacket.State == 0u && drilled.Object.FenceOutstanding == 0u);
  assert(AppleAgxSchedulerActiveFence(&a.Scheduler, 0, 0) == 0u);
  /* AdmissionResetEngineInternal's scheduler stage: Wom1GpuReset reports
   * the drilled fence as LastAbortedFence (the EXP1303 PASS receipt). */
  assert(AppleAgxSchedulerResetEngine(&a.Scheduler, 0, 0, &aborted));
  assert(aborted == DRILL_FENCE);
  /* A second reset of the same boot finds no active job. */
  assert(reset_quiesce("ResetEngine#2") == STATUS_SUCCESS && notifications == 1u);
  return 0;
}
'''

if __name__ == '__main__':
    unittest.main()
