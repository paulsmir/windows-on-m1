"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A dropped firmware job must retire as an aborted fence and notify Windows of
an error exactly once, without advancing successful completion or poisoning a
second context. Valid fault VA/write/level must be attached only to the matching
fence/context; stale or unread register data must stay unknown. Exercise the production completion callbacks and scheduler;
only WDK services, image/graph release and the packet container are modeled.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'
RENDER = ROOT / 'drivers/apple-agx/render-admission'


class SurvivableFaultRecovery(unittest.TestCase):
    def test_real_backend_aborts_and_reports_only_the_culprit(self):
        source = (RENDER / 'src/backend_platform_windows.c').read_text()
        notification = re.search(r'typedef struct _ADMISSION_COMPLETION_NOTIFICATION \{.*?'
                                 r'} ADMISSION_COMPLETION_NOTIFICATION;', source, re.S)[0]
        program = SHIM + notification + '\n' + '\n'.join(
            re.search(r'static (?:BOOLEAN|APPLE_AGX_BACKEND_BOOL) ' + name + r'\(.*?^}',
                      source, re.S | re.M)[0] for name in (
                'AdmissionNotifyCompletionAtInterrupt', 'AdmissionBackendComplete')) + CASES
        with tempfile.TemporaryDirectory() as tmp:
            path = Path(tmp) / 'recovery.c'
            path.write_text(program)
            binary = Path(tmp) / 'recovery'
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                            '-Werror', '-fsanitize=address,undefined', '-I', str(SHARED / 'include'),
                            '-I', str(RENDER / 'include'), str(path),
                            str(SHARED / 'src/apple_agx_scheduler.c'),
                            str(SHARED / 'src/apple_agx_submission.c'), '-o', str(binary)], check=True)
            subprocess.run([str(binary)], check=True)


SHIM = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "apple_agx_scheduler.h"
#include "apple_agx_submission.h"
#include "apple_agx_backend_runtime.h"
#include "render_gpu_fault.h"
#define APPLE_AGX_GPUVA_G3_QUALIFICATION 1
#define TRUE 1
#define FALSE 0
#define NT_SUCCESS(x) ((x) >= 0)
#define RtlZeroMemory memset_zero
#define DXGK_INTERRUPT_DMA_COMPLETED 1
#define DXGK_INTERRUPT_DMA_PAGE_FAULTED 9
#define DXGK_RENDER_PIPELINE_STAGE_UNKNOWN 0
#define DXGK_PAGE_FAULT_WRITE 1
#define AdmissionRenderPacketActive 2
#define AdmissionJobPhaseComplete 1
#define AdmissionJobPhaseJobEnd 2
#define AdmissionJobPhaseNotify 3
typedef int BOOLEAN, NTSTATUS, KIRQL;
typedef int32_t LONG;
typedef unsigned UINT, ULONG, DXGK_PAGE_FAULT_FLAGS;
typedef uint64_t UINT64, ULONGLONG;
typedef uintptr_t ULONG_PTR;
typedef void *PVOID;
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
typedef struct {
  struct { unsigned FenceOutstanding; } Object;
  BOOLEAN GpuvaG3Poisoned;
  void *GpuvaG3Process;
} ADMISSION_RENDER_CONTEXT;
typedef struct {
  unsigned State;
  struct { unsigned Fence; UINT64 ContextToken; } Description;
} PACKET;
typedef struct {
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
  LONG RenderDpcFence, SchedulerDpcPending;
} ADMISSION_CONTEXT;
typedef struct {
  ADMISSION_CONTEXT *Adapter;
  APPLE_AGX_COMPLETION_TRANSACTION Completion;
  ADMISSION_RENDER_CONTEXT *CompletionContext;
  APPLE_AGX_BACKEND_RUNTIME Backend;
  unsigned LastFirmwareRecoveryKind;
  ADMISSION_GPU_FAULT_RECEIPT GpuFault;
} ADMISSION_PLATFORM_RUNTIME;
static unsigned notifications, dpcs, released, lock_depth, sync_fail;
static DXGKARGCB_NOTIFY_INTERRUPT_DATA observed;
static void memset_zero(void *p, size_t n) { memset(p, 0, n); }
static void KeAcquireSpinLock(unsigned *l, KIRQL *irql) { (void)l; *irql=0; assert(!lock_depth++); }
static void KeReleaseSpinLock(unsigned *l, KIRQL irql) { (void)l; (void)irql; assert(lock_depth--==1); }
static LONG InterlockedExchange(LONG *p, LONG v) { LONG old=*p; *p=v; return old; }
static UINT64 KeQueryInterruptTime(void) { return 1; }
static unsigned AdmissionRenderPacketState(PACKET *p) { return p->State; }
static int AdmissionRenderPacketComplete(PACKET *p, unsigned f) {
  assert(p->State==2 && p->Description.Fence==f); p->State=0; return 1;
}
static void AdmissionContextRetireFence(ADMISSION_RENDER_CONTEXT *c, unsigned f) {
  assert(c->Object.FenceOutstanding==f); c->Object.FenceOutstanding=0;
}
static void AdmissionJobTimingMarkWindows(void *r, unsigned f, unsigned phase) { (void)r; (void)f; (void)phase; }
/* Timestamp capture is telemetry only; the real ring/capture has its own replay. */
static void AdmissionGpuCompletionCaptureWindows(ADMISSION_PLATFORM_RUNTIME *r, unsigned f,
    APPLE_AGX_BACKEND_COMPLETION_STATUS status) { (void)r; (void)f; (void)status; }
static void AdmissionJobTimingFirmwareWindows(void *r, unsigned f) { (void)r; (void)f; }
static int AdmissionSaveG4Manager(void *r, unsigned f) { (void)r; (void)f; return 1; }
static int AdmissionGpuvaG3CompleteJob(void *a, unsigned f) { (void)a; (void)f; return 1; }
static int AdmissionDynamicOverlayReleaseActive(void *r, unsigned f) { (void)r; (void)f; return 1; }
static int AdmissionBackendImageReleaseSubmissionRestore(void *a, void *s, unsigned f) {
  (void)a; (void)s; (void)f; ++released; return 1;
}
static int AdmissionGpuvaG3PrivateReported(void *a, void *c, unsigned f) { (void)a; (void)c; (void)f; return 1; }
static void AdmissionRenderCorrelationNotifyAtInterruptWindows(void *a, unsigned f, UINT64 t, BOOLEAN q) {
  (void)a; (void)f; (void)t; (void)q;
}
static void AdmissionRenderCorrelationSynchronizeWindows(void *a, unsigned f, NTSTATUS s, BOOLEAN r) {
  (void)a; (void)f; (void)s; (void)r;
}
static void AdmissionGdiReceiptCompleteWindows(void *a, unsigned f, ULONG s, BOOLEAN r) {
  (void)a; (void)f; (void)s; (void)r;
}
static void notify(void *a, DXGKARGCB_NOTIFY_INTERRUPT_DATA *data) {
  (void)a; observed=*data; ++notifications;
}
static BOOLEAN dpc(void *a) { (void)a; ++dpcs; return TRUE; }
static NTSTATUS sync_callback(void *a, BOOLEAN (*fn)(PVOID), PVOID p, unsigned n, BOOLEAN *result) {
  (void)a; (void)n; assert(!lock_depth); if (sync_fail) return -1; *result=fn(p); return 0;
}
'''

CASES = r'''
int main(void) {
  unsigned mode;
  for (mode=0; mode<11; ++mode) {
    ADMISSION_CONTEXT a={0};
    ADMISSION_PLATFORM_RUNTIME r={0};
    ADMISSION_RENDER_CONTEXT culprit={0}, other={0};
    APPLE_AGX_U32 aborted;
    APPLE_AGX_BACKEND_COMPLETION_STATUS status = mode==0
        ? AppleAgxBackendCompletionSuccess : AppleAgxBackendCompletionDropped;
    notifications=dpcs=released=lock_depth=sync_fail=0;
    r.Adapter=&a; r.LastFirmwareRecoveryKind=4;
    a.InterfaceValid=1; a.Interface.DxgkCbNotifyInterrupt=notify;
    a.Interface.DxgkCbQueueDpc=dpc; a.Interface.DxgkCbSynchronizeExecution=sync_callback;
    AppleAgxSchedulerInitialize(&a.Scheduler);
    a.Scheduler.CompletedFence=a.Scheduler.LastSubmittedFence=40;
    assert(AppleAgxSchedulerQueueFence(&a.Scheduler,0,0,41));
    assert(AppleAgxSchedulerActivateFence(&a.Scheduler,0,0,41));
    assert(AppleAgxSchedulerQueueFence(&a.Scheduler,0,0,42));
    a.RenderPacket.State=2; a.RenderPacket.Description.Fence=41;
    a.RenderPacket.Description.ContextToken=(UINT64)(uintptr_t)&culprit;
    culprit.Object.FenceOutstanding=41; other.Object.FenceOutstanding=42;
    if (mode>=3) {
      ADMISSION_GPU_FAULT_EVENT *fault=&r.GpuFault.Last;
      fault->Fence=41; fault->ContextToken=(UINT64)(uintptr_t)&culprit;
      fault->Flags=ADMISSION_GPU_FAULT_INFO_READ|ADMISSION_GPU_FAULT_INFO_VALID|
                   ADMISSION_GPU_FAULT_JOB_KNOWN;
      fault->FaultInfo=((0x1113ba0000ULL>>6)<<30)|1u|(2u<<7);
      if (mode!=4) fault->FaultInfo|=1u<<4; /* read */
      if (mode==5) fault->Fence=40;
      if (mode==6) fault->ContextToken=(UINT64)(uintptr_t)&other;
      if (mode==7) fault->FaultInfo&=~1ULL;
      if (mode==8) fault->Flags=0; /* opt-in read absent: never decode sentinel */
      if (mode==10) fault->FaultInfo=ADMISSION_GPU_FAULT_INFO_NOT_READ;
      if (mode==9) fault->FaultInfo=((0x8000000040ULL>>6)<<30)|1u|(1u<<4);
    }
    if (mode==2) {
      sync_fail=1;
      assert(!AdmissionBackendComplete(&r,41,0,0,status));
      assert(!notifications && released==1 && a.Scheduler.LastAbortedFence==41);
      sync_fail=0; /* retry must not complete the aborted fence or release twice */
    }
    assert(AdmissionBackendComplete(&r,41,0,0,status));
    assert(notifications==1 && dpcs==1 && released==1 && !lock_depth);
    assert(!culprit.Object.FenceOutstanding && other.Object.FenceOutstanding==42);
    assert(!other.GpuvaG3Poisoned && culprit.GpuvaG3Poisoned==(mode!=0));
    if (mode==0) {
      assert(observed.InterruptType==DXGK_INTERRUPT_DMA_COMPLETED);
      assert(a.Scheduler.CompletedFence==41 && !a.Scheduler.LastAbortedFence);
    } else {
      assert(observed.InterruptType==DXGK_INTERRUPT_DMA_PAGE_FAULTED);
      assert(observed.DmaPageFaulted.FaultedFenceId==41);
      assert(observed.DmaPageFaulted.PageFaultFlags==(mode==4 ? DXGK_PAGE_FAULT_WRITE : 0));
      assert(observed.DmaPageFaulted.FaultedVirtualAddress==
          (mode==3 || mode==4 ? 0x1113ba0000ULL : mode==9 ? 0xffffff8000000040ULL : 0));
      assert(observed.DmaPageFaulted.PageTableLevel==(mode==3 || mode==4 ? 2u : 0u));
      assert(observed.DmaPageFaulted.FaultedPrimitiveAPISequenceNumber==~(UINT64)0);
      assert(observed.DmaPageFaulted.FaultedBindTableEntry==~(UINT)0);
      assert(observed.DmaPageFaulted.FaultErrorCode.IsDeviceSpecificCode==1);
      assert(observed.DmaPageFaulted.FaultErrorCode.DeviceSpecificCode==4);
      assert(a.Scheduler.CompletedFence==40 && a.Scheduler.LastAbortedFence==41);
    }
    assert(!AdmissionBackendComplete(&r,41,0,0,status));
    assert(notifications==1 && released==1);
    assert(AppleAgxSchedulerResetEngine(&a.Scheduler,0,0,&aborted));
    assert(aborted==41 && a.Scheduler.CompletedFence==(mode==0 ? 41u : 40u));
    assert(AppleAgxSchedulerQueueResubmittedPagingFence(&a.Scheduler,0,0,42));
  }
  return 0;
}
'''
