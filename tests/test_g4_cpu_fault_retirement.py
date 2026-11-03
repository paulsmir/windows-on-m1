"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A terminal CPU paging/present error used to notify a known fault fence while
leaving that fence active and QueryCurrentFence one packet behind. Replay the
worker's terminal decision and real scheduler with pending preemption, wrap,
and a foreign active fence; no completion may be invented for another packet.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_g4_submit_virtual_replay import ROOT, function_body


class CpuFaultRetirement(unittest.TestCase):
    def test_terminal_cpu_failure_advances_only_its_fence(self):
        render = ROOT / 'drivers/apple-agx/render-admission'
        paging = (render / 'src/paging_windows.c').read_text()
        scheduler = (render / 'src/scheduler_windows.c').read_text()
        worker = paging[paging.index('static VOID AdmissionPagingWorker('):]
        # Replay the actual worker decision, with the Windows services at its
        # boundary supplied below. The original source takes only success.
        if 'AdmissionPagingRecordTerminal(context,' in worker:
            helper = function_body(paging, 'AdmissionPagingRecordTerminal')
            start = worker.index('  if (!AdmissionPagingRecordTerminal(context,')
        else:
            helper = ''
            start = worker.index('  if (NT_SUCCESS(status) &&')
        decision = worker[start:worker.index('\n  context->PagingCompletionStatus = status;', start)]
        shim = r'''
#include <assert.h>
#include <stdint.h>
#include "apple_agx_scheduler.h"
typedef int BOOLEAN,NTSTATUS,KIRQL;typedef int32_t LONG;typedef unsigned UINT;
#define _Use_decl_annotations_
#define TRUE 1
#define FALSE 0
#define NULL ((void*)0)
#define NT_SUCCESS(x) ((x)>=0)
#define STATUS_SUCCESS 0
#define STATUS_INVALID_DEVICE_STATE ((int)0xc0000184u)
#define ADMISSION_SCHEDULER_NODE 0u
#define ADMISSION_SCHEDULER_ENGINE 0u
typedef struct {APPLE_AGX_SCHEDULER Scheduler, PagingScheduler;unsigned PagingActiveNode,PagingNodeDispatchedFence;int SchedulerLock,SchedulerInitialized,SchedulerFaulted,PagingCompletionStatus,PagingWorkersActive,PagingCallbacksActive,PagingPending;unsigned DispatchedFence,PagingFence;} ADMISSION_CONTEXT;
static int InterlockedCompareExchange(int *p,int v,int c){int old=*p;if(old==c)*p=v;return old;}
static void KeAcquireSpinLock(int *p,int *old){assert(!*p);*p=1;*old=0;}
static void KeReleaseSpinLock(int *p,int old){assert(*p&&!old);*p=0;}
static int InterlockedDecrement(int *p){return --*p;}
static unsigned notifications;
'''
        cases = r'''
static void run(unsigned first,int status,int preempt){
 ADMISSION_CONTEXT c={0};c.SchedulerInitialized=1;AppleAgxSchedulerInitialize(&c.Scheduler);
 unsigned f=first+1;if(!f)f=1;
 c.Scheduler.CompletedFence=c.Scheduler.LastSubmittedFence=first;
 assert(AppleAgxSchedulerQueueFence(&c.Scheduler,0,0,f));
 assert(AppleAgxSchedulerActivateFence(&c.Scheduler,0,0,f));
 c.PagingFence=c.DispatchedFence=f;
 if(preempt)assert(AppleAgxSchedulerBeginBoundaryPreemption(&c.Scheduler,0,0,77,f,f));
 assert(terminal(&c,status));assert(!c.Scheduler.ActiveFence&&!c.DispatchedFence);
 assert(AppleAgxSchedulerCurrentFence(&c.Scheduler,0,0)==f);
 assert(c.Scheduler.LastAbortedFence==(status<0?f:0));
 if(preempt){APPLE_AGX_PREEMPTION p;
  assert(AppleAgxSchedulerClaimBoundaryPreemption(&c.Scheduler,&p));assert(p.LastCompletedFence==f);
 }
 assert(!terminal(&c,status)); /* no second terminal transition */
}
int main(void){
 for(unsigned i=0;i<2;++i)for(int p=0;p<2;++p){
  run(i?0xffffffffu:9,(int)0xc000000du,p);run(i?0xffffffffu:9,0,p);
 }
 ADMISSION_CONTEXT c={0};c.SchedulerInitialized=1;AppleAgxSchedulerInitialize(&c.Scheduler);
 assert(AppleAgxSchedulerQueueFence(&c.Scheduler,0,0,1));
 assert(AppleAgxSchedulerActivateFence(&c.Scheduler,0,0,1));
 c.DispatchedFence=1;c.PagingFence=2;
 c.PagingPending=1;
 assert(!terminal(&c,(int)0xc000000du));assert(c.Scheduler.ActiveFence==1&&c.DispatchedFence==1);
 assert(!c.Scheduler.CompletedFence&&c.PagingPending==1&&!c.PagingWorkersActive);
}
'''
        source = shim + function_body(scheduler, 'AdmissionSchedulerRecordCompletion') + helper
        source += '\nstatic void worker_gate(ADMISSION_CONTEXT *context, NTSTATUS status){\n'
        source += decision + '\n++notifications;--context->PagingWorkersActive;}\n'
        source += '''static int terminal(ADMISSION_CONTEXT *c,NTSTATUS status){
 unsigned before=notifications;++c->PagingWorkersActive;++c->PagingCallbacksActive;worker_gate(c,status);
 return notifications!=before;}\n''' + cases
        with tempfile.TemporaryDirectory() as td:
            p = Path(td);(p/'test.c').write_text(source)
            result = subprocess.run(['clang','-std=c11','-fsanitize=address,undefined',
                '-I'+str(ROOT/'drivers/apple-agx/shared/include'),str(p/'test.c'),
                str(ROOT/'drivers/apple-agx/shared/src/apple_agx_scheduler.c'),'-o',str(p/'test')],
                capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
            result = subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stderr)
