"""Replay the KMD engine/adapter reset with an active AGX render job.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A reset failure must retain its exact gate in the current receipt and history;
a later success must not inherit that failure stage. EXP1938 additionally
requires RTKit notification errors to preserve DMA ownership: a firmware
crash, rejected mailbox message, and read failure must not fabricate an
aborted fence or free the active private scene.

WHAT REAL BUG: EXP1296 bugcheck 0x116 (TdrReason 9, engine reset promoted to
adapter reset): with a G4 render active, DxgkDdiResetEngine and
DxgkDdiResetFromTimeout returned STATUS_DEVICE_HARDWARE_ERROR even when the
firmware had ended the job -- the G3 private preflight refused every started
private scene and AdmissionPlatformRuntimeReset stopped the retained firmware
(RTKit) and then refused because it cannot restart it.

INVARIANTS: the reset succeeds once the firmware has ended the active job
through the completion path (natural completion or drop after Timeout/Fault),
reports that fence as aborted and faults its context; it never reports success
while the job is still owned by the firmware, and it never stops RTKit. A
paging packet queued behind that job never ran: after the reset the original
fence is accepted again exactly once as a resubmission (Microsoft, TDR in
Windows 8 and later, packets unaffected by engine reset); completed, aborted
and never-queued fences stay refused. A render that never ran (the queued
render packet, every G4 pending submission including a context's second job)
keeps its private scene preempted, so VidSch's resubmission with a new fence
and Resubmission set finds it (EXP1303: a cancelled scene refused it,
Wom1G4SubmitFailure branch 7 subsite 0x201); the aborted job's scene and a
cancelled scene stay refused.

The real reset functions are compiled with the real shared scheduler and
render-packet state machines and the real private-scene preempt/cancel
markers, G4 pending drop and resubmission lookup; the firmware/provider is a
scripted model of what AdmissionBackendComplete does to the adapter when a
job ends.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

from test_g4_submit_virtual_replay import function_body

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'
SHARED = ROOT / 'drivers/apple-agx/shared'


def noinline_body(source, name):
    match = re.search(r'static __declspec\(noinline\) NTSTATUS ' + name +
                      r'\(.*?^}', source, re.S | re.M)
    if not match:
        raise AssertionError(f'missing production function {name}')
    return match.group(0)


def optional_body(source, name):
    try:
        return function_body(source, name)
    except AssertionError:
        return ''


class TdrResetQuiesce(unittest.TestCase):
    def test_reset_succeeds_only_after_firmware_ends_the_active_job(self):
        self.run_replay(CASES, FIRMWARE_REPLAY)

    def run_replay(self, cases, firmware_replay, queue_runtime=None):
        scheduler = (RENDER / 'src/scheduler_windows.c').read_text()
        backend = (RENDER / 'src/backend_platform_windows.c').read_text()
        queue = (RENDER / 'src/work_queue_windows.c').read_text()
        gpuva = (RENDER / 'src/gpuva_g3_windows.c').read_text()
        header = (RENDER / 'include/render_admission.h').read_text()
        timeout = re.search(r'#define ADMISSION_RESET_QUIESCE_TIMEOUT_MS (\d+)u', header)
        packet = re.search(r'typedef struct _ADMISSION_CPU_PACKET.*?} ADMISSION_CPU_PACKET;',
                           header, re.S).group(0)
        pending = (re.search(r'#define ADMISSION_G4_PENDING_CAPACITY \d+u', header).group(0) +
                   '\n' + re.search(r'typedef enum _ADMISSION_G4_PENDING_STATE.*?'
                                    r'} ADMISSION_G4_PENDING_STATE;', header, re.S).group(0))
        force = re.search(r'#define ADMISSION_FORCE_CHANNEL_AFTER_MS (\d+)u', backend)
        body = ('#define ADMISSION_FORCE_CHANNEL_AFTER_MS ' +
                (force.group(1) if force else '100') + 'u\n' +
                SHIM.replace('/*TIMEOUT*/', timeout.group(1) if timeout else '1000')
                    .replace('/*PACKET*/', packet).replace('/*PENDING*/', pending) +
                '\n'.join(function_body(gpuva, n) for n in (
                    'AdmissionGpuvaG3PrivateCancel', 'AdmissionGpuvaG3PrivatePreempt',
                    'AdmissionG3PrivateSlotPreempted', 'AdmissionG3PrivateSlotCancelled',
                    'AdmissionG4FindPrivateScene', 'AdmissionG4FindPrivateResubmission',
                    'AdmissionG4PendingDropLocked')) + '\n' +
                function_body(queue, 'AdmissionCpuQueueSubmit') + '\n' +
                function_body(scheduler, 'AdmissionCpuQueueReleaseContextsLocked') + '\n' +
                optional_body(backend, 'AdmissionPostResumePollByte') + '\n' +
                optional_body(backend, 'AdmissionPostResumePollFailure') + '\n' +
                optional_body(backend, 'AdmissionPlatformRuntimeQuiesceActive') + '\n' +
                optional_body(scheduler, 'AdmissionResetQuiesceActive') + '\n' +
                noinline_body(scheduler, 'AdmissionResetEngineInternal') + '\n' +
                noinline_body(scheduler, 'AdmissionResetEngineRecorded') + '\n' +
                '\n'.join(function_body(scheduler, n) for n in (
                    'AdmissionResetEngineEntry', 'AdmissionDdiResetEngine',
                    'AdmissionDdiResetFromTimeout',
                    'AdmissionDdiRestartFromTimeout')) + cases)
        with tempfile.TemporaryDirectory() as tmp:
            program = Path(tmp) / 'reset.c'
            program.write_text(body)
            firmware = Path(tmp) / 'firmware.c'
            firmware.write_text(firmware_replay)
            binary = Path(tmp) / 'reset'
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                            '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                            '-I', str(RENDER / 'include'), '-I', str(SHARED / 'include'),
                            '-I', str(SHARED / 'tests'), str(firmware),
                            str(queue_runtime or SHARED / 'src/apple_agx_g13_queue_runtime.c'),
                            str(SHARED / 'src/apple_agx_g13_codec.c'),
                            str(program),
                            *[str(SHARED / 'src' / (name + '.c')) for name in (
                                'apple_agx_rtkit_session', 'apple_agx_rtkit_boot',
                                'apple_agx_rtkit', 'apple_agx_asc_transport',
                                'apple_agx_gfx_handoff')],
                            str(SHARED / 'src/apple_agx_scheduler.c'),
                            str(RENDER / 'src/render_submission.c'), '-o', str(binary)],
                           check=True)
            subprocess.run([str(binary)], check=True, timeout=20)


SHIM = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_scheduler.h"
#include "render_submission.h"
#include "render_gpu_fault.h"
#include "render_paging.h"
#include "render_present.h"
#include "apple_agx_g4_submit.h"
#include "apple_agx_g13_queue_runtime.h"
#include "apple_agx_rtkit_session.h"
#define APPLE_AGX_GPUVA_G3_QUALIFICATION 1
#define _Use_decl_annotations_
#define _ReturnAddress() ((void *)0)
#define __declspec(x)
#define PAGED_CODE() ((void)0)
#define UNREFERENCED_PARAMETER(x) ((void)(x))
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_TIMEOUT 0x102
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xC000000Du)
#define STATUS_DEVICE_BUSY ((NTSTATUS)0x80000011u)
#define STATUS_INVALID_DEVICE_STATE ((NTSTATUS)0xC0000184u)
#define STATUS_DEVICE_HARDWARE_ERROR ((NTSTATUS)0xC0000483u)
#define STATUS_DEVICE_NOT_READY ((NTSTATUS)0xC00000A3u)
#define STATUS_IO_TIMEOUT ((NTSTATUS)0xC00000B5u)
#define MAXULONG 0xffffffffu
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#define PASSIVE_LEVEL 0
#define Executive 0
#define KernelMode 0
#define ADMISSION_RESET_QUIESCE_TIMEOUT_MS /*TIMEOUT*/u
#define ADMISSION_SCHEDULER_NODE 0u
#define ADMISSION_SCHEDULER_ENGINE 0u
#define AppleAgxBackendRuntimeReady 2
#define AppleAgxBackendRuntimeSubmitted 3
#define AppleAgxBackendRuntimeFailed 6
#define RtlZeroMemory(p, n) memset((p), 0, (n))
#define RtlCopyMemory memcpy
#define ADMISSION_MAX_PAGING_RECORDS 64u
#define ADMISSION_CPU_PACKET_PAGING 1u
#define ADMISSION_CPU_PACKET_PRESENT 2u
#define ADMISSION_CPU_PACKET_NOP 3u
typedef void VOID;
typedef void *PVOID, *HANDLE;
typedef int32_t LONG, NTSTATUS;
typedef int64_t LONGLONG;
typedef uint32_t UINT, ULONG;
typedef unsigned long long ULONGLONG;
typedef uintptr_t ULONG_PTR;
typedef int BOOLEAN, KIRQL;
typedef unsigned char UCHAR;
typedef uint32_t APPLE_AGX_BACKEND_U32, APPLE_AGX_RTKIT_U32;
typedef union { LONGLONG QuadPart; } LARGE_INTEGER;
typedef struct { int Signaled; } KEVENT;
typedef struct _ADMISSION_RENDER_CONTEXT {
  struct { ULONG FenceOutstanding; } Object;
  ULONG GpuvaG3SecondFence;
  volatile LONG GpuvaG3PrivateFence, GpuvaG3CancelFence, GpuvaG3CancelUncertain,
      GpuvaG3PreemptFence;
  volatile LONG GpuvaG3PrivateFence2, GpuvaG3CancelFence2, GpuvaG3PreemptFence2;
  volatile LONG GpuvaG3CancelCaller, GpuvaG3CancelCaller2;
  BOOLEAN GpuvaG3Closing, GpuvaG3Poisoned;
  ULONGLONG GpuvaG3PrivateManagerGeneration;
} ADMISSION_RENDER_CONTEXT;
/* The scene and process fields the private-scene lookups read. */
typedef struct _ADMISSION_G3_PRIVATE_SCENE {
  struct _ADMISSION_G3_PRIVATE_SCENE *Next;
  ADMISSION_RENDER_CONTEXT *Context;
  struct { ULONGLONG Generation; } Storage;
  ULONG Fence, Submitting, Queued, Started, ReleaseRequested, Quarantined;
} ADMISSION_G3_PRIVATE_SCENE;
typedef struct {
  ADMISSION_G3_PRIVATE_SCENE *PrivateScenes;
  struct { ULONGLONG ProcessId; } Graph;
  struct { ULONGLONG Generation; } PrivateManager;
} ADMISSION_G3_PROCESS;
/*PACKET*/
/*PENDING*/
typedef struct {
  ADMISSION_G4_PENDING_STATE State;
  ULONG Fence;
  ADMISSION_RENDER_CONTEXT *Context;
} ADMISSION_G4_PENDING;
typedef struct { int Provider; APPLE_AGX_BACKEND_U32 LastPollGuard, LastDrainGuard;
  struct { APPLE_AGX_G13_QUEUE_RUNTIME Runtime; APPLE_AGX_BACKEND_U32 LastIngestGuard; } QueueProvider;
} APPLE_AGX_PLATFORM_PROVIDER;
typedef struct _ADMISSION_CONTEXT ADMISSION_CONTEXT;
typedef struct _ADMISSION_PLATFORM_RUNTIME {
  ADMISSION_CONTEXT *Adapter;
  KEVENT WorkIdle;
  volatile LONG WorkScheduled, WorkersActive, Stopping, Resetting;
  BOOLEAN BackendStarted;
  APPLE_AGX_BACKEND_RUNTIME Backend;
  APPLE_AGX_PLATFORM_PROVIDER Provider;
  APPLE_AGX_RTKIT_SESSION Rtkit;
  APPLE_AGX_ASC_IO AscIo;
  volatile LONG FirmwareRecoveries;
  BOOLEAN RecoverChannelOnStall;
  ADMISSION_GPU_POST_RESUME_RECEIPT PostResume;
  ULONG Depth2KickedFence; /* rc102: the successor kicked behind the job */
} ADMISSION_PLATFORM_RUNTIME;
struct _ADMISSION_CONTEXT {
  int SchedulerLock, PagingLock, BackendImage;
  volatile LONG SchedulerInitialized, SchedulerFaulted, RenderDpcFence;
  volatile LONG PagingPending, PagingWorkersActive, PagingDpcsActive;
  volatile LONG PagingStopping, PagingRecordsUnsubmitted;
  PVOID PagingWorkItem;
  KEVENT PagingIdle;
  ULONG CpuQueueHead, CpuQueueCount, DispatchedFence;
  ADMISSION_CPU_PACKET CpuQueue[APPLE_AGX_SCHEDULER_QUEUE_CAPACITY];
  APPLE_AGX_SCHEDULER Scheduler;
  ADMISSION_RENDER_PACKET RenderPacket;
  ADMISSION_G4_PENDING G4Pending[ADMISSION_G4_PENDING_CAPACITY];
  ULONG G4PendingHead, G4PendingCount;
  PVOID PlatformRuntime;
  ADMISSION_GPU_RESET_RECEIPT GpuReset;
};
typedef struct { UINT NodeOrdinal, EngineOrdinal, LastAbortedFenceId; } DXGKARG_RESETENGINE;
typedef struct {
  HANDLE hContext; UINT SubmissionFenceId;
  struct { UINT Paging : 1, Resubmission : 1; } Flags;
} DXGKARG_SUBMITCOMMAND;

static int lock_depth, rtkit_stops, polls, drop_at_poll, fail_poll, worker_busy,
    worker_completes, paused, private_resets, scene_queued, scene_started,
    scene_quarantined, reactivate_after_quiesce;
static int use_firmware_runtime;
void ReplayQueueSetup(unsigned int fence);
int ReplayQueuePoll(int poll);
static ULONGLONG now_ms;
static ADMISSION_CONTEXT *model_adapter;
static ADMISSION_PLATFORM_RUNTIME *model_runtime;
static ADMISSION_G3_PRIVATE_SCENE *model_active_scene;
static unsigned captures;
#define AdmissionGpuRecoverySnapshot CaptureRaw
#include "render_gpu_recovery_snapshot.h"
#undef AdmissionGpuRecoverySnapshot
static void AdmissionGpuRecoverySnapshot(const APPLE_AGX_G13_QUEUE_RUNTIME *r,
    ADMISSION_GPU_RECOVERY_SNAPSHOT *s) {
  assert(model_runtime->Resetting && model_runtime->WorkIdle.Signaled);
  assert(!lock_depth);
  ++captures;
  CaptureRaw(r, s);
}

static LONG InterlockedCompareExchange(volatile LONG *p, LONG v, LONG e) {
  LONG o = *p; if (o == e) *p = v; return o; }
static LONG InterlockedExchange(volatile LONG *p, LONG v) { LONG o = *p; *p = v; return o; }
static void KeClearEvent(KEVENT *e) { e->Signaled = 0; }
static LARGE_INTEGER KeQueryPerformanceCounter(PVOID f) {
  LARGE_INTEGER t; (void)f; t.QuadPart = (LONGLONG)now_ms; return t; }
static int dispatches;
static void AdmissionDispatchQueuedWork(ADMISSION_CONTEXT *c) { (void)c; assert(!lock_depth); ++dispatches; }
static void KeAcquireSpinLock(int *l, KIRQL *i) { (void)l; *i = 2; ++lock_depth; }
static void KeReleaseSpinLock(int *l, KIRQL i) { (void)l; (void)i; --lock_depth; }
static void KeAcquireSpinLockAtDpcLevel(int *l) { (void)l; ++lock_depth; }
static void KeReleaseSpinLockFromDpcLevel(int *l) { (void)l; --lock_depth; }
static int KeGetCurrentIrql(void) { return lock_depth ? 2 : PASSIVE_LEVEL; }
static ULONGLONG AdmissionPlatformNowMs(void) { return now_ms; }
static NTSTATUS KeDelayExecutionThread(int m, BOOLEAN a, LARGE_INTEGER *t) {
  (void)m; (void)a; assert(!lock_depth && t->QuadPart == -10000LL); ++now_ms; return 0; }
static void AdmissionContextRetireFence(ADMISSION_RENDER_CONTEXT *c, ULONG f) {
  if (c->Object.FenceOutstanding == f) {
    c->Object.FenceOutstanding = c->GpuvaG3SecondFence; c->GpuvaG3SecondFence = 0u;
  } else if (c->GpuvaG3SecondFence == f) c->GpuvaG3SecondFence = 0u;
}

/* rc102: these replays kick no depth-2 successor (the scheduler holds no
 * kicked fence), so the reset never aborts one. */
static void AdmissionG4PendingAbortKickedLocked(ADMISSION_CONTEXT *c) { (void)c; assert(0); }
static void AdmissionGpuvaG3AbortJob(ADMISSION_CONTEXT *c, ULONG f) { (void)c; (void)f; assert(0); }

/* What AdmissionBackendComplete does when the firmware ends the job
 * (natural completion, or a drop after Timeout/Fault and resume). */
static void model_job_ends(void) {
  ADMISSION_CONTEXT *c = model_adapter;
  ULONG fence = c->RenderPacket.Description.Fence;
  ADMISSION_RENDER_CONTEXT *owner =
      (ADMISSION_RENDER_CONTEXT *)(ULONG_PTR)c->RenderPacket.Description.ContextToken;
  assert(!lock_depth);
  assert(AppleAgxSchedulerAbortActiveFence(&c->Scheduler, 0, 0, fence));
  if (AppleAgxSchedulerPreemptionPhase(&c->Scheduler) == AppleAgxPreemptionWaitCurrentBoundary)
    assert(AppleAgxSchedulerObserveBoundaryCompletion(&c->Scheduler, 0, 0, fence));
  assert(AdmissionRenderPacketComplete(&c->RenderPacket, fence));
  AdmissionContextRetireFence(owner, fence);
  if (owner->GpuvaG3PrivateFence == (LONG)fence) owner->GpuvaG3PrivateFence = 0;
  scene_queued = 0; /* AdmissionGpuvaG3PrivateReported */
  if (model_active_scene != NULL) model_active_scene->Queued = 0u;
  ++model_runtime->FirmwareRecoveries; /* a drop after Timeout + resume */
  model_runtime->Backend.Phase = AppleAgxBackendRuntimeReady;
}

static NTSTATUS KeWaitForSingleObject(KEVENT *e, int r, int m, BOOLEAN a, LARGE_INTEGER *t) {
  (void)r; (void)m; (void)a; assert(!lock_depth && t && t->QuadPart < 0);
  if (worker_busy && model_runtime->Resetting) {
    /* The worker leaves its poll loop at the next step once Resetting is set. */
    if (worker_completes) model_job_ends();
    worker_busy = 0; model_runtime->WorkScheduled = 0; model_runtime->WorkersActive = 0;
    e->Signaled = 1;
  }
  if (e->Signaled) return STATUS_SUCCESS;
  now_ms += (ULONGLONG)(-t->QuadPart / 10000LL);
  return STATUS_TIMEOUT;
}
/* Only MMIO is simulated; the RTKit drain and sticky crash state are real. */
static unsigned mailbox_mode, mailbox_consumed;
static APPLE_AGX_ASC_BOOL mailbox_read32(void *ctx, unsigned offset, unsigned *v) {
  (void)ctx;
  assert(!lock_depth);
  if (mailbox_mode == 2) return APPLE_AGX_ASC_FALSE;
  assert(offset == J313_AGX_G2_ASC_OUTBOX_CTRL_OFFSET ||
         offset == J313_AGX_G2_ASC_INBOX_CTRL_OFFSET);
  *v = mailbox_mode && !mailbox_consumed ? 0 : APPLE_AGX_ASC_MAILBOX_EMPTY;
  return APPLE_AGX_ASC_TRUE;
}
static APPLE_AGX_ASC_BOOL mailbox_read64(void *ctx, unsigned offset,
                                       unsigned long long *v) {
  (void)ctx;
  assert(!lock_depth);
  if (offset == J313_AGX_G2_ASC_OUTBOX0_OFFSET)
    *v = mailbox_mode == 3 ? (1ULL << 52) : 0x0043000000000000ULL;
  else {
    assert(offset == J313_AGX_G2_ASC_OUTBOX1_OFFSET);
    *v = mailbox_mode == 3 ? 1u : 0x20u;
    ++mailbox_consumed;
  }
  return APPLE_AGX_ASC_TRUE;
}
/* rc77: a firmware replay may expose its real queue runtime; the backend's
 * view of it (provider runtime fields) is synchronized after every poll and
 * its forced RecoverChannel is redirected to that real runtime. */
__attribute__((weak)) APPLE_AGX_G13_QUEUE_RUNTIME *ReplayQueueRuntime(void) { return NULL; }
static void sync_queue_runtime(APPLE_AGX_PLATFORM_PROVIDER *p) {
  APPLE_AGX_G13_QUEUE_RUNTIME *real = ReplayQueueRuntime();
  if (real == NULL) return;
  p->QueueProvider.Runtime.Phase = real->Phase;
  p->QueueProvider.Runtime.PendingFence = real->PendingFence;
  p->QueueProvider.Runtime.RecoveryPending = real->RecoveryPending;
  p->QueueProvider.Runtime.PostRecoveryEvents = real->PostRecoveryEvents;
  p->QueueProvider.Runtime.PostRecoveryKindMask = real->PostRecoveryKindMask;
  p->QueueProvider.Runtime.LastPostRecoveryKind = real->LastPostRecoveryKind;
}
static APPLE_AGX_G13_QUEUE_RUNTIME_RESULT ReplayForceChannelRecovery(
    APPLE_AGX_G13_QUEUE_RUNTIME *r, APPLE_AGX_BACKEND_U32 *issued) {
  APPLE_AGX_G13_QUEUE_RUNTIME *real = ReplayQueueRuntime();
  assert(!lock_depth);
  return AppleAgxG13QueueRuntimeForceChannelRecovery(real ? real : r, issued);
}
#define AppleAgxG13QueueRuntimeForceChannelRecovery ReplayForceChannelRecovery
static BOOLEAN AppleAgxPlatformProviderPoll(APPLE_AGX_PLATFORM_PROVIDER *p,
    APPLE_AGX_BACKEND_U32 max, APPLE_AGX_BACKEND_U32 *drained, APPLE_AGX_BACKEND_U32 *completed) {
  (void)p; (void)max; assert(!lock_depth); assert(!worker_busy);
  *drained = *completed = 0u; ++polls;
  if (fail_poll) {
    p->LastPollGuard = 2u; p->LastDrainGuard = 3u; p->QueueProvider.LastIngestGuard = 0x1234u;
    return FALSE;
  }
  if (use_firmware_runtime) {
    if (ReplayQueuePoll(polls)) model_job_ends();
    sync_queue_runtime(p);
  } else if (polls == drop_at_poll) model_job_ends();
  return TRUE;
}
/* The pre-fix reset path: RTKit stop of the retained firmware, then refusal. */
static NTSTATUS AdmissionPlatformRuntimeReset(ADMISSION_CONTEXT *c, ULONG *aborted) {
  (void)c; *aborted = 0u; ++rtkit_stops; return STATUS_DEVICE_HARDWARE_ERROR; }
static BOOLEAN AdmissionGpuvaG3PrivateReset(ADMISSION_CONTEXT *c) {
  assert(!lock_depth); ++private_resets;
  if (reactivate_after_quiesce) {
    /* Model a worker activating after the first packet-state check. */
    c->RenderPacket.State = AdmissionRenderPacketActive;
  }
  if (scene_queued && scene_started) { scene_quarantined = 1; return FALSE; }
  return TRUE;
}
static void AdmissionPagingUpdateIdleLocked(ADMISSION_CONTEXT *c) { (void)c; assert(lock_depth); }
static BOOLEAN AdmissionBackendImageReleaseSubmission(int *i, ULONG f) { (void)i; (void)f; return TRUE; }
static NTSTATUS AdmissionScanoutSetTimelinePaused(ADMISSION_CONTEXT *c, BOOLEAN p) {
  (void)c; assert(!lock_depth); paused = p; return STATUS_SUCCESS; }
static int published, recorded;
static ADMISSION_GPU_RESET_RECEIPT last_recorded;
static void AdmissionPublishFirmwareRecovery(ADMISSION_PLATFORM_RUNTIME *r) {
  (void)r; assert(!lock_depth); ++published; }
/* rc77: model of the post-resume receipt (no shared memory or MMIO here). */
static int post_resume_published;
static ADMISSION_GPU_POST_RESUME_RECEIPT last_post_resume;
static void AdmissionCapturePostResume(ADMISSION_PLATFORM_RUNTIME *r, ULONG fence,
    ULONG wait_ms, ADMISSION_GPU_POST_RESUME_RECEIPT *receipt) {
  receipt->Version = ADMISSION_GPU_POST_RESUME_VERSION;
  receipt->Bytes = sizeof(*receipt);
  receipt->Fence = fence; receipt->WaitMs = wait_ms;
  receipt->PostEvents = r->Provider.QueueProvider.Runtime.PostRecoveryEvents;
  receipt->PostEventKindMask = r->Provider.QueueProvider.Runtime.PostRecoveryKindMask;
  receipt->LastPostEventKind = r->Provider.QueueProvider.Runtime.LastPostRecoveryKind;
}
static void AdmissionPublishPostResume(ADMISSION_PLATFORM_RUNTIME *r) {
  assert(!lock_depth); ++post_resume_published; last_post_resume = r->PostResume; }
static void AdmissionPlatformRuntimeResetState(ADMISSION_CONTEXT *c, ADMISSION_GPU_RESET_RECEIPT *r) {
  ADMISSION_PLATFORM_RUNTIME *p = c->PlatformRuntime;
  r->BackendPhase = (ULONG)p->Backend.Phase; r->WorkerBusy = p->WorkScheduled ? 1u : 0u;
  r->Halted = 0u; r->FirmwareRecoveries = (ULONG)p->FirmwareRecoveries;
  r->EntryMs = (ULONG)now_ms; r->JobAgeMs = 2100u; r->PollAgeMs = 7u; }
static void AdmissionRecordGpuReset(ADMISSION_CONTEXT *c) {
  assert(!lock_depth); ++recorded; last_recorded = c->GpuReset; }
'''

CASES = r'''
static ADMISSION_CONTEXT adapter;
static ADMISSION_PLATFORM_RUNTIME runtime;
/* owner runs the hung job; other has renders queued behind it; cancelled
 * holds a scene whose job the KMD cancelled before the reset. */
static ADMISSION_RENDER_CONTEXT owner, other, cancelled;
static ADMISSION_PAGING_RECORD paging_record;
static ADMISSION_G3_PROCESS process;
static ADMISSION_G3_PRIVATE_SCENE scene_owner, scene_first, scene_second, scene_cancelled;
static const APPLE_AGX_G4_PRIVATE_LEASE lease_owner = {17u, 5u, 9u, 9u},
    lease_first = {17u, 5u, 11u, 11u}, lease_second = {17u, 5u, 13u, 13u},
    lease_cancelled = {17u, 5u, 15u, 15u};

static void scene_init(ADMISSION_G3_PRIVATE_SCENE *s, ADMISSION_RENDER_CONTEXT *c,
                       const APPLE_AGX_G4_PRIVATE_LEASE *l, ADMISSION_G3_PRIVATE_SCENE *next) {
  memset(s, 0, sizeof(*s));
  s->Context = c; s->Storage.Generation = l->SceneId; s->Next = next;
  c->GpuvaG3PrivateManagerGeneration = l->ManagerGeneration;
}

/* A private job as AdmissionG4SubmitVirtualEnvelope leaves it: the scene
 * queued with the fence, which takes slot 1, or slot 2 when slot 1 is busy. */
static void private_job(ADMISSION_RENDER_CONTEXT *c, ADMISSION_G3_PRIVATE_SCENE *s, ULONG fence) {
  s->Queued = 1u; s->Fence = fence;
  if (c->GpuvaG3PrivateFence != 0) c->GpuvaG3PrivateFence2 = (LONG)fence;
  else c->GpuvaG3PrivateFence = (LONG)fence;
}

/* A private render queued behind the render slot (AdmissionG4PendingQueue):
 * the context's oldest job, or its second job (GpuvaG3SecondFence). */
static void pend(ULONG fence, ADMISSION_RENDER_CONTEXT *c, ADMISSION_G3_PRIVATE_SCENE *s) {
  ADMISSION_G4_PENDING *e = &adapter.G4Pending[
      (adapter.G4PendingHead + adapter.G4PendingCount) % ADMISSION_G4_PENDING_CAPACITY];
  assert(adapter.G4PendingCount < ADMISSION_G4_PENDING_CAPACITY &&
         e->State == AdmissionG4PendingFree);
  assert(c->Object.FenceOutstanding == 0u || c->GpuvaG3SecondFence == 0u);
  if (c->Object.FenceOutstanding == 0u) c->Object.FenceOutstanding = fence;
  else c->GpuvaG3SecondFence = fence;
  private_job(c, s, fence);
  assert(AppleAgxSchedulerQueueFence(&adapter.Scheduler, 0, 0, fence));
  e->State = AdmissionG4PendingQueued; e->Fence = fence; e->Context = c;
  ++adapter.G4PendingCount;
}

/* A one-record paging packet as AdmissionGpuvaG3SubmitVirtualPaging forwards it. */
static NTSTATUS submit_paging(ULONG fence, UINT resubmission) {
  DXGKARG_SUBMITCOMMAND args;
  memset(&args, 0, sizeof(args));
  args.SubmissionFenceId = fence; args.Flags.Paging = 1u; args.Flags.Resubmission = resubmission;
  return AdmissionCpuQueueSubmit(&adapter, &args, ADMISSION_CPU_PACKET_PAGING,
                                 &paging_record, sizeof(paging_record));
}

/* A G4 render the worker has queued (Prepare/Queue as SubmitCommandVirtual). */
static void queue_packet(ULONG fence, ADMISSION_RENDER_CONTEXT *c) {
  ADMISSION_RENDER_PACKET_DESCRIPTION d;
  memset(&d, 0, sizeof(d));
  d.Fence = fence; d.AllocationCount = 1u; d.ContextToken = (unsigned long long)(uintptr_t)c;
  d.AllocationToken = d.PrivateDataToken = 1u; d.PrivateDataBytes = d.PrivateDataEnd = 64u;
  d.DmaEnd = 64u; d.DestinationGpuVa = 0x1500fa0000ull; d.DestinationBytes = 0x4000u;
  d.Kind = ADMISSION_RENDER_PACKET_KIND_RENDER;
  assert(AdmissionRenderPacketPrepare(&adapter.RenderPacket, &d));
  assert(AdmissionRenderPacketQueue(&adapter.RenderPacket, fence, d.ContextToken, 1u, 0u, 64u));
}

/* EXP1296 state: fence 0xce71c active (0xce71b completed), a preemption
 * waiting on its boundary, a started private scene. */
static void setup(int worker, int queued) {
  memset(&adapter, 0, sizeof(adapter)); memset(&runtime, 0, sizeof(runtime));
  memset(&owner, 0, sizeof(owner)); memset(&other, 0, sizeof(other));
  memset(&cancelled, 0, sizeof(cancelled)); memset(&process, 0, sizeof(process));
  rtkit_stops = polls = drop_at_poll = fail_poll = worker_completes = 0;
  captures = 0;
  mailbox_mode = mailbox_consumed = 0;
  AppleAgxRtkitSessionInitialize(&runtime.Rtkit);
  runtime.Rtkit.Running = runtime.Rtkit.CpuReady = APPLE_AGX_RTKIT_TRUE;
  runtime.Rtkit.Boot.Phase = AppleAgxRtkitBootReady;
  runtime.Rtkit.CrashlogReplySent = APPLE_AGX_RTKIT_TRUE;
  runtime.AscIo.Read32 = mailbox_read32;
  runtime.AscIo.Read64 = mailbox_read64;
  published = recorded = 0; memset(&last_recorded, 0, sizeof(last_recorded));
  private_resets = scene_quarantined = paused = reactivate_after_quiesce = 0;
  scene_queued = scene_started = 1; now_ms = 1000u;
  model_adapter = &adapter; model_runtime = &runtime;
  adapter.SchedulerInitialized = 1; adapter.PlatformRuntime = &runtime;
  adapter.PagingWorkItem = &adapter;
  runtime.Adapter = &adapter; runtime.BackendStarted = TRUE;
  process.Graph.ProcessId = lease_owner.ManagerId;
  process.PrivateManager.Generation = lease_owner.ManagerGeneration;
  scene_init(&scene_cancelled, &cancelled, &lease_cancelled, NULL);
  scene_init(&scene_second, &other, &lease_second, &scene_cancelled);
  scene_init(&scene_first, &other, &lease_first, &scene_second);
  scene_init(&scene_owner, &owner, &lease_owner, &scene_first);
  process.PrivateScenes = &scene_owner;
  model_active_scene = &scene_owner;
  AppleAgxSchedulerInitialize(&adapter.Scheduler);
  AdmissionRenderPacketInitialize(&adapter.RenderPacket);
  adapter.Scheduler.CompletedFence = adapter.Scheduler.LastSubmittedFence = 0xce71bu;
  assert(AppleAgxSchedulerQueueFence(&adapter.Scheduler, 0, 0, 0xce71cu));
  assert(AppleAgxSchedulerActivateFence(&adapter.Scheduler, 0, 0, 0xce71cu));
  queue_packet(0xce71cu, &owner);
  assert(AdmissionRenderPacketActivate(&adapter.RenderPacket, 0xce71cu));
  adapter.DispatchedFence = 0xce71cu;
  owner.Object.FenceOutstanding = 0xce71cu;
  private_job(&owner, &scene_owner, 0xce71cu); scene_owner.Started = 1u;
  /* A later submission accepted before the preemption request: a render
   * fence (1) or a paging packet waiting in the CPU queue (2). */
  if (queued == 1) assert(AppleAgxSchedulerQueueFence(&adapter.Scheduler, 0, 0, 0xce71eu));
  if (queued == 2)
    assert(submit_paging(0xce71eu, 0u) == STATUS_SUCCESS && adapter.CpuQueueCount == 1u);
  if (queued == 3) {
    /* EXP1303: DxgkDdiPreemptCommand was refused (SchedulerFaulted
     * 0x1018f), so nothing was preempted. Context other's oldest and second
     * private renders wait in the G4 pending queue behind the hung job; a
     * third context's scene was cancelled earlier (a failed bind). */
    pend(0xce71du, &other, &scene_first);
    pend(0xce71eu, &other, &scene_second);
    assert(other.Object.FenceOutstanding == 0xce71du && other.GpuvaG3SecondFence == 0xce71eu &&
           other.GpuvaG3PrivateFence == (LONG)0xce71du &&
           other.GpuvaG3PrivateFence2 == (LONG)0xce71eu);
    private_job(&cancelled, &scene_cancelled, 0xce71au);
    AdmissionGpuvaG3PrivateCancel(&cancelled, 0xce71au, FALSE);
    adapter.SchedulerFaulted = 0x1018f;
  } else {
    assert(AppleAgxSchedulerBeginBoundaryPreemption(&adapter.Scheduler, 0, 0, 0xce71fu,
        adapter.Scheduler.LastSubmittedFence, 0xce71cu));
    assert(adapter.Scheduler.PreemptionPhase == AppleAgxPreemptionWaitCurrentBoundary);
  }
  runtime.Backend.Phase = AppleAgxBackendRuntimeSubmitted;
  worker_busy = worker;
  runtime.WorkIdle.Signaled = !worker; runtime.WorkScheduled = runtime.WorkersActive = worker;
}

static void expect_aborted(const DXGKARG_RESETENGINE *reset) {
  assert(reset->LastAbortedFenceId == 0xce71cu);
  assert(adapter.Scheduler.ActiveFence == 0u && adapter.Scheduler.CompletedFence == 0xce71bu &&
         adapter.Scheduler.LastAbortedFence == 0xce71cu);
  assert(adapter.Scheduler.PreemptionPhase == AppleAgxPreemptionIdle);
  assert(AdmissionRenderPacketState(&adapter.RenderPacket) == AdmissionRenderPacketEmpty);
  assert(owner.GpuvaG3Poisoned && owner.Object.FenceOutstanding == 0u);
  assert(adapter.SchedulerFaulted == 0 && adapter.DispatchedFence == 0u);
  assert(runtime.Resetting == 0 && !scene_quarantined && rtkit_stops == 0);
}

int main(void) {
  DXGKARG_RESETENGINE reset;
  unsigned mode;

  /* EXP1938: all three failures enter the same immediate quiesce/poll
   * result. Neither this receipt nor pending queue operands identify the
   * rejected message. The unretired scene must survive every failure. */
  for (mode = 1; mode <= 3; ++mode) {
    unsigned retry;
    setup(0, 0); mailbox_mode = mode;
    for (retry = 0; retry < 3; ++retry) {
      /* A protocol error consumes its message, unlike sticky crash or
       * transport failure. Supply another message for each such attempt. */
      if (mode == 1) mailbox_consumed = 0;
      memset(&reset, 0, sizeof(reset));
      assert((retry < 2 ? AdmissionDdiResetEngine(&adapter, &reset) :
              AdmissionDdiResetFromTimeout(&adapter)) == STATUS_DEVICE_HARDWARE_ERROR);
      assert(last_recorded.Quiesce == AdmissionGpuResetQuiescePoll &&
             last_recorded.Stage == AdmissionGpuResetStageQuiesce &&
             last_recorded.WaitMs == 0 && last_recorded.Succeeded == 0);
      /* rc92: the published post-resume receipt names the RTKit drain. */
      assert((last_post_resume.PollFailure >> 24) == ADMISSION_GPU_POST_RESUME_POLL_RTKIT &&
             (last_post_resume.PollFailure & 0xffu) != AppleAgxRtkitSessionResultOk &&
             (last_post_resume.PollFailure & 0xffff00u) == 0u);
      assert(last_recorded.Resets == retry + 1 && polls == 0 &&
             private_resets == 0 && rtkit_stops == 0);
      assert(adapter.Scheduler.ActiveFence == 0xce71cu &&
             adapter.Scheduler.CompletedFence == 0xce71bu &&
             AdmissionRenderPacketState(&adapter.RenderPacket) == AdmissionRenderPacketActive);
      assert(owner.Object.FenceOutstanding == 0xce71cu &&
             owner.GpuvaG3PrivateFence == (LONG)0xce71cu &&
             scene_owner.Started && scene_owner.Queued);
      assert(reset.LastAbortedFenceId == 0 && last_recorded.LastAbortedFence == 0 &&
             runtime.Resetting == 0 && captures == 2 * (retry + 1));
    }
    assert(runtime.Rtkit.CrashlogCrashed == (mode == 3));
    if (mode == 3) assert(mailbox_consumed == 1); /* crash remains sticky */
    if (mode == 2) assert(mailbox_consumed == 0); /* no invented message */
  }

  /* Real queue runtime: fault -> resume -> partial retirement -> dropped
   * completion -> genuine quiescence -> successful Windows engine reset. */
  setup(0, 0); use_firmware_runtime = 1;
  ReplayQueueSetup(0xce71cu);
  memset(&reset, 0, sizeof(reset));
  assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_SUCCESS);
  expect_aborted(&reset);
  assert(polls == 4 && last_recorded.Quiesce == AdmissionGpuResetQuiesceEnded);
  assert(last_recorded.Stage == AdmissionGpuResetStageComplete);
  /* Adapter TDR uses the same retirement proof and can restart scheduling. */
  setup(0, 0); ReplayQueueSetup(0xce71cu);
  assert(AdmissionDdiResetFromTimeout(&adapter) == STATUS_SUCCESS);
  assert(last_recorded.LastAbortedFence == 0xce71cu && paused);
  assert(AdmissionDdiRestartFromTimeout(&adapter) == STATUS_SUCCESS && !paused);
  use_firmware_runtime = 0;

  /* 1. EXP1296: the worker already left (SchedulerFaulted set before the
   * TDR); the firmware drops the job when the reset polls the provider. */
  setup(0, 0); adapter.SchedulerFaulted = 0x40000 | 3930; drop_at_poll = 3;
  memset(&reset, 0, sizeof(reset));
  assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_SUCCESS);
  expect_aborted(&reset); assert(polls == 3);
  assert(captures == 2u && last_recorded.RetirementBefore.Captured &&
         last_recorded.RetirementAfter.Captured);
  /* Wom1GpuReset names the setter of SchedulerFaulted and the end of the job. */
  assert(recorded == 1 && published == 1);
  assert(last_recorded.Version == ADMISSION_GPU_RESET_RECEIPT_VERSION &&
         last_recorded.Bytes == sizeof(ADMISSION_GPU_RESET_RECEIPT));
  assert(last_recorded.Resets == 1u && last_recorded.Succeeded == 1u &&
         last_recorded.Entry == ADMISSION_GPU_RESET_ENGINE && last_recorded.Status == 0u);
  assert(last_recorded.SchedulerFaulted == (0x40000u | 3930u));
  assert(last_recorded.PacketState == AdmissionRenderPacketActive &&
         last_recorded.PacketFence == 0xce71cu && last_recorded.ActiveFence == 0xce71cu &&
         last_recorded.CompletedFence == 0xce71bu &&
         last_recorded.PreemptionPhase == AppleAgxPreemptionWaitCurrentBoundary);
  assert(last_recorded.ContextToken == (unsigned long long)(uintptr_t)&owner);
  assert(last_recorded.Quiesce == AdmissionGpuResetQuiesceEnded &&
         last_recorded.WaitMs == 2u && last_recorded.LastAbortedFence == 0xce71cu &&
         last_recorded.BackendPhase == AppleAgxBackendRuntimeSubmitted &&
         last_recorded.FirmwareRecoveries == 1u);
  /* Version 2: the timed history entry mirrors the attempt. */
  assert(last_recorded.Version == 4u && last_recorded.HistoryCount == 1u &&
         last_recorded.EntryMs == 1000u && last_recorded.JobAgeMs == 2100u &&
         last_recorded.PollAgeMs == 7u);
  assert(last_recorded.History[0].EntryMs == 1000u &&
         last_recorded.History[0].Quiesce == AdmissionGpuResetQuiesceEnded &&
         last_recorded.History[0].PacketFence == 0xce71cu &&
         last_recorded.History[0].LastAbortedFence == 0xce71cu &&
         last_recorded.History[0].SchedulerFaulted == (0x40000u | 3930u) &&
         last_recorded.History[0].JobAgeMs == 2100u);

  assert(last_recorded.Stage == AdmissionGpuResetStageComplete &&
         last_recorded.HistoryStage[0] == AdmissionGpuResetStageComplete);

  /* 2. The same storm promoted to an adapter reset: success, the last
   * completed fence advances to the last submitted one, restart resumes. */
  setup(0, 1); drop_at_poll = 2;
  assert(AdmissionDdiResetFromTimeout(&adapter) == STATUS_SUCCESS);
  assert(paused && adapter.Scheduler.CompletedFence == 0xce71eu &&
         adapter.Scheduler.LastSubmittedFence == 0xce71eu);
  assert(owner.GpuvaG3Poisoned && rtkit_stops == 0 && adapter.SchedulerFaulted == 0);
  assert(AdmissionDdiRestartFromTimeout(&adapter) == STATUS_SUCCESS && !paused);
  assert(last_recorded.Entry == ADMISSION_GPU_RESET_ADAPTER && last_recorded.Succeeded == 1u);
  /* The adapter reset's OS fence convention supersedes the prior abort.
   * A later engine reset must not regress to that older executed boundary. */
  assert(adapter.Scheduler.LastAbortedFence == 0u);
  assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_SUCCESS);
  assert(reset.LastAbortedFenceId == 0xce71eu);
  assert(!AppleAgxSchedulerQueueResubmittedPagingFence(&adapter.Scheduler,0,0,0xce71eu));

  /* 3. The worker is still polling and the job ends naturally on its way out:
   * no RTKit stop, success (a non-private job reaches the runtime path). */
  setup(1, 0); scene_started = 0; worker_completes = 1;
  memset(&reset, 0, sizeof(reset));
  assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_SUCCESS);
  expect_aborted(&reset); assert(polls == 0);

  /* 4. The firmware never ends the job: no success, no RTKit stop, the job
   * stays active and dispatch stays frozen; the wait is bounded. */
  setup(0, 0);
  {
    ULONGLONG start = now_ms;
    memset(&reset, 0, sizeof(reset));
    assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_DEVICE_HARDWARE_ERROR);
    assert(now_ms - start <= ADMISSION_RESET_QUIESCE_TIMEOUT_MS + 1u);
  }
  assert(adapter.Scheduler.ActiveFence == 0xce71cu && rtkit_stops == 0);
  assert(AdmissionRenderPacketState(&adapter.RenderPacket) == AdmissionRenderPacketActive);
  assert(adapter.SchedulerFaulted != 0 && runtime.Resetting == 0 && owner.GpuvaG3Poisoned);
  assert(last_recorded.Quiesce == AdmissionGpuResetQuiesceTimeout &&
         last_recorded.Status == (ULONG)STATUS_DEVICE_HARDWARE_ERROR &&
         last_recorded.Succeeded == 0u && last_recorded.LastAbortedFence == 0u);
  assert(AdmissionDdiResetFromTimeout(&adapter) == STATUS_DEVICE_HARDWARE_ERROR);
  assert(rtkit_stops == 0 && adapter.Scheduler.ActiveFence == 0xce71cu);
  /* The reset's own fault code is what the next attempt finds. */
  assert(last_recorded.Resets == 2u && last_recorded.Entry == ADMISSION_GPU_RESET_ADAPTER &&
         (last_recorded.SchedulerFaulted >> 16) == 1u);
  assert(last_recorded.HistoryCount == 2u &&
         last_recorded.History[0].Entry == ADMISSION_GPU_RESET_ENGINE &&
         last_recorded.History[1].Entry == ADMISSION_GPU_RESET_ADAPTER &&
         last_recorded.History[1].Quiesce == AdmissionGpuResetQuiesceTimeout &&
         last_recorded.History[1].EntryMs > last_recorded.History[0].EntryMs);

  assert(last_recorded.Stage == AdmissionGpuResetStageQuiesce &&
         last_recorded.HistoryStage[0] == AdmissionGpuResetStageQuiesce &&
         last_recorded.HistoryStage[1] == AdmissionGpuResetStageQuiesce);

  /* An old started scene remains after local packet retirement. Distinguish
   * its private-reset refusal from the identical c0000483 quiesce status. */
  setup(0, 0);
  model_job_ends();
  scene_queued = scene_started = 1;
  assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_DEVICE_HARDWARE_ERROR);
  assert(last_recorded.Stage == AdmissionGpuResetStagePrivate &&
         last_recorded.Quiesce == AdmissionGpuResetQuiesceNone &&
         last_recorded.HistoryStage[0] == AdmissionGpuResetStagePrivate);
  /* A successful retry must overwrite the last stage, preserving history. */
  scene_queued = 0;
  assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_SUCCESS);
  assert(last_recorded.Stage == AdmissionGpuResetStageComplete &&
         last_recorded.HistoryStage[0] == AdmissionGpuResetStagePrivate &&
         last_recorded.HistoryStage[1] == AdmissionGpuResetStageComplete);

  setup(0, 0);
  model_job_ends();
  reactivate_after_quiesce = 1;
  assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_DEVICE_HARDWARE_ERROR);
  assert(last_recorded.Stage == AdmissionGpuResetStagePacketActive &&
         last_recorded.Quiesce == AdmissionGpuResetQuiesceNone &&
         last_recorded.HistoryStage[0] == AdmissionGpuResetStagePacketActive);

  /* 5. A provider that cannot be polled proves nothing. */
  setup(0, 0); fail_poll = 1;
  memset(&reset, 0, sizeof(reset));
  assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_DEVICE_HARDWARE_ERROR);
  assert(adapter.Scheduler.ActiveFence == 0xce71cu && rtkit_stops == 0 && runtime.Resetting == 0);
  assert(last_recorded.Quiesce == AdmissionGpuResetQuiescePoll);
  /* rc92: ... and the provider's guards, saturated per byte. */
  assert(last_post_resume.PollFailure ==
         ((ADMISSION_GPU_POST_RESUME_POLL_PROVIDER << 24) | (0xffu << 16) | (3u << 8) | 2u));
  /* The next wait on the same fence starts without the old failure. */
  fail_poll = 0; drop_at_poll = polls + 1;
  memset(&reset, 0, sizeof(reset));
  assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_SUCCESS);
  assert(last_post_resume.PollFailure == 0u);

  /* 6. A worker that never leaves: no exclusive poll ownership, no success. */
  setup(1, 0); runtime.Resetting = 0;
  worker_busy = 0; runtime.WorkIdle.Signaled = 0; /* stuck worker */
  memset(&adapter.GpuReset.RetirementBefore, 0xff, sizeof(adapter.GpuReset.RetirementBefore));
  memset(&adapter.GpuReset.RetirementAfter, 0xff, sizeof(adapter.GpuReset.RetirementAfter));
  memset(&reset, 0, sizeof(reset));
  assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_DEVICE_HARDWARE_ERROR);
  assert(polls == 0 && adapter.Scheduler.ActiveFence == 0xce71cu && runtime.Resetting == 0);
  assert(last_recorded.Quiesce == AdmissionGpuResetQuiesceWorker && published == 0);
  assert(captures == 0u && !last_recorded.RetirementBefore.Captured &&
         !last_recorded.RetirementAfter.Captured);

  /* 7. Nothing active (a queued render): unchanged engine reset. */
  setup(0, 0); drop_at_poll = 1;
  assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_SUCCESS); /* ends 0xce71c */
  assert(AppleAgxSchedulerQueueFence(&adapter.Scheduler, 0, 0, 0xce71eu));
  queue_packet(0xce71eu, &other);
  other.Object.FenceOutstanding = 0xce71eu; private_job(&other, &scene_first, 0xce71eu);
  polls = 0;
  memset(&reset, 0, sizeof(reset));
  assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_SUCCESS);
  assert(reset.LastAbortedFenceId == 0xce71cu && polls == 0);
  assert(last_recorded.Quiesce == AdmissionGpuResetQuiesceNone &&
         last_recorded.PacketState == AdmissionRenderPacketQueued);
  assert(AdmissionRenderPacketState(&adapter.RenderPacket) == AdmissionRenderPacketEmpty);
  assert(other.Object.FenceOutstanding == 0u && adapter.Scheduler.QueuedFence == 0u);
  /* EXP1303: VidSch resubmits never-started renders after an engine reset
   * with new fences and Resubmission set, as after a preemption; a cancelled
   * private scene made that resubmission fail (Wom1G4SubmitFailure branch 7,
   * subsite 0x201, cancel marker = the queued fence). */
  assert(other.GpuvaG3PreemptFence == (LONG)0xce71eu && other.GpuvaG3CancelFence == 0);
  assert(AdmissionG4FindPrivateResubmission(&process, &other, &lease_first, 0xce720u) ==
         &scene_first);
  assert(AdmissionG4FindPrivateResubmission(&process, &other, &lease_first, 0xce71eu) == NULL);

  /* 8. Microsoft (TDR in Windows 8 and later, packets unaffected by engine
   * reset): the paging packet queued behind the active render never ran. The
   * reset empties the queue; VidSch resubmits the packet with its original
   * fence, before new packets. */
  setup(0, 2); drop_at_poll = 1;
  memset(&reset, 0, sizeof(reset));
  assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_SUCCESS);
  expect_aborted(&reset);
  assert(adapter.CpuQueueCount == 0u && adapter.Scheduler.QueuedFence == 0u);
  /* The completed, the aborted and a never-queued fence stay refused. */
  assert(submit_paging(0xce71bu, 1u) == STATUS_DEVICE_BUSY);
  assert(submit_paging(0xce71cu, 1u) == STATUS_DEVICE_BUSY);
  assert(submit_paging(0xce71du, 1u) == STATUS_DEVICE_BUSY);
  assert(submit_paging(0xce71eu, 1u) == STATUS_SUCCESS);
  assert(adapter.CpuQueueCount == 1u &&
         adapter.CpuQueue[adapter.CpuQueueHead].Fence == 0xce71eu &&
         adapter.Scheduler.QueuedFence == 0xce71eu);
  assert(submit_paging(0xce71eu, 1u) == STATUS_DEVICE_BUSY); /* once */
  assert(submit_paging(0xce720u, 0u) == STATUS_SUCCESS);
  assert(adapter.CpuQueueCount == 2u && adapter.Scheduler.LastSubmittedFence == 0xce720u);

  /* 9. Renders queued behind the hung job in the G4 pending queue (the
   * other context's oldest job and its second job) never ran either. The
   * reset empties the queue and preempts both scenes in their own slots, so
   * VidSch's resubmissions with new fences find them; the aborted job's
   * scene and a scene cancelled before the reset stay refused. */
  setup(0, 3); drop_at_poll = 1;
  memset(&reset, 0, sizeof(reset));
  assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_SUCCESS);
  expect_aborted(&reset);
  assert(last_recorded.SchedulerFaulted == 0x1018fu);
  assert(adapter.G4PendingCount == 0u &&
         adapter.G4Pending[0].State == AdmissionG4PendingFree &&
         adapter.G4Pending[1].State == AdmissionG4PendingFree);
  assert(other.Object.FenceOutstanding == 0u && other.GpuvaG3SecondFence == 0u);
  assert(other.GpuvaG3PreemptFence == (LONG)0xce71du &&
         other.GpuvaG3PreemptFence2 == (LONG)0xce71eu);
  assert(other.GpuvaG3CancelFence == 0 && other.GpuvaG3CancelFence2 == 0 &&
         other.GpuvaG3CancelUncertain == 0);
  /* New fences, in order, after the last submitted one. */
  assert(AdmissionG4FindPrivateResubmission(&process, &other, &lease_first, 0xce721u) ==
         &scene_first);
  assert(AdmissionG4FindPrivateResubmission(&process, &other, &lease_second, 0xce722u) ==
         &scene_second);
  assert(AppleAgxSchedulerQueueFence(&adapter.Scheduler, 0, 0, 0xce721u) &&
         AppleAgxSchedulerQueueFence(&adapter.Scheduler, 0, 0, 0xce722u));
  /* The original fence is no resubmission, and without Resubmission the
   * scene still awaits its preempted job (the EXP911 contract). */
  assert(AdmissionG4FindPrivateResubmission(&process, &other, &lease_first, 0xce71du) == NULL);
  assert(AdmissionG4FindPrivateScene(&process, &other, &lease_second, 0xce723u, FALSE) == NULL);
  /* The aborted job's context is faulted and its scene was not preempted. */
  assert(owner.GpuvaG3PreemptFence == 0 && owner.GpuvaG3PreemptFence2 == 0);
  assert(AdmissionG4FindPrivateResubmission(&process, &owner, &lease_owner, 0xce723u) == NULL);
  /* A reset never turns a cancellation into a preemption. */
  assert(cancelled.GpuvaG3CancelFence == (LONG)0xce71au && cancelled.GpuvaG3PreemptFence == 0);
  assert(AdmissionG4FindPrivateResubmission(&process, &cancelled, &lease_cancelled,
                                            0xce723u) == NULL);
  assert(lock_depth == 0);
  puts("reset quiesce replay ok");
  return 0;
}
'''


FIRMWARE_REPLAY = r'''
#include "apple_agx_g13_queue_runtime_test.c"
#undef assert
#include <assert.h>
static TEST_FIXTURE recovery_fixture;
static APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION recovery_submission;
void ReplayQueueSetup(unsigned int fence) {
  TestInitialize(&recovery_fixture);
  recovery_fixture.Runtime.Io.Recover = TestRecover;
  recovery_submission = TestSubmission(&recovery_fixture, fence);
  assert(AppleAgxG13QueueRuntimeSubmit(&recovery_fixture.Runtime,
                                      &recovery_submission) == 0);
}
int ReplayQueuePoll(int poll) {
  APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION completion;
  unsigned char event[APPLE_AGX_G13_EVENT_MESSAGE_SIZE] = {0};
  if (poll == 1) {
    PutU32(event, AppleAgxG13EventTimeout);
    assert(AppleAgxG13QueueRuntimeHandleEvent(&recovery_fixture.Runtime,
                                              event, sizeof(event)) == 0);
  }
  if (poll == 2) {
    recovery_fixture.TaDone = recovery_fixture.TaWrite;
  }
  /* Killed commands leave both stamps unchanged. Only done pointers advance. */
  if (poll == 4) recovery_fixture.D3Done = recovery_fixture.D3Write;
  assert(AppleAgxG13QueueRuntimeCheckTimeout(&recovery_fixture.Runtime,
                                             1000ULL + poll) == 0);
  if (poll < 4) {
    assert(!AppleAgxG13QueueRuntimeTakeCompletion(&recovery_fixture.Runtime,
                                                  &completion));
    return 0;
  }
  assert(AppleAgxG13QueueRuntimeTakeCompletion(&recovery_fixture.Runtime,
                                               &completion));
  assert(completion.Fence == recovery_submission.Fence &&
         completion.Status == AppleAgxG13QueueCompletionRecovered);
  assert(recovery_fixture.IoState.Quiesces == 0);
  assert(recovery_fixture.TaStamp == 0 && recovery_fixture.D3Stamp == 0);
  return 1;
}
'''

if __name__ == '__main__':
    unittest.main()
