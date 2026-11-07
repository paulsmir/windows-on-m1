"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
An independent paging fence must not retire the active render's same-valued
fence. Exercise real KMD queues and scheduler with independent namespaces,
while the legacy gate remains serialized and CPU present shares one worker.
"""
import re
import unittest
from g3_vidmm_replay import body as kmd_body
import test_g4_paging_serialization as serialization
ROOT, RENDER, SHARED = serialization.ROOT, serialization.RENDER, serialization.SHARED
from test_g4_paging_submit_containment import SHIM, function

LIVE_CASE = r'''
int main(void) {
 ADMISSION_CONTEXT *c=calloc(1,sizeof(*c)); assert(c);
 DXGKARG_SUBMITCOMMAND a={0}; ADMISSION_PAGING_RECORD p={0};
 c->Started=c->SchedulerInitialized=1;c->PagingWorkItem=c;
 c->LiveAdditivePaging=1;
 AppleAgxSchedulerInitialize(&c->Scheduler);
 AppleAgxSchedulerInitialize(&c->PagingScheduler);
 AdmissionRenderPacketInitialize(&c->RenderPacket);
 assert(AppleAgxSchedulerQueueFence(&c->Scheduler,0,0,41));
 assert(AppleAgxSchedulerActivateFence(&c->Scheduler,0,0,41));
 c->DispatchedFence=41;
 p.Kind=AdmissionPagingMonitoredFence;p.Bytes=8;p.FenceValue=99;
 a.NodeOrdinal=1;a.SubmissionFenceId=41;
 assert(AdmissionCpuQueueSubmit(c,&a,ADMISSION_CPU_PACKET_PAGING,&p,sizeof(p))==0);
 assert(cpu_launches==1 && c->PagingPending && c->PagingActiveNode==1);
 assert(c->Scheduler.ActiveFence==41 && c->Scheduler.CompletedFence==0);
 assert(c->PagingScheduler.ActiveFence==41 && c->DispatchedFence==41);
 assert(!AdmissionPagingQuiescent(c));
 assert(AppleAgxSchedulerCompleteActiveFence(&c->PagingScheduler,0,0,41));
 c->PagingPending=0;c->PagingNodeDispatchedFence=0;
 assert(c->Scheduler.ActiveFence==41 && c->Scheduler.CompletedFence==0);
 a.SubmissionFenceId=42;
 assert(AdmissionCpuQueueSubmit(c,&a,ADMISSION_CPU_PACKET_PAGING,&p,sizeof(p))==0);
 assert(cpu_launches==2 && c->PagingFence==42);
 assert(AppleAgxSchedulerCompleteActiveFence(&c->PagingScheduler,0,0,42));
 c->PagingPending=0;c->PagingNodeDispatchedFence=0;
 assert(AdmissionPagingQuiescent(c));
 assert(c->Scheduler.CompletedFence==0 && c->DispatchedFence==41);
 free(c);return 0;
}
'''

class LivePaging(unittest.TestCase):
    compile_run = serialization.PagingSerialization.compile_run

    def test_actual_queue_completes_paging_while_render_active(self):
        header=(RENDER/'include/render_admission.h').read_text()
        packet=re.search(r'typedef struct _ADMISSION_CPU_PACKET\s*\{.*?'
                         r'\}\s*ADMISSION_CPU_PACKET;',header,re.S).group(0)
        shim=SHIM.replace('/*PACKET*/',packet)
        # Explicit modeled OS context; production queue and scheduler are real.
        if 'ULONG LiveAdditivePaging' not in shim:
            shim=shim.replace('APPLE_AGX_SCHEDULER Scheduler;',
                'ULONG LiveAdditivePaging, PagingActiveNode, PagingNodeHead, PagingNodeCount, PagingNodeDispatchedFence;\n'
                'APPLE_AGX_SCHEDULER PagingScheduler;\n'
                'ADMISSION_CPU_PACKET PagingNodeQueue[APPLE_AGX_SCHEDULER_QUEUE_CAPACITY];\n'
                'APPLE_AGX_SCHEDULER Scheduler;')
            shim=shim.replace('void *hContext; unsigned SubmissionFenceId;',
                              'void *hContext; unsigned SubmissionFenceId; unsigned NodeOrdinal;')
        queue=(RENDER/'src/work_queue_windows.c').read_text()
        source=shim
        for name in ('AdmissionCpuQueueSubmit','AdmissionDispatchQueuedWork','AdmissionPagingQuiescent'):
            source+='\n'+function(queue,name)
        self.compile_run(source+LIVE_CASE,[SHARED/'src/apple_agx_scheduler.c',
            RENDER/'src/render_submission.c',RENDER/'src/render_paging.c'],
            [RENDER/'include',SHARED/'include'])

WORKER_CASE=r'''
static ADMISSION_CONTEXT *current;
static unsigned notices, noticed_node, noticed_fence, execute_count, early_dpc;
static void notify(void *handle,DXGKARGCB_NOTIFY_INTERRUPT_DATA *n) {
 (void)handle;assert(n->InterruptType==DXGK_INTERRUPT_DMA_COMPLETED);
 ++notices;noticed_node=n->DmaCompleted.NodeOrdinal;noticed_fence=n->DmaCompleted.SubmissionFenceId;
}
static BOOLEAN queue_dpc(void *handle){(void)handle;return TRUE;}
static NTSTATUS synchronize(void *h,BOOLEAN (*fn)(void*),void *p,unsigned m,BOOLEAN *done) {
 (void)h;(void)m;*done=fn(p);
 if(early_dpc){AdmissionPagingDpc(current);AdmissionDispatchQueuedWork(current);
   assert(current->PagingWorkersActive==1 && current->PagingCallbacksActive==1);
   assert(!current->PagingIdle);assert(cpu_launches==1);}
 return 0;
}
int main(void) {
 for(early_dpc=0;early_dpc<2;++early_dpc){
 ADMISSION_CONTEXT *c=calloc(1,sizeof(*c));current=c;cpu_launches=notices=execute_count=0;
 DXGKARG_SUBMITCOMMAND a={0};ADMISSION_PAGING_RECORD r={0};
 c->Started=c->SchedulerInitialized=c->InterfaceValid=1;c->PagingWorkItem=c;c->LiveAdditivePaging=1;
 c->Interface=(WORKER_INTERFACE){c,synchronize,notify,queue_dpc};
 AppleAgxSchedulerInitialize(&c->Scheduler);AppleAgxSchedulerInitialize(&c->PagingScheduler);
 AdmissionRenderPacketInitialize(&c->RenderPacket);
 assert(AppleAgxSchedulerQueueFence(&c->Scheduler,0,0,41));
 assert(AppleAgxSchedulerActivateFence(&c->Scheduler,0,0,41));c->DispatchedFence=41;
 r.Kind=AdmissionPagingMonitoredFence;r.Bytes=8;r.FenceValue=99;a.NodeOrdinal=1;a.SubmissionFenceId=41;
 assert(AdmissionCpuQueueSubmit(c,&a,ADMISSION_CPU_PACKET_PAGING,&r,sizeof(r))==0);
 a.SubmissionFenceId=42;r.FenceValue=100;
 assert(AdmissionCpuQueueSubmit(c,&a,ADMISSION_CPU_PACKET_PAGING,&r,sizeof(r))==0);
 assert(cpu_launches==1);AdmissionPagingWorker(NULL,c);
 assert(notices==1&&noticed_node==1&&noticed_fence==41);
 assert(c->Scheduler.ActiveFence==41&&c->Scheduler.CompletedFence==0&&c->DispatchedFence==41);
 if(!early_dpc){assert(cpu_launches==1);AdmissionPagingDpc(c);AdmissionDispatchQueuedWork(c);}
 assert(cpu_launches==2 && c->PagingFence==42 && c->PagingRecords[0].FenceValue==100);
 /* Complete second packet with late DPC to keep the callback assertion simple. */
 unsigned saved=early_dpc;early_dpc=0;AdmissionPagingWorker(NULL,c);AdmissionPagingDpc(c);early_dpc=saved;
 assert(notices==2&&noticed_node==1&&noticed_fence==42&&c->PagingScheduler.CompletedFence==42);
 assert(c->Scheduler.CompletedFence==0&&AdmissionPagingQuiescent(c));
 /* Stop must refuse the callback tail even after active records retired. */
 c->PagingCallbacksActive=1;AdmissionPagingUpdateIdleLocked(c);
 assert(AdmissionPagingStop(c)==STATUS_DEVICE_BUSY&&c->PagingWorkItem==c);
 c->PagingCallbacksActive=0;AdmissionPagingUpdateIdleLocked(c);
 assert(AdmissionPagingStop(c)==0&&!c->PagingWorkItem);free(c);
 }
 return 0;
}
'''

def worker_source():
    header=(RENDER/'include/render_admission.h').read_text()
    packet=re.search(r'typedef struct _ADMISSION_CPU_PACKET\s*\{.*?'
                     r'\}\s*ADMISSION_CPU_PACKET;',header,re.S).group(0)
    shim=SHIM.replace('/*PACKET*/',packet)
    shim=shim.replace('typedef struct _ADMISSION_CONTEXT {',
        (ROOT/'tests/g4_live_worker_shim.h').read_text()+'\ntypedef struct _ADMISSION_CONTEXT {')
    shim=shim.replace(' int Started, PagingLock, SchedulerLock, PagingIdle;',
        ' int Started, PagingLock, SchedulerLock, PagingIdle, InterfaceValid; WORKER_INTERFACE Interface;\n'
        ' LONG PagingDpcPending, PresentTransferState, FeatureReadyMask; ULONG PagingLastCompletedFence;\n'
        ' ULONGLONG PresentCopyFaultVa; ULONG PresentCopyFaultWrite; LONGLONG PagingNotifyQpc;\n'
        ' struct {ULONG Fence,NotifyInterrupt,NotifyDpc;} PresentTransferReceipt;')
    shim=shim.replace('QueueToDispatch[3];','QueueToDispatch[3],DispatchToWorker[3],Worker[3],NotifyToDpc[3];')
    shim+='''
typedef void *PIO_WORKITEM;
#define Executive 0
#define KernelMode 0
#define APPLE_AGX_WDDM_READY_MEMORY_PAGING 1
static NTSTATUS KeWaitForSingleObject(int *p,int a,int b,int c,LARGE_INTEGER *t){(void)a;(void)b;(void)c;(void)t;return *p ? STATUS_SUCCESS : STATUS_DEVICE_BUSY;}
static void IoFreeWorkItem(void *p){(void)p;}
static LONG InterlockedAnd(volatile LONG *p,LONG v){LONG old=*p;*p &= v;return old;}
static LONG InterlockedIncrement(volatile LONG *p){return ++*p;}
static LONG InterlockedDecrement(volatile LONG *p){return --*p;}
static void KeSetEvent(int *p,int n,BOOLEAN b){(void)n;(void)b;*p=1;}
static void AdmissionContextRetireFence(ADMISSION_RENDER_CONTEXT *c,ULONG f){assert(c->Object.FenceOutstanding==f);c->Object.FenceOutstanding=0;}
static NTSTATUS AdmissionMemoryRuntimeExecutePresent(ADMISSION_CONTEXT *c,void *d,UINT b,ULONGLONG *out){(void)c;(void)d;*out=b;return 0;}
static NTSTATUS AdmissionMemoryRuntimeExecutePaging(ADMISSION_CONTEXT *c,const ADMISSION_PAGING_RECORD *r){(void)c;assert(r->FenceValue==99||r->FenceValue==100);return 0;}
static void AdmissionRecordPresentTransfer(ADMISSION_CONTEXT *c,UINT f,void *d,UINT b,ULONGLONG copied,NTSTATUS s){(void)c;(void)f;(void)d;(void)b;(void)copied;(void)s;}
static void AdmissionSchedulerWorkerFinished(ADMISSION_CONTEXT *c){assert(c->PagingCallbacksActive);assert(!c->PagingIdle);}
void AdmissionPagingUpdateIdleLocked(ADMISSION_CONTEXT *);
'''
    paging=(RENDER/'src/paging_windows.c').read_text()
    shim+=re.search(r'typedef struct _ADMISSION_PAGING_NOTIFICATION\s*\{.*?\} ADMISSION_PAGING_NOTIFICATION;',paging,re.S).group(0)
    scheduler=(RENDER/'src/scheduler_windows.c').read_text()
    queue=(RENDER/'src/work_queue_windows.c').read_text()
    for name in ('AdmissionCpuQueueSubmit','AdmissionDispatchQueuedWork','AdmissionPagingQuiescent'):
        shim+='\n'+function(queue,name)
    shim+='\n'+function(scheduler,'AdmissionSchedulerRecordCompletion')
    for name in ('AdmissionPagingNotifyAtInterrupt','AdmissionPagingRecordTerminal','AdmissionPagingWorker',
                 'AdmissionPagingUpdateIdleLocked','AdmissionPagingDpc','AdmissionPagingStop'):
        shim+='\n'+kmd_body(paging,name)
    return shim

def test_worker(self):
    self.compile_run(worker_source()+WORKER_CASE,[SHARED/'src/apple_agx_scheduler.c',
        RENDER/'src/render_submission.c',RENDER/'src/render_paging.c'],[RENDER/'include',SHARED/'include'])
LivePaging.test_actual_worker_notify_and_both_dpc_orders=test_worker
