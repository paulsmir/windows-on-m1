"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP1969's 0x119/2 reported DEVICE_BUSY from submit. Exercise the production
virtual-submit boundary with every class of downstream status: only success
and INVALID_PARAMETER may escape, and refusal must not retire prior GPU work.
The real scheduler carries rejected boundaries through completion/preemption.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_g4_submit_virtual_replay import ROOT, SOURCE, function_body

SHARED = ROOT / 'drivers/apple-agx/shared'
SHIM = r'''
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "apple_agx_scheduler.h"
typedef int32_t NTSTATUS,LONG;typedef unsigned ULONG;typedef int KIRQL;
typedef void *HANDLE;
#define _Use_decl_annotations_
#define NULL ((void *)0)
#define STATUS_SUCCESS ((NTSTATUS)0)
#define STATUS_INVALID_PARAMETER ((NTSTATUS)0xc000000d)
#define FALSE 0
#define AdmissionG4RejectStatus 16
struct args {void *hContext;unsigned SubmissionFenceId,NodeOrdinal,EngineOrdinal;struct {unsigned Value,Present,Resubmission;} Flags;};
typedef struct args DXGKARG_SUBMITCOMMANDVIRTUAL;
typedef struct {LONG G4SubmitVirtualInFlight,G4SubmitVirtualFence,G4SubmitVirtualFlags,SchedulerInitialized;struct {LONG Resubmissions,Submissions;} G4PreemptOverlap;int SchedulerLock;APPLE_AGX_SCHEDULER Scheduler, PagingScheduler;ULONG LiveAdditivePaging;} ADMISSION_CONTEXT;
static NTSTATUS inner_status;static unsigned diagnostics,downstream;
static LONG InterlockedIncrement(LONG *v){return ++*v;}static LONG InterlockedDecrement(LONG *v){return --*v;}
static LONG InterlockedExchange(LONG *v,LONG n){LONG old=*v;*v=n;return old;}
static LONG InterlockedCompareExchange(LONG *v,LONG n,LONG cmp){LONG old=*v;if(old==cmp)*v=n;return old;}
static void KeAcquireSpinLock(int *l,KIRQL *i){assert(!*l);*l=1;*i=0;}static void KeReleaseSpinLock(int *l,KIRQL i){assert(*l&&i==0);*l=0;}
static NTSTATUS AdmissionDdiSubmitCommandVirtualInner(HANDLE a,const DXGKARG_SUBMITCOMMANDVIRTUAL *v){(void)a;(void)v;return inner_status;}
static NTSTATUS AdmissionG4SubmitReject(ADMISSION_CONTEXT *a,const void *c,const DXGKARG_SUBMITCOMMANDVIRTUAL *v,ULONG b,NTSTATUS s,ULONG d,int valid){(void)a;(void)c;(void)v;assert(b==16&&!valid);++diagnostics;downstream=d;return s;}
'''
CASES = r'''
static unsigned add(unsigned f,unsigned n){unsigned out=f+n;return out?out:1;}
static void preempt(APPLE_AGX_SCHEDULER *s,unsigned id,unsigned want){
 APPLE_AGX_PREEMPTION p;
 assert(AppleAgxSchedulerBeginBoundaryPreemption(s,0,0,id,s->LastSubmittedFence,s->ActiveFence));
 assert(AppleAgxSchedulerClaimBoundaryPreemption(s,&p));assert(p.LastCompletedFence==want);
 assert(AppleAgxSchedulerCommitBoundaryPreemption(s,id));
}
static void ordered_cases(unsigned seed){
 APPLE_AGX_SCHEDULER s;APPLE_AGX_PREEMPTION p;unsigned aborted;
 unsigned a=add(seed,1),b=add(a,1),r=add(b,1),r2=add(r,1),next=add(r2,1);
 AppleAgxSchedulerInitialize(&s);s.CompletedFence=s.LastSubmittedFence=seed;
 assert(AppleAgxSchedulerQueueFence(&s,0,0,a));assert(AppleAgxSchedulerActivateFence(&s,0,0,a));
 assert(AppleAgxSchedulerQueueFence(&s,0,0,b));
 assert(AppleAgxSchedulerRejectFence(&s,0,0,r));assert(AppleAgxSchedulerRejectFence(&s,0,0,r2));
 assert(!AppleAgxSchedulerRejectFence(&s,1,0,next));assert(!AppleAgxSchedulerRejectFence(&s,0,0,0));
 assert(!AppleAgxSchedulerRejectFence(&s,0,0,a));assert(!AppleAgxSchedulerRejectFence(&s,0,0,b));
 assert(s.ActiveFence==a&&s.QueueCount==1&&s.CompletedFence==seed);
 assert(AppleAgxSchedulerBeginBoundaryPreemption(&s,0,0,71,r2,a));
 assert(AppleAgxSchedulerFaultActiveFence(&s,0,0,a));
 assert(AppleAgxSchedulerObserveBoundaryCompletion(&s,0,0,a));
 assert(AppleAgxSchedulerClaimBoundaryPreemption(&s,&p));assert(p.LastCompletedFence==a);
 assert(AppleAgxSchedulerCommitBoundaryPreemption(&s,71));
 assert(s.PreemptedFenceCount==1&&s.PreemptedFenceQueue[0]==b);
 preempt(&s,72,a); /* no new packets: do not forget b and its rejected tail */
 assert(AppleAgxSchedulerResetEngine(&s,0,0,&aborted));assert(aborted==a&&s.CompletedFence==a);
 assert(!AppleAgxSchedulerQueueResubmittedPagingFence(&s,0,0,r));
 assert(AppleAgxSchedulerQueueResubmittedPagingFence(&s,0,0,b));
 assert(AppleAgxSchedulerActivateFence(&s,0,0,b));
 assert(AppleAgxSchedulerFaultActiveFence(&s,0,0,b));
 assert(s.CompletedFence==r2&&s.LastAbortedFence==b);
 assert(AppleAgxSchedulerRejectFence(&s,0,0,r));assert(s.CompletedFence==r2);
 preempt(&s,73,r2); /* retired boundary cannot prefer the older abort */
 assert(AppleAgxSchedulerQueueFence(&s,0,0,next));assert(AppleAgxSchedulerActivateFence(&s,0,0,next));
 assert(AppleAgxSchedulerCompleteActiveFence(&s,0,0,next));assert(s.CompletedFence==next);

 /* Rejected tail on the active packet: real completion -> preemption agrees. */
 AppleAgxSchedulerInitialize(&s);s.CompletedFence=s.LastSubmittedFence=seed;
 assert(AppleAgxSchedulerQueueFence(&s,0,0,a));assert(AppleAgxSchedulerActivateFence(&s,0,0,a));
 assert(AppleAgxSchedulerRejectFence(&s,0,0,b));
 assert(AppleAgxSchedulerBeginBoundaryPreemption(&s,0,0,81,b,a));
 assert(s.CompletedFence==seed&&s.ActiveFence==a);
 assert(AppleAgxSchedulerCompleteActiveFence(&s,0,0,a));
 assert(AppleAgxSchedulerObserveBoundaryCompletion(&s,0,0,a));
 assert(AppleAgxSchedulerClaimBoundaryPreemption(&s,&p));assert(p.LastCompletedFence==b);
 assert(AppleAgxSchedulerCommitBoundaryPreemption(&s,81));

 /* A reset is called only after physical quiescence. A refused successor
  * becomes completed, but the reset's aborted ID is the actual GPU packet. */
 AppleAgxSchedulerInitialize(&s);s.CompletedFence=s.LastSubmittedFence=seed;
 assert(AppleAgxSchedulerQueueFence(&s,0,0,a));assert(AppleAgxSchedulerActivateFence(&s,0,0,a));
 assert(AppleAgxSchedulerRejectFence(&s,0,0,b));assert(AppleAgxSchedulerQueueFence(&s,0,0,r));
 assert(s.ActiveFence==a&&s.CompletedFence==seed); /* no reset, still hung */
 assert(AppleAgxSchedulerResetEngine(&s,0,0,&aborted));assert(aborted==a&&s.LastAbortedFence==a&&s.CompletedFence==b);
 assert(AppleAgxSchedulerQueueResubmittedPagingFence(&s,0,0,r));assert(AppleAgxSchedulerActivateFence(&s,0,0,r));
 assert(AppleAgxSchedulerCompleteActiveFence(&s,0,0,r));assert(s.CompletedFence==r);

 /* Reject an old paging resubmission; its never-run tail stays ordered. */
 AppleAgxSchedulerInitialize(&s);s.CompletedFence=s.LastSubmittedFence=seed;
 assert(AppleAgxSchedulerQueueFence(&s,0,0,a));assert(AppleAgxSchedulerQueueFence(&s,0,0,b));
 assert(AppleAgxSchedulerRejectFence(&s,0,0,r));preempt(&s,91,seed);
 assert(AppleAgxSchedulerRejectFence(&s,0,0,b));assert(s.PreemptedFenceCount==1&&s.CompletedFence==seed);
 assert(AppleAgxSchedulerQueueResubmittedPagingFence(&s,0,0,a));assert(AppleAgxSchedulerActivateFence(&s,0,0,a));
 assert(AppleAgxSchedulerCompleteActiveFence(&s,0,0,a));assert(s.CompletedFence==r);
}
static void notification_snapshot(void){
 APPLE_AGX_SCHEDULER s;APPLE_AGX_PREEMPTION p;
 AppleAgxSchedulerInitialize(&s);
 assert(AppleAgxSchedulerRejectFence(&s,0,0,1));
 assert(AppleAgxSchedulerBeginBoundaryPreemption(&s,0,0,21,1,0));
 assert(AppleAgxSchedulerRejectFence(&s,0,0,2));
 assert(AppleAgxSchedulerClaimBoundaryPreemption(&s,&p));assert(p.LastCompletedFence==2);
 assert(AppleAgxSchedulerRejectFence(&s,0,0,3));
 assert(p.LastCompletedFence==2&&s.PreemptionLastCompletedFence==2);
 assert(AppleAgxSchedulerCommitBoundaryPreemption(&s,21));
 assert(AppleAgxSchedulerCurrentFence(&s,0,0)==3);
 preempt(&s,22,3);
}
static void full_queue(void){
 APPLE_AGX_SCHEDULER s;AppleAgxSchedulerInitialize(&s);
 for(unsigned i=1;i<=64;++i)assert(AppleAgxSchedulerQueueFence(&s,0,0,i));
 assert(!AppleAgxSchedulerQueueFence(&s,0,0,65));
 for(unsigned i=65;i<=10000;++i)assert(AppleAgxSchedulerRejectFence(&s,0,0,i));
 assert(s.QueueCount==64&&!s.CompletedFence);
 for(unsigned i=1;i<=64;++i){assert(AppleAgxSchedulerActivateFence(&s,0,0,i));
  assert(AppleAgxSchedulerCompleteActiveFence(&s,0,0,i));assert(s.CompletedFence==(i==64?10000:i));}
 assert(AppleAgxSchedulerQueueFence(&s,0,0,10001));assert(AppleAgxSchedulerActivateFence(&s,0,0,10001));
 assert(AppleAgxSchedulerCompleteActiveFence(&s,0,0,10001));assert(s.CompletedFence==10001);
}

int main(void){
 ordered_cases(0);ordered_cases(0xfffffffdu);ordered_cases(0x7ffffffdu);full_queue();notification_snapshot();
 const NTSTATUS statuses[]={0,(NTSTATUS)0x80000011,(NTSTATUS)0xc000000d,(NTSTATUS)0xc000009a,(NTSTATUS)0xc0000185,0x103};
 for(unsigned i=0;i<sizeof(statuses)/sizeof(statuses[0]);++i){
  ADMISSION_CONTEXT a={0};a.SchedulerInitialized=1;AppleAgxSchedulerInitialize(&a.Scheduler);
  DXGKARG_SUBMITCOMMANDVIRTUAL args={0};args.SubmissionFenceId=10;
  inner_status=statuses[i];diagnostics=downstream=0;
  NTSTATUS got=AdmissionDdiSubmitCommandVirtual(&a,&args);
  assert(got==(i?STATUS_INVALID_PARAMETER:STATUS_SUCCESS));
  assert(!a.G4SubmitVirtualInFlight&&!a.SchedulerLock);
  if(i&&i!=2)assert(diagnostics==1&&downstream==(unsigned)statuses[i]);
  if(i)assert(AppleAgxSchedulerCurrentFence(&a.Scheduler,0,0)==10);
 }
 ADMISSION_CONTEXT a={0};a.SchedulerInitialized=1;AppleAgxSchedulerInitialize(&a.Scheduler);
 assert(AppleAgxSchedulerQueueFence(&a.Scheduler,0,0,1));
 assert(AppleAgxSchedulerActivateFence(&a.Scheduler,0,0,1));
 assert(AppleAgxSchedulerQueueFence(&a.Scheduler,0,0,2));
 DXGKARG_SUBMITCOMMANDVIRTUAL args={0};args.SubmissionFenceId=3;inner_status=(NTSTATUS)0x80000011;
 assert(AdmissionDdiSubmitCommandVirtual(&a,&args)==STATUS_INVALID_PARAMETER);
 assert(a.Scheduler.ActiveFence==1&&a.Scheduler.QueueCount==1&&!a.Scheduler.CompletedFence);
 assert(AppleAgxSchedulerCompleteActiveFence(&a.Scheduler,0,0,1));assert(a.Scheduler.CompletedFence==1);
 assert(AppleAgxSchedulerActivateFence(&a.Scheduler,0,0,2));
 assert(AppleAgxSchedulerCompleteActiveFence(&a.Scheduler,0,0,2));assert(a.Scheduler.CompletedFence==3);
 return 0;
}
'''

class SubmitRejection(unittest.TestCase):
    def test_real_virtual_boundary_whitelists_status_and_orders_rejected_fence(self):
        source = SHIM + function_body(SOURCE.read_text(), 'AdmissionDdiSubmitCommandVirtual') + CASES
        with tempfile.TemporaryDirectory() as td:
            p = Path(td);(p/'test.c').write_text(source)
            r = subprocess.run(['clang','-std=c11','-Wno-macro-redefined','-fsanitize=address,undefined',
                '-I'+str(SHARED/'include'),str(p/'test.c'),str(SHARED/'src/apple_agx_scheduler.c'),
                '-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
            r = subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(r.returncode,0,r.stderr)
