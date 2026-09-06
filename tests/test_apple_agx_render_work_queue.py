"""Exercise real Windows queue/retirement functions with a reentrant OS callback."""
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT/'drivers/apple-agx/render-admission'
SHARED = ROOT/'drivers/apple-agx/shared'


class WorkQueueTests(unittest.TestCase):
    def test_active_render_consecutive_copies_and_reentrant_completion(self):
        paging=(RENDER/'src/paging_windows.c').read_text()
        scheduler=(RENDER/'src/scheduler_windows.c').read_text()
        header=(RENDER/'include/render_admission.h').read_text()
        def function(source,name):
            return re.search(r'_Use_decl_annotations_ (?:NTSTATUS|BOOLEAN|VOID|void) '+name+r'\(.*?^}',source,re.S|re.M).group(0)
        packet=re.search(r'typedef struct _ADMISSION_CPU_PACKET.*?} ADMISSION_CPU_PACKET;',header,re.S).group(0)
        shim=r'''
#include <stdint.h>
#include <string.h>
#include <stdlib.h>
#include <assert.h>
#include "apple_agx_scheduler.h"
#include "render_submission.h"
#include "render_paging.h"
#define _Use_decl_annotations_
#define __declspec(x)
#define ADMISSION_MAX_PAGING_RECORDS 64u
#define ADMISSION_PRESENT_BLT_DMA_MAX 4096u
#define ADMISSION_CPU_PACKET_PAGING 1u
#define ADMISSION_CPU_PACKET_PRESENT 2u
#define ADMISSION_SCHEDULER_NODE 0u
#define ADMISSION_SCHEDULER_ENGINE 0u
#define TRUE 1
#define FALSE 0
#define STATUS_SUCCESS 0
#define STATUS_PENDING 1
#define STATUS_INVALID_PARAMETER (-1)
#define STATUS_DEVICE_BUSY (-2)
#define STATUS_INVALID_DEVICE_STATE (-3)
#define STATUS_DEVICE_HARDWARE_ERROR (-4)
#define PAGED_CODE() ((void)0)
#define NT_SUCCESS(x) ((x)>=0)
#define IO_NO_INCREMENT 0
#define DelayedWorkQueue 0
#define RtlCopyMemory memcpy
typedef void VOID;
typedef void *PVOID;
typedef uint32_t UINT,ULONG;
typedef uintptr_t ULONG_PTR;
typedef int32_t LONG,NTSTATUS;
typedef int BOOLEAN,KIRQL;
typedef unsigned char UCHAR;
/*PACKET*/
typedef struct _ADMISSION_CONTEXT ADMISSION_CONTEXT;
struct _ADMISSION_CONTEXT {
 int Started,InterfaceValid,PagingLock,SchedulerLock,PagingIdle;
 volatile LONG SchedulerInitialized,SchedulerFaulted,SchedulerDpcPending,RenderDpcFence;
 volatile LONG PagingStopping,PagingPending,PagingDpcPending,PresentTransferState;
 volatile LONG PagingWorkersActive,PagingDpcsActive;
 void *PagingWorkItem;
 ULONG CpuQueueHead,CpuQueueCount,DispatchedFence,PresentCopyBytes,PagingRecordCount;
 ULONG PagingFence,PagingLastSubmittedFence,PagingLastCompletedFence;
 NTSTATUS PagingCompletionStatus;
 struct {ULONG Fence,NotifyDpc;} PresentTransferReceipt;
 APPLE_AGX_SCHEDULER Scheduler;
 ADMISSION_RENDER_PACKET RenderPacket;
 int BackendImage;
 ADMISSION_CPU_PACKET CpuQueue[64];
 UCHAR PresentCopyCommand[4096];ADMISSION_PAGING_RECORD PagingRecords[64];
 struct {void *DeviceHandle;void (*DxgkCbNotifyDpc)(void *);} Interface;
};
typedef struct {unsigned SubmissionFenceId;} DXGKARG_SUBMITCOMMAND;
typedef struct {unsigned PreemptionFenceId,NodeOrdinal,EngineOrdinal;union {unsigned Value;} Flags;} DXGKARG_PREEMPTCOMMAND;
typedef struct {unsigned NodeOrdinal,EngineOrdinal,LastAbortedFenceId;} DXGKARG_RESETENGINE;
typedef void *HANDLE;
typedef struct {struct {unsigned FenceOutstanding;} Object;} ADMISSION_RENDER_CONTEXT;
static int lock_depth,cpu_launches,render_launches,platform_busy,inject_submission,finish_during_attempt;
static int InterlockedCompareExchange(volatile LONG *p,LONG value,LONG expected){LONG old=*p;if(old==expected)*p=value;return old;}
static int InterlockedExchange(volatile LONG *p,LONG value){LONG old=*p;*p=value;return old;}
static int InterlockedIncrement(volatile LONG *p){return ++*p;}
static int InterlockedDecrement(volatile LONG *p){return --*p;}
static void KeAcquireSpinLock(int *p,KIRQL *irql){(void)p;*irql=2;++lock_depth;}
static void KeReleaseSpinLock(int *p,KIRQL irql){(void)p;(void)irql;--lock_depth;}
static void KeAcquireSpinLockAtDpcLevel(int *p){(void)p;++lock_depth;}
static void KeReleaseSpinLockFromDpcLevel(int *p){(void)p;--lock_depth;}
static void KeClearEvent(int *p){*p=0;}
static void KeSetEvent(int *p,int n,int w){(void)n;(void)w;*p=1;}
static void AdmissionPagingQueueActive(ADMISSION_CONTEXT *c){assert(!lock_depth);assert(c->PagingPending);++cpu_launches;}
static int AdmissionPlatformRuntimeSubmit(ADMISSION_CONTEXT *c){(void)c;assert(!lock_depth);
 if(platform_busy){if(finish_during_attempt){platform_busy=0;finish_during_attempt=0;}return 0;}++render_launches;return 1;}
static int AdmissionPlatformRuntimeReady(ADMISSION_CONTEXT *c){(void)c;return !platform_busy;}
static unsigned preemption_notified,preemption_completed,released_backend;
static int AdmissionSchedulerTryNotifyPreemption(ADMISSION_CONTEXT *c){
 APPLE_AGX_PREEMPTION p;assert(!lock_depth);
 if(!AppleAgxSchedulerClaimBoundaryPreemption(&c->Scheduler,&p))return 0;
 preemption_notified=p.PreemptionFence;preemption_completed=p.LastCompletedFence;
 return AppleAgxSchedulerCommitBoundaryPreemption(&c->Scheduler,p.PreemptionFence);
}
static int AdmissionBackendImageReleaseSubmission(int *image,unsigned fence){(void)image;released_backend=fence;return 1;}
static NTSTATUS AdmissionPlatformRuntimeReset(ADMISSION_CONTEXT *c,unsigned *fence){(void)c;(void)fence;assert(0);return -4;}
void AdmissionPagingWorker(void){}
#define IoQueueWorkItem(a,b,c,d) ((void)(a),(void)(b),(void)(c),AdmissionPagingQueueActive(d))
void AdmissionDispatchQueuedWork(ADMISSION_CONTEXT *Context);
'''.replace('/*PACKET*/',packet)
        cases=r'''
static void notify_dpc(void *opaque){
 ADMISSION_CONTEXT *c=opaque;DXGKARG_SUBMITCOMMAND a={13};unsigned char data[4]={0xd1};
 assert(lock_depth==0);
 assert(c->PagingPending==0 && c->PresentCopyBytes==0 && c->PagingRecordCount==0);
 if(inject_submission){inject_submission=0;assert(AdmissionPagingSubmitPresent(c,&a,data,4)==0);}
}
int main(void){
 ADMISSION_CONTEXT *c=calloc(1,sizeof(*c));assert(c);
 DXGKARG_SUBMITCOMMAND a={11};unsigned char b[4]={0xb1},d[4]={0xc1};
 c->Started=c->InterfaceValid=c->SchedulerInitialized=1;c->PagingWorkItem=c;
 c->Interface.DeviceHandle=c;c->Interface.DxgkCbNotifyDpc=notify_dpc;
 AppleAgxSchedulerInitialize(&c->Scheduler);AdmissionRenderPacketInitialize(&c->RenderPacket);
 assert(AppleAgxSchedulerQueueFence(&c->Scheduler,0,0,10));
 assert(AppleAgxSchedulerActivateFence(&c->Scheduler,0,0,10));c->DispatchedFence=10;
 assert(AdmissionPagingSubmitPresent(c,&a,b,4)==0); /* old code failed BUSY after queue mutation */
 a.SubmissionFenceId=12;assert(AdmissionPagingSubmitPresent(c,&a,d,4)==0);
 b[0]=0;d[0]=0;
 assert(cpu_launches==0 && c->CpuQueueCount==2 && c->Scheduler.ActiveFence==10);
 assert(c->Scheduler.QueuedFence==11 && c->Scheduler.LastSubmittedFence==12);
 assert(AdmissionSchedulerRecordCompletion(c,10));AdmissionDispatchQueuedWork(c);
 assert(cpu_launches==1 && c->Scheduler.ActiveFence==11 && c->PresentCopyCommand[0]==0xb1);
 assert(c->CpuQueueCount==1 && c->PagingPending==1);
 assert(AdmissionSchedulerRecordCompletion(c,11));
 c->PagingDpcPending=1;c->PagingCompletionStatus=0;
 c->PresentTransferState=3;c->PresentTransferReceipt.Fence=11;inject_submission=1;
 AdmissionPagingDpc(c); /* notify callback immediately submits another packet */
 assert(c->PresentTransferReceipt.NotifyDpc==1 && c->PresentTransferState==4);
 assert(cpu_launches==2 && c->Scheduler.ActiveFence==12 && c->PresentCopyCommand[0]==0xc1);
 assert(c->PagingPending==1 && c->CpuQueueCount==1); /* old cleanup must not clear new packet */
 assert(AdmissionSchedulerRecordCompletion(c,12));c->PagingDpcPending=1;c->PagingCompletionStatus=0;
 AdmissionPagingDpc(c);AdmissionDispatchQueuedWork(c);
 assert(cpu_launches==3 && c->Scheduler.ActiveFence==13 && c->PresentCopyCommand[0]==0xd1);
 assert(AppleAgxSchedulerQueueFence(&c->Scheduler,0,0,14));
 c->RenderPacket.State=AdmissionRenderPacketQueued;c->RenderPacket.Description.Fence=14;
 AdmissionDispatchQueuedWork(c);assert(render_launches==0);
 assert(AdmissionSchedulerRecordCompletion(c,13));c->PagingDpcPending=1;c->PagingCompletionStatus=0;
 platform_busy=1;AdmissionPagingDpc(c);AdmissionDispatchQueuedWork(c);
 assert(render_launches==0 && c->DispatchedFence==0);
 platform_busy=0;AdmissionDispatchQueuedWork(c);AdmissionDispatchQueuedWork(c);
 assert(render_launches==1 && c->DispatchedFence==14 && c->CpuQueueCount==0);
 assert(AppleAgxSchedulerActivateFence(&c->Scheduler,0,0,14));c->RenderPacket.State=AdmissionRenderPacketActive;
 a.SubmissionFenceId=15;assert(AdmissionPagingSubmitPresent(c,&a,b,4)==0);
 DXGKARG_PREEMPTCOMMAND preempt={100,0,0,{0}};
 assert(AdmissionDdiPreemptCommand(c,&preempt)==0);
 assert(c->CpuQueueCount==0 && c->Scheduler.QueuedFence==0 && c->Scheduler.ActiveFence==14);
 assert(!c->SchedulerFaulted && !preemption_notified);
 assert(AppleAgxSchedulerCompleteActiveFence(&c->Scheduler,0,0,14));
 assert(AppleAgxSchedulerObserveBoundaryCompletion(&c->Scheduler,0,0,14));
 c->RenderPacket.State=AdmissionRenderPacketEmpty;
 c->RenderDpcFence=14;c->SchedulerDpcPending=1;AdmissionSchedulerDpc(c);
 assert(preemption_notified==100 && preemption_completed==14 && c->DispatchedFence==0);
 a.SubmissionFenceId=16;assert(AdmissionPagingSubmitPresent(c,&a,b,4)==0);
 assert(AdmissionSchedulerRecordCompletion(c,16));c->PagingDpcPending=1;c->PagingCompletionStatus=0;
 AdmissionPagingDpc(c);
 ADMISSION_RENDER_CONTEXT renderContext={{17}};
 assert(AppleAgxSchedulerQueueFence(&c->Scheduler,0,0,17));c->RenderPacket.State=AdmissionRenderPacketQueued;
 c->RenderPacket.Description.Fence=17;c->RenderPacket.Description.ContextToken=(unsigned long long)(uintptr_t)&renderContext;
 platform_busy=1;a.SubmissionFenceId=18;assert(AdmissionPagingSubmitPresent(c,&a,b,4)==0);
 DXGKARG_RESETENGINE reset={0};assert(AdmissionDdiResetEngine(c,&reset)==0);
 assert(c->CpuQueueCount==0 && c->Scheduler.QueuedFence==0 && c->DispatchedFence==0);
 assert(reset.LastAbortedFenceId==16 && released_backend==17 && renderContext.Object.FenceOutstanding==0);
 assert(AppleAgxSchedulerQueueFence(&c->Scheduler,0,0,19));c->RenderPacket.State=AdmissionRenderPacketQueued;
 c->RenderPacket.Description.Fence=19;platform_busy=1;finish_during_attempt=1;
 AdmissionDispatchQueuedWork(c);assert(render_launches==2 && c->DispatchedFence==19);
 c->PagingWorkersActive=1;c->PagingIdle=0;
 KeAcquireSpinLockAtDpcLevel(&c->PagingLock);AdmissionPagingUpdateIdleLocked(c);assert(!c->PagingIdle);
 c->PagingWorkersActive=0;AdmissionPagingUpdateIdleLocked(c);assert(c->PagingIdle);
 KeReleaseSpinLockFromDpcLevel(&c->PagingLock);
 assert(lock_depth==0);free(c);
 /* Physical retirement before the render notification must stay reserved,
  * even if preemption and an unrelated VSync DPC intervene. */
 c=calloc(1,sizeof(*c));assert(c);c->Started=c->InterfaceValid=c->SchedulerInitialized=1;c->PagingWorkItem=c;
 c->Interface.DeviceHandle=c;c->Interface.DxgkCbNotifyDpc=notify_dpc;
 AppleAgxSchedulerInitialize(&c->Scheduler);AdmissionRenderPacketInitialize(&c->RenderPacket);
 assert(AppleAgxSchedulerQueueFence(&c->Scheduler,0,0,1));assert(AppleAgxSchedulerActivateFence(&c->Scheduler,0,0,1));
 c->DispatchedFence=1;assert(AppleAgxSchedulerCompleteActiveFence(&c->Scheduler,0,0,1));
 preemption_notified=0;preempt.PreemptionFenceId=200;
 assert(AdmissionDdiPreemptCommand(c,&preempt)==0);assert(c->DispatchedFence==1 && !preemption_notified);
 c->SchedulerDpcPending=1;AdmissionSchedulerDpc(c);assert(c->DispatchedFence==1 && !preemption_notified);
 c->RenderDpcFence=1;c->SchedulerDpcPending=1;AdmissionSchedulerDpc(c);
 assert(preemption_notified==200 && preemption_completed==1 && c->DispatchedFence==0);
 a.SubmissionFenceId=2;assert(AdmissionPagingSubmitPresent(c,&a,b,4)==0);assert(c->Scheduler.ActiveFence==2);
 free(c);return 0;
}
'''
        queue=(RENDER/'src/work_queue_windows.c').read_text().replace('#include "render_admission.h"','')
        reset_helper=re.search(r'static __declspec\(noinline\) NTSTATUS AdmissionResetEngineInternal\(.*?^}',scheduler,re.S|re.M).group(0)
        body=(shim+function(scheduler,'AdmissionSchedulerRecordCompletion')+
              function(scheduler,'AdmissionSchedulerSubmitFence')+queue+
              function(paging,'AdmissionPagingSubmitPresent')+
              function(paging,'AdmissionPagingUpdateIdleLocked')+
              function(paging,'AdmissionPagingDpc')+
              function(scheduler,'AdmissionDdiPreemptCommand')+
              function(scheduler,'AdmissionSchedulerDpc')+
              reset_helper+function(scheduler,'AdmissionDdiResetEngine')+cases)
        with tempfile.TemporaryDirectory() as tmp:
            program=Path(tmp)/'queue.c';program.write_text(body);binary=Path(tmp)/'queue'
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                            '-fsanitize=address,undefined','-I',str(RENDER/'include'),'-I',str(SHARED/'include'),
                            str(program),str(SHARED/'src/apple_agx_scheduler.c'),
                            str(RENDER/'src/render_submission.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
