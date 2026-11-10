"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Node1 preemption/reset/query must not acknowledge, reset, or advance node0's
identically numbered active render fence. Compile the real scheduler DDIs;
ResetEngine uses its complete node1 branch, excluding unrelated GPU reset.
"""
import re, unittest
import test_g4_paging_serialization as harness
import test_preempt_notification_competing_callers as preempt
from g3_vidmm_replay import body

CASES=r'''
int main(void){
 ADMISSION_CONTEXT a={0};DXGKARG_PREEMPTCOMMAND p={77,1,0,{0}};
 DXGKARG_QUERYCURRENTFENCE q={1,0,0};DXGKARG_RESETENGINE reset={1,0,0};
 DXGKARG_QUERYDEPENDENTENGINEGROUP group={1,0,0};
 a.InterfaceValid=a.SchedulerInitialized=1;a.LiveAdditivePaging=1;current=&a;
 a.Interface=(INTERFACE){&a,synchronize,notify,queue_dpc};
 AppleAgxSchedulerInitialize(&a.Scheduler);AppleAgxSchedulerInitialize(&a.PagingScheduler);
 assert(AppleAgxSchedulerQueueFence(&a.Scheduler,0,0,41));
 assert(AppleAgxSchedulerActivateFence(&a.Scheduler,0,0,41));a.DispatchedFence=41;
 assert(AppleAgxSchedulerQueueFence(&a.PagingScheduler,0,0,41));
 assert(AppleAgxSchedulerActivateFence(&a.PagingScheduler,0,0,41));
 assert(AppleAgxSchedulerQueueFence(&a.PagingScheduler,0,0,42));a.PagingNodeCount=1;
 a.PagingActiveNode=1;a.PagingPending=1;a.PagingNodeDispatchedFence=41;
 assert(AdmissionDdiPreemptCommand(&a,&p)==0&&!interrupts);
 assert(a.PagingNodeCount==0&&a.PagingScheduler.PreemptedFenceCount==1);
 assert(a.Scheduler.ActiveFence==41&&a.Scheduler.CompletedFence==0);
 assert(AdmissionResetEngineInternal(&a,&reset)==STATUS_DEVICE_BUSY);
 assert(AdmissionDdiQueryCurrentFence(&a,&q)==0&&q.CurrentFence==0);
 assert(AppleAgxSchedulerCompleteActiveFence(&a.PagingScheduler,0,0,41));
 assert(AppleAgxSchedulerObserveBoundaryCompletion(&a.PagingScheduler,0,0,41));
 a.PagingPending=0;a.PagingNodeDispatchedFence=0;
 AdmissionSchedulerWorkerFinished(&a);
 assert(interrupts==1&&last_node==1&&last_completed==41);
 assert(a.Scheduler.ActiveFence==41&&a.DispatchedFence==41&&!a.Scheduler.CompletedFence);
 assert(AdmissionDdiQueryCurrentFence(&a,&q)==0&&q.CurrentFence==41);
 assert(AdmissionDdiQueryDependentEngineGroup(&a,&group)==0&&group.DependentNodeOrdinalMask==3);
 assert(AppleAgxSchedulerQueueResubmittedPagingFence(&a.PagingScheduler,0,0,42));
 a.PagingNodeCount=1;
 assert(AdmissionResetEngineInternal(&a,&reset)==0&&a.PagingNodeCount==0);
 assert(a.Scheduler.ActiveFence==41&&a.DispatchedFence==41&&!a.Scheduler.CompletedFence);
 assert(AppleAgxSchedulerQueueResubmittedPagingFence(&a.PagingScheduler,0,0,42));
 a.LiveAdditivePaging=0;
 assert(AdmissionDdiQueryCurrentFence(&a,&q)==STATUS_INVALID_PARAMETER);
 assert(AdmissionDdiPreemptCommand(&a,&p)==STATUS_INVALID_PARAMETER);
 assert(AdmissionResetEngineInternal(&a,&reset)==STATUS_INVALID_PARAMETER);
 return 0;
}
'''

class LiveNodeControl(unittest.TestCase):
    compile_run=harness.PagingSerialization.compile_run
    def test_real_node_one_preemption_reset_and_queries(self):
        source=(harness.RENDER/'src/scheduler_windows.c').read_text()
        shim=preempt.SHIM.replace('ULONG PreemptBeganMs,PreemptBeganFence;',
            'ULONG PagingNodeDispatchedFence;struct {ULONG Stage;} GpuReset;ULONG PreemptBeganMs,PreemptBeganFence;')
        start=shim.index('static void notify(')
        end=shim.index('static BOOLEAN queue_dpc',start)
        shim=shim[:start]+'''static unsigned last_node,last_completed;
static void notify(void *d,DXGKARGCB_NOTIFY_INTERRUPT_DATA *data){
 (void)d;assert(!locks);++interrupts;
 last_node=data->DmaPreempted.NodeOrdinal;last_completed=data->DmaPreempted.LastCompletedFenceId;
}
'''+shim[end:]
        shim+='''
#define STATUS_DEVICE_BUSY (-4)
#define STATUS_INVALID_DEVICE_STATE (-5)
#define PAGED_CODE() ((void)0)
#define AdmissionGpuResetStageValidate 1
#define AdmissionRenderCorrelationQueryFenceWindows(c,f) ((void)(c),(void)(f))
typedef int ADMISSION_RENDER_PACKET_STATE;
typedef struct {unsigned NodeOrdinal,EngineOrdinal,CurrentFence;} DXGKARG_QUERYCURRENTFENCE;
typedef struct {unsigned NodeOrdinal,EngineOrdinal,LastAbortedFenceId;} DXGKARG_RESETENGINE;
typedef struct {unsigned NodeOrdinal,EngineOrdinal;unsigned long long DependentNodeOrdinalMask;} DXGKARG_QUERYDEPENDENTENGINEGROUP;
#pragma clang diagnostic ignored "-Wunused-variable"
'''
        for name in ('AdmissionNotifyPreemptionAtInterrupt','AdmissionSchedulerTryNotifyPreemption',
                     'AdmissionSchedulerWorkerFinished','AdmissionDdiPreemptCommand',
                     'AdmissionDdiQueryCurrentFence','AdmissionDdiQueryDependentEngineGroup'):
            shim+='\n'+body(source,name)
        reset=body(source.replace('static __declspec(noinline)', 'static'),'AdmissionResetEngineInternal')
        reset=reset[:reset.index('  context->GpuReset.Stage = AdmissionGpuResetStageQuiesce;')]
        shim+='\n'+reset+'  return STATUS_INVALID_PARAMETER;\n}\n'
        self.compile_run('#pragma clang diagnostic ignored "-Wunused-variable"\n'+shim+CASES,[harness.SHARED/'src/apple_agx_scheduler.c'],[harness.SHARED/'include'])

    def test_actual_adapter_reset_gates_both_nodes(self):
        source=(harness.RENDER/'src/scheduler_windows.c').read_text()
        shim=r'''
#include <assert.h>
#include <string.h>
#include "apple_agx_scheduler.h"
#define _Use_decl_annotations_
#define RtlZeroMemory(p,n) memset(p,0,n)
#define TRUE 1
#define STATUS_INVALID_PARAMETER (-1)
#define NT_SUCCESS(s) ((s)>=0)
#define ADMISSION_GPU_RESET_ADAPTER 2
typedef void *HANDLE;typedef int NTSTATUS,KIRQL,LONG;
typedef struct {unsigned NodeOrdinal,EngineOrdinal,LastAbortedFenceId;} DXGKARG_RESETENGINE;
typedef struct {unsigned LiveAdditivePaging;LONG PagingAdapterResetting;int SchedulerLock;
 APPLE_AGX_SCHEDULER Scheduler,PagingScheduler;struct {int HaltObserve;} GpuReset;} ADMISSION_CONTEXT;
static unsigned calls,fail_second;
/* TDR-H0 halt observation: switch off here. */
static void AdmissionPlatformRuntimeTdrHaltObserve(ADMISSION_CONTEXT *c,int *o){(void)c;(void)o;}
static LONG InterlockedExchange(LONG *p,LONG v){LONG old=*p;*p=v;return old;}
static void KeAcquireSpinLock(int *p,KIRQL *old){assert(!*p);*p=1;*old=0;}
static void KeReleaseSpinLock(int *p,KIRQL old){assert(*p&&!old);*p=0;}
static NTSTATUS AdmissionScanoutSetTimelinePaused(ADMISSION_CONTEXT *c,int paused){assert(c->PagingAdapterResetting&&paused);return 0;}
static NTSTATUS AdmissionResetEngineEntry(ADMISSION_CONTEXT *c,DXGKARG_RESETENGINE *r,unsigned entry){
 assert(c->PagingAdapterResetting&&entry==ADMISSION_GPU_RESET_ADAPTER);
 assert(r->NodeOrdinal==calls++);return fail_second&&r->NodeOrdinal==1 ? -2 : 0;}
'''
        cases=r'''
int main(void){
 for(fail_second=0;fail_second<2;++fail_second){
 ADMISSION_CONTEXT c={0};c.LiveAdditivePaging=1;calls=0;
 c.Scheduler.LastSubmittedFence=41;c.PagingScheduler.LastSubmittedFence=99;
 int s=AdmissionDdiResetFromTimeout(&c);assert(calls==2);
 if(fail_second){assert(s<0&&c.PagingAdapterResetting&&c.PagingScheduler.CompletedFence==0);}
 else {assert(s==0&&!c.PagingAdapterResetting&&c.Scheduler.CompletedFence==41&&c.PagingScheduler.CompletedFence==99);}
 }
 return 0;
}
'''
        self.compile_run(shim+body(source,'AdmissionDdiResetFromTimeout')+cases,[],[harness.SHARED/'include'])
