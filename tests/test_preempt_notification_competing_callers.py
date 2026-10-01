"""Real PreemptCommand must not poison an adapter when another notifier wins."""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from tests.test_g4_submit_virtual_replay import function_body

ROOT = Path(__file__).resolve().parents[1]


class CompetingPreemptionNotificationTests(unittest.TestCase):
    def test_completed_or_owned_notification_is_not_a_scheduler_fault(self):
        source = Path(os.environ.get('AGX_PREEMPT_SCHEDULER_SOURCE',
            ROOT / 'drivers/apple-agx/render-admission/src/scheduler_windows.c')).read_text()
        body = '\n'.join(function_body(source, name) for name in (
            'AdmissionNotifyPreemptionAtInterrupt',
            'AdmissionSchedulerTryNotifyPreemption',
            'AdmissionSchedulerWorkerFinished',
            'AdmissionDdiPreemptCommand'))
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            (p / 'race.c').write_text(SHIM + body + CASES)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                '-Wextra', '-Werror', '-Wno-unused-function',
                '-fsanitize=address,undefined', '-I',
                str(ROOT / 'drivers/apple-agx/shared/include'), str(p / 'race.c'),
                str(ROOT / 'drivers/apple-agx/shared/src/apple_agx_scheduler.c'),
                '-o', str(p / 'race')], check=True)
            subprocess.run([str(p / 'race')], check=True, timeout=10)


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
typedef struct {struct {unsigned FenceOutstanding;} Object;} ADMISSION_RENDER_CONTEXT;
typedef struct {int State;struct {unsigned Fence;uintptr_t ContextToken;} Description;} PACKET;
typedef struct {void *DeviceHandle;NTSTATUS (*DxgkCbSynchronizeExecution)(void*,BOOLEAN(*)(void*),void*,ULONG,BOOLEAN*);
 void (*DxgkCbNotifyInterrupt)(void*,DXGKARGCB_NOTIFY_INTERRUPT_DATA*);BOOLEAN (*DxgkCbQueueDpc)(void*);} INTERFACE;
typedef struct {int PagingLock,SchedulerLock,InterfaceValid;volatile LONG SchedulerInitialized,SchedulerFaulted,SchedulerDpcPending;
 volatile LONG PagingPending,PagingDpcPending,PagingDpcsActive;unsigned DispatchedFence,CpuQueueHead,CpuQueueCount;
 int BackendImage;PACKET RenderPacket;INTERFACE Interface;APPLE_AGX_SCHEDULER Scheduler;} ADMISSION_CONTEXT;
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
 assert(AppleAgxSchedulerQueueFence(&a->Scheduler,0,0,0x731));
 assert(AppleAgxSchedulerActivateFence(&a->Scheduler,0,0,0x731));
 assert(AppleAgxSchedulerCompleteActiveFence(&a->Scheduler,0,0,0x731));
 assert(AppleAgxSchedulerQueueFence(&a->Scheduler,0,0,0x732));
 a->CpuQueueCount=1;
}
int main(void){
 ADMISSION_CONTEXT a;
 DXGKARG_PREEMPTCOMMAND args={77,0,0,{0}};
 setup(&a);injectAfterUnlock=1;
 assert(AdmissionDdiPreemptCommand(&a,&args)==STATUS_SUCCESS);
 fprintf(stderr,"competing notifier: phase=%u fault=%d notifications=%d completed=%x last=%x\n",
   (unsigned)a.Scheduler.PreemptionPhase,a.SchedulerFaulted,interrupts,
   a.Scheduler.CompletedFence,a.Scheduler.LastSubmittedFence);
 assert(interrupts==1 && dpcs==1 && !locks);
 assert(a.Scheduler.PreemptionPhase==AppleAgxPreemptionIdle);
 assert(a.Scheduler.PreemptedFenceCount==1 && a.Scheduler.PreemptedFenceQueue[0]==0x732);
 assert(!a.SchedulerFaulted); /* Old code poisons the healthy adapter here. */
 assert(AppleAgxSchedulerQueueFence(&a.Scheduler,0,0,0x735));
 setup(&a);injectInsideNotify=1;
 assert(AdmissionDdiPreemptCommand(&a,&args)==STATUS_SUCCESS);
 assert(interrupts==1 && dpcs==1 && !a.SchedulerFaulted);
 setup(&a);
 assert(AdmissionDdiPreemptCommand(&a,&args)==STATUS_SUCCESS);
 assert(interrupts==1 && dpcs==1 && !a.SchedulerFaulted);
 /* Genuine synchronization failure must retain fail-closed behavior. */
 setup(&a);syncFails=1;
 assert(AdmissionDdiPreemptCommand(&a,&args)==STATUS_SUCCESS);
 assert(a.SchedulerFaulted && !interrupts);
 LONG firstFault=a.SchedulerFaulted;
 assert((firstFault >> 16)==1 && (firstFault & 0xffff)!=0);
 args.PreemptionFenceId=78; /* Already claimed after the real sync failure. */
 assert(AdmissionDdiPreemptCommand(&a,&args)==STATUS_SUCCESS);
 assert(a.SchedulerFaulted==firstFault);
 args.PreemptionFenceId=77;
 /* A waiting boundary is still owned and must not be acknowledged early. */
 setup(&a);assert(AppleAgxSchedulerActivateFence(&a.Scheduler,0,0,0x732));
 assert(AppleAgxSchedulerBeginBoundaryPreemption(&a.Scheduler,0,0,77,0x732,0x732));
 assert(AdmissionSchedulerTryNotifyPreemption(&a));
 assert(!interrupts && !a.SchedulerFaulted && a.Scheduler.ActiveFence==0x732 &&
        a.Scheduler.PreemptionPhase==AppleAgxPreemptionWaitCurrentBoundary);
 /* Unknown internal phases are not legitimate competing owners. */
 setup(&a);a.Scheduler.PreemptionPhase=(APPLE_AGX_PREEMPTION_PHASE)99;
 assert(!AdmissionSchedulerTryNotifyPreemption(&a));
 assert(!interrupts);
 return 0;
}
'''
