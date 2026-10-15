"""Replay the KMD engine/adapter reset with an active AGX render job.

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
and never-queued fences stay refused.

The real reset functions are compiled with the real shared scheduler and
render-packet state machines; the firmware/provider is a scripted model of
what AdmissionBackendComplete does to the adapter when a job ends.
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
        scheduler = (RENDER / 'src/scheduler_windows.c').read_text()
        backend = (RENDER / 'src/backend_platform_windows.c').read_text()
        queue = (RENDER / 'src/work_queue_windows.c').read_text()
        header = (RENDER / 'include/render_admission.h').read_text()
        timeout = re.search(r'#define ADMISSION_RESET_QUIESCE_TIMEOUT_MS (\d+)u', header)
        packet = re.search(r'typedef struct _ADMISSION_CPU_PACKET.*?} ADMISSION_CPU_PACKET;',
                           header, re.S).group(0)
        body = (SHIM.replace('/*TIMEOUT*/', timeout.group(1) if timeout else '1000')
                    .replace('/*PACKET*/', packet) +
                function_body(queue, 'AdmissionCpuQueueSubmit') + '\n' +
                function_body(scheduler, 'AdmissionCpuQueueReleaseContextsLocked') + '\n' +
                optional_body(backend, 'AdmissionPlatformRuntimeQuiesceActive') + '\n' +
                optional_body(scheduler, 'AdmissionResetQuiesceActive') + '\n' +
                noinline_body(scheduler, 'AdmissionResetEngineInternal') + '\n' +
                noinline_body(scheduler, 'AdmissionResetEngineRecorded') + '\n' +
                '\n'.join(function_body(scheduler, n) for n in (
                    'AdmissionResetEngineEntry', 'AdmissionDdiResetEngine',
                    'AdmissionDdiResetFromTimeout',
                    'AdmissionDdiRestartFromTimeout')) + CASES)
        with tempfile.TemporaryDirectory() as tmp:
            program = Path(tmp) / 'reset.c'
            program.write_text(body)
            binary = Path(tmp) / 'reset'
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                            '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                            '-I', str(RENDER / 'include'), '-I', str(SHARED / 'include'),
                            str(program), str(SHARED / 'src/apple_agx_scheduler.c'),
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
#define APPLE_AGX_GPUVA_G3_QUALIFICATION 1
#define _Use_decl_annotations_
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
#define NT_SUCCESS(s) ((NTSTATUS)(s) >= 0)
#define PASSIVE_LEVEL 0
#define Executive 0
#define KernelMode 0
#define ADMISSION_RESET_QUIESCE_TIMEOUT_MS /*TIMEOUT*/u
#define ADMISSION_SCHEDULER_NODE 0u
#define ADMISSION_SCHEDULER_ENGINE 0u
#define APPLE_AGX_RTKIT_RUNTIME_DRAIN_LIMIT 64u
#define AppleAgxRtkitSessionResultOk 0
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
typedef int APPLE_AGX_RTKIT_SESSION_RESULT;
typedef union { LONGLONG QuadPart; } LARGE_INTEGER;
typedef struct { int Signaled; } KEVENT;
typedef struct _ADMISSION_RENDER_CONTEXT {
  struct { ULONG FenceOutstanding; } Object;
  ULONG GpuvaG3SecondFence;
  volatile LONG GpuvaG3PrivateFence, GpuvaG3PrivateFence2;
  BOOLEAN GpuvaG3Poisoned;
} ADMISSION_RENDER_CONTEXT;
/*PACKET*/
typedef struct { int Provider; } APPLE_AGX_PLATFORM_PROVIDER;
typedef struct { int Phase; } APPLE_AGX_BACKEND_RUNTIME;
typedef struct _ADMISSION_CONTEXT ADMISSION_CONTEXT;
typedef struct _ADMISSION_PLATFORM_RUNTIME {
  ADMISSION_CONTEXT *Adapter;
  KEVENT WorkIdle;
  volatile LONG WorkScheduled, WorkersActive, Stopping, Resetting;
  BOOLEAN BackendStarted;
  APPLE_AGX_BACKEND_RUNTIME Backend;
  APPLE_AGX_PLATFORM_PROVIDER Provider;
  int Rtkit, AscIo;
  volatile LONG FirmwareRecoveries;
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
    scene_quarantined, cancels;
static ULONGLONG now_ms;
static ADMISSION_CONTEXT *model_adapter;
static ADMISSION_PLATFORM_RUNTIME *model_runtime;

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

/* What AdmissionBackendComplete does when the firmware ends the job
 * (natural completion, or a drop after Timeout/Fault and resume). */
static void model_job_ends(void) {
  ADMISSION_CONTEXT *c = model_adapter;
  ULONG fence = c->RenderPacket.Description.Fence;
  ADMISSION_RENDER_CONTEXT *owner =
      (ADMISSION_RENDER_CONTEXT *)(ULONG_PTR)c->RenderPacket.Description.ContextToken;
  assert(!lock_depth);
  assert(AppleAgxSchedulerCompleteActiveFence(&c->Scheduler, 0, 0, fence));
  if (AppleAgxSchedulerPreemptionPhase(&c->Scheduler) == AppleAgxPreemptionWaitCurrentBoundary)
    assert(AppleAgxSchedulerObserveBoundaryCompletion(&c->Scheduler, 0, 0, fence));
  assert(AdmissionRenderPacketComplete(&c->RenderPacket, fence));
  AdmissionContextRetireFence(owner, fence);
  if (owner->GpuvaG3PrivateFence == (LONG)fence) owner->GpuvaG3PrivateFence = 0;
  scene_queued = 0; /* AdmissionGpuvaG3PrivateReported */
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
static APPLE_AGX_RTKIT_SESSION_RESULT AppleAgxRtkitSessionDrainRuntime(
    int *s, int *io, APPLE_AGX_RTKIT_U32 limit, APPLE_AGX_RTKIT_U32 *n) {
  (void)s; (void)io; (void)limit; assert(!lock_depth); *n = 0u; return AppleAgxRtkitSessionResultOk; }
static BOOLEAN AppleAgxPlatformProviderPoll(APPLE_AGX_PLATFORM_PROVIDER *p,
    APPLE_AGX_BACKEND_U32 max, APPLE_AGX_BACKEND_U32 *drained, APPLE_AGX_BACKEND_U32 *completed) {
  (void)p; (void)max; assert(!lock_depth); assert(!worker_busy);
  *drained = *completed = 0u; ++polls;
  if (fail_poll) return FALSE;
  if (polls == drop_at_poll) model_job_ends();
  return TRUE;
}
/* The pre-fix reset path: RTKit stop of the retained firmware, then refusal. */
static NTSTATUS AdmissionPlatformRuntimeReset(ADMISSION_CONTEXT *c, ULONG *aborted) {
  (void)c; *aborted = 0u; ++rtkit_stops; return STATUS_DEVICE_HARDWARE_ERROR; }
static BOOLEAN AdmissionGpuvaG3PrivateReset(ADMISSION_CONTEXT *c) {
  (void)c; assert(!lock_depth); ++private_resets;
  if (scene_queued && scene_started) { scene_quarantined = 1; return FALSE; }
  return TRUE;
}
static void AdmissionGpuvaG3PrivateCancel(ADMISSION_RENDER_CONTEXT *c, ULONG f, BOOLEAN u) {
  (void)c; (void)f; (void)u; ++cancels; }
static void AdmissionG4PendingDropLocked(ADMISSION_CONTEXT *c, BOOLEAN p) { (void)c; (void)p; assert(lock_depth); }
static void AdmissionPagingUpdateIdleLocked(ADMISSION_CONTEXT *c) { (void)c; assert(lock_depth); }
static BOOLEAN AdmissionBackendImageReleaseSubmission(int *i, ULONG f) { (void)i; (void)f; return TRUE; }
static NTSTATUS AdmissionScanoutSetTimelinePaused(ADMISSION_CONTEXT *c, BOOLEAN p) {
  (void)c; assert(!lock_depth); paused = p; return STATUS_SUCCESS; }
static int published, recorded;
static ADMISSION_GPU_RESET_RECEIPT last_recorded;
static void AdmissionPublishFirmwareRecovery(ADMISSION_PLATFORM_RUNTIME *r) {
  (void)r; assert(!lock_depth); ++published; }
static void AdmissionPlatformRuntimeResetState(ADMISSION_CONTEXT *c, ADMISSION_GPU_RESET_RECEIPT *r) {
  ADMISSION_PLATFORM_RUNTIME *p = c->PlatformRuntime;
  r->BackendPhase = (ULONG)p->Backend.Phase; r->WorkerBusy = p->WorkScheduled ? 1u : 0u;
  r->Halted = 0u; r->FirmwareRecoveries = (ULONG)p->FirmwareRecoveries; }
static void AdmissionRecordGpuReset(ADMISSION_CONTEXT *c) {
  assert(!lock_depth); ++recorded; last_recorded = c->GpuReset; }
'''

CASES = r'''
static ADMISSION_CONTEXT adapter;
static ADMISSION_PLATFORM_RUNTIME runtime;
static ADMISSION_RENDER_CONTEXT owner;
static ADMISSION_PAGING_RECORD paging_record;

/* A one-record paging packet as AdmissionGpuvaG3SubmitVirtualPaging forwards it. */
static NTSTATUS submit_paging(ULONG fence, UINT resubmission) {
  DXGKARG_SUBMITCOMMAND args;
  memset(&args, 0, sizeof(args));
  args.SubmissionFenceId = fence; args.Flags.Paging = 1u; args.Flags.Resubmission = resubmission;
  return AdmissionCpuQueueSubmit(&adapter, &args, ADMISSION_CPU_PACKET_PAGING,
                                 &paging_record, sizeof(paging_record));
}

/* A G4 render the worker has queued (Prepare/Queue as SubmitCommandVirtual). */
static void queue_packet(ULONG fence) {
  ADMISSION_RENDER_PACKET_DESCRIPTION d;
  memset(&d, 0, sizeof(d));
  d.Fence = fence; d.AllocationCount = 1u; d.ContextToken = (unsigned long long)(uintptr_t)&owner;
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
  memset(&owner, 0, sizeof(owner));
  rtkit_stops = polls = drop_at_poll = fail_poll = worker_completes = 0;
  published = recorded = 0; memset(&last_recorded, 0, sizeof(last_recorded));
  private_resets = scene_quarantined = cancels = paused = 0;
  scene_queued = scene_started = 1; now_ms = 1000u;
  model_adapter = &adapter; model_runtime = &runtime;
  adapter.SchedulerInitialized = 1; adapter.PlatformRuntime = &runtime;
  adapter.PagingWorkItem = &adapter;
  runtime.Adapter = &adapter; runtime.BackendStarted = TRUE;
  AppleAgxSchedulerInitialize(&adapter.Scheduler);
  AdmissionRenderPacketInitialize(&adapter.RenderPacket);
  adapter.Scheduler.CompletedFence = adapter.Scheduler.LastSubmittedFence = 0xce71bu;
  assert(AppleAgxSchedulerQueueFence(&adapter.Scheduler, 0, 0, 0xce71cu));
  assert(AppleAgxSchedulerActivateFence(&adapter.Scheduler, 0, 0, 0xce71cu));
  queue_packet(0xce71cu);
  assert(AdmissionRenderPacketActivate(&adapter.RenderPacket, 0xce71cu));
  adapter.DispatchedFence = 0xce71cu;
  owner.Object.FenceOutstanding = 0xce71cu; owner.GpuvaG3PrivateFence = (LONG)0xce71cu;
  /* A later submission accepted before the preemption request: a render
   * fence (1) or a paging packet waiting in the CPU queue (2). */
  if (queued == 1) assert(AppleAgxSchedulerQueueFence(&adapter.Scheduler, 0, 0, 0xce71eu));
  if (queued == 2)
    assert(submit_paging(0xce71eu, 0u) == STATUS_SUCCESS && adapter.CpuQueueCount == 1u);
  assert(AppleAgxSchedulerBeginBoundaryPreemption(&adapter.Scheduler, 0, 0, 0xce71fu,
      adapter.Scheduler.LastSubmittedFence, 0xce71cu));
  assert(adapter.Scheduler.PreemptionPhase == AppleAgxPreemptionWaitCurrentBoundary);
  runtime.Backend.Phase = AppleAgxBackendRuntimeSubmitted;
  worker_busy = worker;
  runtime.WorkIdle.Signaled = !worker; runtime.WorkScheduled = runtime.WorkersActive = worker;
}

static void expect_aborted(const DXGKARG_RESETENGINE *reset) {
  assert(reset->LastAbortedFenceId == 0xce71cu);
  assert(adapter.Scheduler.ActiveFence == 0u && adapter.Scheduler.CompletedFence == 0xce71cu);
  assert(adapter.Scheduler.PreemptionPhase == AppleAgxPreemptionIdle);
  assert(AdmissionRenderPacketState(&adapter.RenderPacket) == AdmissionRenderPacketEmpty);
  assert(owner.GpuvaG3Poisoned && owner.Object.FenceOutstanding == 0u);
  assert(adapter.SchedulerFaulted == 0 && adapter.DispatchedFence == 0u);
  assert(runtime.Resetting == 0 && !scene_quarantined && rtkit_stops == 0);
}

int main(void) {
  DXGKARG_RESETENGINE reset;

  /* 1. EXP1296: the worker already left (SchedulerFaulted set before the
   * TDR); the firmware drops the job when the reset polls the provider. */
  setup(0, 0); adapter.SchedulerFaulted = 0x40000 | 3930; drop_at_poll = 3;
  memset(&reset, 0, sizeof(reset));
  assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_SUCCESS);
  expect_aborted(&reset); assert(polls == 3);
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

  /* 2. The same storm promoted to an adapter reset: success, the last
   * completed fence advances to the last submitted one, restart resumes. */
  setup(0, 1); drop_at_poll = 2;
  assert(AdmissionDdiResetFromTimeout(&adapter) == STATUS_SUCCESS);
  assert(paused && adapter.Scheduler.CompletedFence == 0xce71eu &&
         adapter.Scheduler.LastSubmittedFence == 0xce71eu);
  assert(owner.GpuvaG3Poisoned && rtkit_stops == 0 && adapter.SchedulerFaulted == 0);
  assert(AdmissionDdiRestartFromTimeout(&adapter) == STATUS_SUCCESS && !paused);
  assert(last_recorded.Entry == ADMISSION_GPU_RESET_ADAPTER && last_recorded.Succeeded == 1u);

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

  /* 5. A provider that cannot be polled proves nothing. */
  setup(0, 0); fail_poll = 1;
  memset(&reset, 0, sizeof(reset));
  assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_DEVICE_HARDWARE_ERROR);
  assert(adapter.Scheduler.ActiveFence == 0xce71cu && rtkit_stops == 0 && runtime.Resetting == 0);
  assert(last_recorded.Quiesce == AdmissionGpuResetQuiescePoll);

  /* 6. A worker that never leaves: no exclusive poll ownership, no success. */
  setup(1, 0); runtime.Resetting = 0;
  worker_busy = 0; runtime.WorkIdle.Signaled = 0; /* stuck worker */
  memset(&reset, 0, sizeof(reset));
  assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_DEVICE_HARDWARE_ERROR);
  assert(polls == 0 && adapter.Scheduler.ActiveFence == 0xce71cu && runtime.Resetting == 0);
  assert(last_recorded.Quiesce == AdmissionGpuResetQuiesceWorker && published == 0);

  /* 7. Nothing active (a queued render): unchanged engine reset. */
  setup(0, 0); drop_at_poll = 1;
  assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_SUCCESS); /* ends 0xce71c */
  assert(AppleAgxSchedulerQueueFence(&adapter.Scheduler, 0, 0, 0xce71eu));
  queue_packet(0xce71eu);
  owner.Object.FenceOutstanding = 0xce71eu; polls = 0;
  memset(&reset, 0, sizeof(reset));
  assert(AdmissionDdiResetEngine(&adapter, &reset) == STATUS_SUCCESS);
  assert(reset.LastAbortedFenceId == 0xce71cu && polls == 0);
  assert(last_recorded.Quiesce == AdmissionGpuResetQuiesceNone &&
         last_recorded.PacketState == AdmissionRenderPacketQueued);
  assert(AdmissionRenderPacketState(&adapter.RenderPacket) == AdmissionRenderPacketEmpty);
  assert(owner.Object.FenceOutstanding == 0u && adapter.Scheduler.QueuedFence == 0u);

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
  assert(lock_depth == 0);
  puts("reset quiesce replay ok");
  return 0;
}
'''

if __name__ == '__main__':
    unittest.main()
