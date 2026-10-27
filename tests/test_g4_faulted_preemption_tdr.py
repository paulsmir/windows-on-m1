"""A faulted scheduler must leave preemption unreported until the TDR reset.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP1982b: after the GravityMark stream job timed out, SchedulerFaulted stayed
set with no active job. PreemptCommand still reported DMA_PREEMPTED at once,
so VidSch resubmitted into the frozen queue (rc59 holds paging packets while
faulted), every preemption succeeded, TdrDelay never expired and
DxgkDdiResetEngine never ran: paging waits and the ordered reboot hung. The
invariant: while SchedulerFaulted is set, no caller of the real
AdmissionSchedulerTryNotifyPreemption reports DMA_PREEMPTED and none of them
records a new fault; after the scheduler reset that the TDR performs, the same
real code reports the next boundary preemption normally.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_g4_submit_virtual_replay import function_body

ROOT = Path(__file__).resolve().parents[1]

# The real-scheduler shim of test_preempt_notification_competing_callers
# (that module imports tests.* and runs only from the repository root).
SHIM = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_scheduler.h"
#define _In_
#define _Inout_
#define _Use_decl_annotations_
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_INVALID_PARAMETER (-1)
#define NT_SUCCESS(x) ((x)>=0)
#define RtlZeroMemory(p,n) memset(p,0,n)
#define ADMISSION_SCHEDULER_NODE 0
#define ADMISSION_SCHEDULER_ENGINE 0
#define DXGK_INTERRUPT_DMA_PREEMPTED 3
#define AdmissionRenderPacketQueued 1
#define VOID void
typedef int BOOLEAN,NTSTATUS,KIRQL;
typedef int32_t LONG;
typedef uint32_t UINT,ULONG;
typedef uintptr_t ULONG_PTR;
typedef void *PVOID,*HANDLE;
typedef struct {unsigned InterruptType;struct {unsigned PreemptionFenceId,LastCompletedFenceId,NodeOrdinal,EngineOrdinal;} DmaPreempted;} DXGKARGCB_NOTIFY_INTERRUPT_DATA;
typedef struct {unsigned PreemptionFenceId,NodeOrdinal,EngineOrdinal;struct {unsigned Value;} Flags;} DXGKARG_PREEMPTCOMMAND;
typedef struct {struct {unsigned FenceOutstanding;} Object; unsigned GpuvaG3SecondFence;} ADMISSION_RENDER_CONTEXT;
static void AdmissionContextRetireFence(ADMISSION_RENDER_CONTEXT *c, unsigned f) {
  if (!c || !f) return;
  if (c->Object.FenceOutstanding == f) { c->Object.FenceOutstanding = c->GpuvaG3SecondFence; c->GpuvaG3SecondFence = 0; }
  else if (c->GpuvaG3SecondFence == f) c->GpuvaG3SecondFence = 0;
}
typedef struct {int State;struct {unsigned Fence;uintptr_t ContextToken;} Description;} PACKET;
typedef struct {void *DeviceHandle;NTSTATUS (*DxgkCbSynchronizeExecution)(void*,BOOLEAN(*)(void*),void*,ULONG,BOOLEAN*);
 void (*DxgkCbNotifyInterrupt)(void*,DXGKARGCB_NOTIFY_INTERRUPT_DATA*);BOOLEAN (*DxgkCbQueueDpc)(void*);} INTERFACE;
typedef struct {int PagingLock,SchedulerLock,InterfaceValid;volatile LONG SchedulerInitialized,SchedulerFaulted,SchedulerDpcPending;
 volatile LONG PagingPending,PagingDpcPending,PagingDpcsActive;unsigned DispatchedFence,CpuQueueHead,CpuQueueCount;
 int BackendImage;PACKET RenderPacket;INTERFACE Interface;APPLE_AGX_SCHEDULER Scheduler;
 ULONG PreemptBeganMs,PreemptBeganFence;volatile LONG PreemptRefusals;} ADMISSION_CONTEXT;
typedef struct {ADMISSION_CONTEXT *Context;APPLE_AGX_PREEMPTION Preemption;} ADMISSION_PREEMPTION_NOTIFICATION;
static ADMISSION_CONTEXT *current;
static int locks,injectAfterUnlock,injectInsideNotify,interrupts,dpcs,dispatches,syncFails;
VOID AdmissionSchedulerWorkerFinished(ADMISSION_CONTEXT *);
static BOOLEAN AdmissionSchedulerTryNotifyPreemption(ADMISSION_CONTEXT *);
static LONG InterlockedCompareExchange(volatile LONG *p,LONG v,LONG e){LONG old=*p;if(old==e)*p=v;return old;}
static LONG InterlockedExchange(volatile LONG *p,LONG v){LONG old=*p;*p=v;return old;}
static void KeAcquireSpinLock(int *p,KIRQL *i){(void)p;assert(!locks);*i=2;locks=1;}
static void KeReleaseSpinLock(int *p,KIRQL i){(void)p;(void)i;assert(locks==1);locks=0;}
static void KeAcquireSpinLockAtDpcLevel(int *p){(void)p;++locks;}
static void KeReleaseSpinLockFromDpcLevel(int *p){
 assert(locks>0);--locks;
 if(!locks && current && p==&current->PagingLock && injectAfterUnlock){
  injectAfterUnlock=0;
  /* Another legitimate caller retires the same ready boundary before the
   * original PreemptCommand acts on its saved notifyNow decision. */
  AdmissionSchedulerWorkerFinished(current);
 }
}
static int AdmissionPlatformRenderWorkerScheduled(ADMISSION_CONTEXT *a){(void)a;return 0;}
static unsigned long long KeQueryInterruptTime(void){return 0ull;}
/* EXP1899N receipt-only snapshot: counted, never a scheduler decision */
static void AdmissionSchedulerRecordPreemptRefusalLocked(ADMISSION_CONTEXT *a,const DXGKARG_PREEMPTCOMMAND *c,
 unsigned cutoff,unsigned active,unsigned queued){(void)c;(void)cutoff;(void)active;(void)queued;
 assert(locks==2);++a->PreemptRefusals;}
static int AdmissionRenderPacketState(PACKET *p){return p->State;}
static int AdmissionRenderPacketDiscardQueued(PACKET *p,unsigned f){(void)p;(void)f;assert(0);return 0;}
static int AdmissionBackendImageReleaseSubmission(int *b,unsigned f){(void)b;(void)f;assert(0);return 0;}
static void AdmissionCpuQueueReleaseContextsLocked(ADMISSION_CONTEXT *a){(void)a;assert(locks==2);}
static void AdmissionPagingUpdateIdleLocked(ADMISSION_CONTEXT *a){(void)a;assert(locks==2);}
static void AdmissionDispatchQueuedWork(ADMISSION_CONTEXT *a){(void)a;assert(!locks);++dispatches;}
static NTSTATUS synchronize(void *d,BOOLEAN(*fn)(void*),void *arg,ULONG message,BOOLEAN *result){
 (void)d;assert(!locks && message==0);
 if(syncFails){*result=0;return -1;}
 *result=fn(arg);return 0;
}
static void notify(void *d,DXGKARGCB_NOTIFY_INTERRUPT_DATA *data){
 ADMISSION_CONTEXT *a=d;assert(!locks);
 assert(data->InterruptType==DXGK_INTERRUPT_DMA_PREEMPTED);
 assert(data->DmaPreempted.PreemptionFenceId==77);
 assert(data->DmaPreempted.LastCompletedFenceId==0x731);++interrupts;
 if(injectInsideNotify){
  injectInsideNotify=0;
  assert(AppleAgxSchedulerPreemptionPhase(&a->Scheduler)==AppleAgxPreemptionNotificationClaimed);
  assert(AdmissionSchedulerTryNotifyPreemption(a));
  assert(interrupts==1 && a->Scheduler.PreemptionPending);
 }
}
static BOOLEAN queue_dpc(void *d){(void)d;++dpcs;return 1;}
'''

CASES = r'''
static void setup(ADMISSION_CONTEXT *a){
 memset(a,0,sizeof(*a));current=a;
 locks=injectAfterUnlock=injectInsideNotify=interrupts=dpcs=dispatches=syncFails=0;
 a->InterfaceValid=1;a->SchedulerInitialized=1;
 a->Interface=(INTERFACE){a,synchronize,notify,queue_dpc};
 AppleAgxSchedulerInitialize(&a->Scheduler);
 /* The dropped stream job retired as faulted: completed 0x731, nothing
  * active; a held paging packet 0x732 waits in the frozen queue. */
 assert(AppleAgxSchedulerQueueFence(&a->Scheduler,0,0,0x731));
 assert(AppleAgxSchedulerActivateFence(&a->Scheduler,0,0,0x731));
 assert(AppleAgxSchedulerFaultActiveFence(&a->Scheduler,0,0,0x731));
 assert(AppleAgxSchedulerQueueFence(&a->Scheduler,0,0,0x732));
 a->CpuQueueCount=1;
}
int main(void){
 ADMISSION_CONTEXT a;
 DXGKARG_PREEMPTCOMMAND args={77,0,0,{0}};
 unsigned last=0;
 const LONG fault=0x40000L|4565; /* e.g. the platform worker's poll guard */
 setup(&a);a.SchedulerFaulted=fault;
 assert(AdmissionDdiPreemptCommand(&a,&args)==STATUS_SUCCESS);
 fprintf(stderr,"faulted preemption: phase=%u fault=%x notifications=%d\n",
   (unsigned)a.Scheduler.PreemptionPhase,(unsigned)a.SchedulerFaulted,interrupts);
 /* Old code: DMA_PREEMPTED with LastCompletedFence 0x731 hides the hang. */
 assert(!interrupts && !dpcs);
 assert(a.Scheduler.PreemptionPhase==AppleAgxPreemptionReadyToNotify);
 assert(a.Scheduler.PreemptionPending && a.Scheduler.PendingPreemptionFence==77);
 assert(a.SchedulerFaulted==fault); /* deferral is not a second fault */
 /* The other notifiers (worker exit, DPC retry) defer the same way. */
 AdmissionSchedulerWorkerFinished(&a);
 assert(AdmissionSchedulerTryNotifyPreemption(&a));
 assert(!interrupts && a.Scheduler.PreemptionPhase==AppleAgxPreemptionReadyToNotify);
 /* A second request while the first is outstanding is still refused as
  * before (one preemption at a time); the original code is kept. */
 args.PreemptionFenceId=78;
 assert(AdmissionDdiPreemptCommand(&a,&args)==STATUS_SUCCESS);
 assert(!interrupts && a.SchedulerFaulted==fault && a.PreemptRefusals==1);
 /* TdrDelay expires; DxgkDdiResetEngine resets the scheduler and clears the
  * fault (AdmissionResetEngineInternal). The held fence stays resubmittable. */
 assert(AppleAgxSchedulerResetEngine(&a.Scheduler,0,0,&last));
 assert(last==0x731 && a.Scheduler.PreemptionPhase==AppleAgxPreemptionIdle);
 assert(a.Scheduler.PreemptedFenceCount==1 && a.Scheduler.PreemptedFenceQueue[0]==0x732);
 a.SchedulerFaulted=0;
 /* After the reset the next boundary preemption is reported normally. */
 args.PreemptionFenceId=77;
 assert(AdmissionDdiPreemptCommand(&a,&args)==STATUS_SUCCESS);
 assert(interrupts==1 && dpcs==1 && !a.SchedulerFaulted);
 assert(a.Scheduler.PreemptionPhase==AppleAgxPreemptionIdle);
 /* Healthy control: an unfaulted scheduler still preempts immediately. */
 setup(&a);
 assert(AdmissionDdiPreemptCommand(&a,&args)==STATUS_SUCCESS);
 assert(interrupts==1 && dpcs==1 && !a.SchedulerFaulted);
 return 0;
}
'''


class FaultedPreemptionDefersToTdr(unittest.TestCase):
    def test_faulted_scheduler_never_reports_preemption_before_reset(self):
        source = Path(os.environ.get('AGX_PREEMPT_SCHEDULER_SOURCE',
            ROOT / 'drivers/apple-agx/render-admission/src/scheduler_windows.c')).read_text()
        body = '\n'.join(function_body(source, name) for name in (
            'AdmissionNotifyPreemptionAtInterrupt',
            'AdmissionSchedulerTryNotifyPreemption',
            'AdmissionSchedulerWorkerFinished',
            'AdmissionDdiPreemptCommand'))
        # The shared shim's notify() checks the competing-caller fixture's
        # last completed fence; this fixture completed the same 0x731.
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            (p / 'faulted.c').write_text(SHIM + body + CASES)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                '-Wextra', '-Werror', '-Wno-unused-function',
                '-fsanitize=address,undefined', '-I',
                str(ROOT / 'drivers/apple-agx/shared/include'), str(p / 'faulted.c'),
                str(ROOT / 'drivers/apple-agx/shared/src/apple_agx_scheduler.c'),
                '-o', str(p / 'faulted')], check=True)
            subprocess.run([str(p / 'faulted')], check=True, timeout=10)


if __name__ == '__main__':
    unittest.main()
