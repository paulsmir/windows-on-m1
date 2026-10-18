"""A queued render's worker reservation must retire before OS resubmission.

WHAT REAL BUG: EXP913 DWM fences3923/8530/10554 are rejected at runtime
predicate8 because DMA_PREEMPTED was published while WorkScheduled remained1.
Replay actual notification and worker retirement code with the real scheduler.
"""
from pathlib import Path
import os, subprocess, tempfile, unittest
from test_g4_submit_virtual_replay import function_body
ROOT=Path(__file__).resolve().parents[1]

class RenderPreemptWorkerRetirement(unittest.TestCase):
    def test_preemption_does_not_offer_resubmission_before_worker_retirement(self):
        src=ROOT/'drivers/apple-agx/render-admission/src'
        scheduler=Path(os.environ.get('AGX_PREEMPT_SCHEDULER_SOURCE',src/'scheduler_windows.c')).read_text()
        backend=Path(os.environ.get('AGX_PREEMPT_BACKEND_SOURCE',src/'backend_platform_windows.c')).read_text()
        extra=''
        if 'AdmissionPlatformRenderWorkerScheduled(' in backend:
            extra+=function_body(backend,'AdmissionPlatformRenderWorkerScheduled')+'\n'
        extra+='\n'.join(function_body(backend,n) for n in
            ('AdmissionPlatformRuntimeReadyEx','AdmissionPlatformRuntimeReady'))
        extra+='\n'+'\n'.join(function_body(scheduler,n) for n in
            ('AdmissionNotifyPreemptionAtInterrupt','AdmissionSchedulerTryNotifyPreemption'))
        if 'AdmissionSchedulerWorkerFinished(' in scheduler:
            extra+='\n'+function_body(scheduler,'AdmissionSchedulerWorkerFinished')
        extra+='\n'+function_body(backend,'AdmissionPlatformWorkerFinished')
        program=SHIM+extra+CASES
        with tempfile.TemporaryDirectory() as d:
            d=Path(d);(d/'replay.c').write_text(program)
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                '-Wno-unused-function','-fsanitize=address,undefined','-I',str(ROOT/'drivers/apple-agx/shared/include'),
                str(d/'replay.c'),str(ROOT/'drivers/apple-agx/shared/src/apple_agx_scheduler.c'),'-o',str(d/'replay')],check=True)
            subprocess.run([str(d/'replay')],check=True,timeout=10)

SHIM=r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_scheduler.h"
#define _In_
#define _Inout_
#define _Use_decl_annotations_
#define VOID void
#define TRUE 1
#define FALSE 0
#define IO_NO_INCREMENT 0
#define NT_SUCCESS(s) ((s)>=0)
#define DXGK_INTERRUPT_DMA_PREEMPTED 3
#define ADMISSION_SCHEDULER_NODE 0
#define ADMISSION_SCHEDULER_ENGINE 0
#define AppleAgxBackendRuntimeReady 2
#define RtlZeroMemory(p,n) memset(p,0,n)
typedef int BOOLEAN,NTSTATUS,KIRQL;typedef int32_t LONG;typedef unsigned ULONG;typedef void *PVOID;
typedef struct {unsigned InterruptType;struct {unsigned PreemptionFenceId,LastCompletedFenceId,NodeOrdinal,EngineOrdinal;} DmaPreempted;} DXGKARGCB_NOTIFY_INTERRUPT_DATA;
typedef struct {void *DeviceHandle;NTSTATUS (*DxgkCbSynchronizeExecution)(void*,BOOLEAN(*)(void*),void*,ULONG,BOOLEAN*);
 void (*DxgkCbNotifyInterrupt)(void*,DXGKARGCB_NOTIFY_INTERRUPT_DATA*);BOOLEAN (*DxgkCbQueueDpc)(void*);} INTERFACE;
typedef struct {void *PlatformRuntime;int SchedulerLock,InterfaceValid;volatile LONG SchedulerInitialized,SchedulerFaulted,SchedulerDpcPending;
 volatile LONG PagingPending,PagingDpcPending,PagingDpcsActive;ULONG DispatchedFence;INTERFACE Interface;APPLE_AGX_SCHEDULER Scheduler;} ADMISSION_CONTEXT;
typedef struct {ADMISSION_CONTEXT *Adapter;int ProviderReady,BackendStarted;struct {int Phase;} Backend;void *WorkItem;
 volatile LONG Stopping,Resetting,WorkScheduled,WorkersActive;int WorkIdle,SlotEvent;} ADMISSION_PLATFORM_RUNTIME;
typedef struct {ADMISSION_CONTEXT *Context;APPLE_AGX_PREEMPTION Preemption;} ADMISSION_PREEMPTION_NOTIFICATION;
static int locks,interrupts,dpcs,dispatches;
static ULONG enqueueOnDpc, expectedPreempt=77, expectedCompleted;
static int replayPaging, activations;
static ADMISSION_PLATFORM_RUNTIME *current;
static void KeAcquireSpinLock(int *p,int *i){(void)p;*i=2;assert(!locks);locks=1;}
static void KeReleaseSpinLock(int *p,int i){(void)p;(void)i;assert(locks);locks=0;}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG v,LONG e){LONG o=*p;if(o==e)*p=v;return o;}
static LONG InterlockedExchange(volatile LONG *p,LONG v){LONG o=*p;*p=v;return o;}
static LONG InterlockedDecrement(volatile LONG *p){assert(*p>0);return --*p;}
static void KeSetEvent(int *p,int n,int w){(void)n;(void)w;if(current&&p==&current->SlotEvent){*p=1;return;}assert(locks);*p=1;}
static void AdmissionDispatchQueuedWork(ADMISSION_CONTEXT *c){
 assert(!locks);++dispatches;
 if(enqueueOnDpc && !AppleAgxSchedulerDispatchBlocked(&c->Scheduler) &&
    AppleAgxSchedulerQueuedFence(&c->Scheduler,0,0)==enqueueOnDpc &&
    !AppleAgxSchedulerActiveFence(&c->Scheduler,0,0)){
  assert(AppleAgxSchedulerActivateFence(&c->Scheduler,0,0,enqueueOnDpc));++activations;
 }
}
static NTSTATUS sync_callback(void *d,BOOLEAN(*fn)(void*),void *arg,ULONG msg,BOOLEAN *result){(void)d;assert(!locks && !msg);*result=fn(arg);return 0;}
static void notify_callback(void *d,DXGKARGCB_NOTIFY_INTERRUPT_DATA *data){
 (void)d;fprintf(stderr,"DMA_PREEMPTED WorkScheduled=%d last_completed=%u\n",current->WorkScheduled,data->DmaPreempted.LastCompletedFenceId);
 assert(current->WorkScheduled==0);assert(data->InterruptType==DXGK_INTERRUPT_DMA_PREEMPTED);
 assert(data->DmaPreempted.PreemptionFenceId==expectedPreempt);assert(data->DmaPreempted.LastCompletedFenceId==expectedCompleted);++interrupts;
}
static BOOLEAN queue_dpc(void *d){
 ADMISSION_CONTEXT *a=d;++dpcs;
 if(enqueueOnDpc){
  int accepted=replayPaging ? AppleAgxSchedulerQueueResubmittedPagingFence(&a->Scheduler,0,0,enqueueOnDpc)
                           : AppleAgxSchedulerQueueFence(&a->Scheduler,0,0,enqueueOnDpc);
  fprintf(stderr,"paging enqueue during notification fence=%x phase=%u accepted=%d\n",enqueueOnDpc,(unsigned)a->Scheduler.PreemptionPhase,accepted);
  assert(accepted);
  assert(AppleAgxSchedulerDispatchBlocked(&a->Scheduler));
  assert(!AppleAgxSchedulerActivateFence(&a->Scheduler,0,0,enqueueOnDpc));
  AdmissionDispatchQueuedWork(a); /* The DPC can finish before Commit returns. */
  assert(!activations);
 }
 return 1;
}
'''
CASES=r'''
static void setup(ADMISSION_CONTEXT *a,ADMISSION_PLATFORM_RUNTIME *r){
 memset(a,0,sizeof(*a));memset(r,0,sizeof(*r));current=r;interrupts=dpcs=dispatches=0;
 enqueueOnDpc=0;expectedPreempt=77;expectedCompleted=0;replayPaging=activations=0;
 a->PlatformRuntime=r;a->InterfaceValid=1;a->SchedulerInitialized=1;
 a->Interface=(INTERFACE){a,sync_callback,notify_callback,queue_dpc};
 r->Adapter=a;r->ProviderReady=r->BackendStarted=1;r->Backend.Phase=2;r->WorkItem=r;
 r->WorkScheduled=1;r->WorkersActive=1;AppleAgxSchedulerInitialize(&a->Scheduler);
 assert(AppleAgxSchedulerQueueFence(&a->Scheduler,0,0,10));
 assert(AppleAgxSchedulerBeginBoundaryPreemption(&a->Scheduler,0,0,77,10,0));
}
int main(void){
 ADMISSION_CONTEXT a;ADMISSION_PLATFORM_RUNTIME r;setup(&a,&r);
 assert(!AdmissionPlatformRuntimeReady(&a));
 assert(AdmissionSchedulerTryNotifyPreemption(&a));
 assert(!interrupts && AppleAgxSchedulerPreemptionPhase(&a.Scheduler)==AppleAgxPreemptionReadyToNotify);
 AdmissionPlatformWorkerFinished(&r);
 assert(interrupts==1 && dpcs==1 && !a.SchedulerFaulted && !locks && dispatches>=1);
 assert(AdmissionPlatformRuntimeReady(&a));
 assert(AppleAgxSchedulerPreemptionPhase(&a.Scheduler)==AppleAgxPreemptionIdle);
 assert(AppleAgxSchedulerQueueFence(&a.Scheduler,0,0,11));
 /* Outstanding render or CPU completion must still be reported first. */
 setup(&a,&r);a.DispatchedFence=10;AdmissionPlatformWorkerFinished(&r);assert(!interrupts);
 a.DispatchedFence=0;assert(AdmissionSchedulerTryNotifyPreemption(&a));assert(interrupts==1);
 setup(&a,&r);a.PagingPending=1;AdmissionPlatformWorkerFinished(&r);assert(!interrupts);
 a.PagingPending=0;assert(AdmissionSchedulerTryNotifyPreemption(&a));assert(interrupts==1);
 setup(&a,&r);r.Stopping=1;AdmissionPlatformWorkerFinished(&r);assert(!interrupts && !dispatches);
 setup(&a,&r);r.Resetting=1;AdmissionPlatformWorkerFinished(&r);assert(!interrupts && !dispatches);
 setup(&a,&r);AppleAgxSchedulerInitialize(&a.Scheduler);AdmissionPlatformWorkerFinished(&r);assert(!interrupts && !a.SchedulerFaulted);
 /* An already retired worker keeps the immediate preemption path. */
 setup(&a,&r);r.WorkScheduled=0;assert(AdmissionSchedulerTryNotifyPreemption(&a));assert(interrupts==1);
 /* EXP915 exact empty-queue dump: normal paging F972 follows completedF971
  * while the old notification is already visible to Windows. */
 setup(&a,&r);AppleAgxSchedulerInitialize(&a.Scheduler);
 a.Scheduler.CompletedFence=a.Scheduler.LastSubmittedFence=0xf971;
 expectedPreempt=0x117;expectedCompleted=0xf971;
 assert(AppleAgxSchedulerBeginBoundaryPreemption(&a.Scheduler,0,0,0x117,0xf971,0));
 r.WorkScheduled=0;enqueueOnDpc=0xf972;
 assert(AdmissionSchedulerTryNotifyPreemption(&a));
 assert(activations==1 && a.Scheduler.ActiveFence==0xf972 && a.Scheduler.CompletedFence==0xf971);
 assert(!a.SchedulerFaulted && interrupts==1 && dpcs==1);
 /* Same-ID paging replay has the same admission boundary. */
 setup(&a,&r);r.WorkScheduled=0;enqueueOnDpc=10;replayPaging=1;
 assert(AdmissionSchedulerTryNotifyPreemption(&a));
 assert(activations==1 && a.Scheduler.ActiveFence==10 && a.Scheduler.CompletedFence==0);
 puts("preemption waits for reserved worker; exact notification then valid resubmission; no fake completion");return 0;
}
'''
