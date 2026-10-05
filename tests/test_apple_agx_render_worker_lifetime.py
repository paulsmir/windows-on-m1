"""A worker must not publish idle while a successor callback remains live."""
import os
import re
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT=Path(__file__).resolve().parents[1]


class WorkerLifetimeTests(unittest.TestCase):
    def test_delayed_cancelled_render_wake_does_not_fault_cpu_or_overtake_notification(self):
        source=(ROOT/'drivers/apple-agx/render-admission/src/backend_platform_windows.c').read_text()
        worker=source[source.index('static VOID AdmissionPlatformWorker('):]
        start=worker.index('  cancelled =')
        end=worker.index('  KeReleaseSpinLock(&adapter->SchedulerLock, old_irql);',start)
        classification=worker[start:end]
        shim=r'''
#include <assert.h>
#include <stdint.h>
#include "apple_agx_scheduler.h"
#include "render_submission.h"
#define TRUE 1
#define FALSE 0
#define AppleAgxBackendRuntimeReady 0
typedef int BOOLEAN;typedef int32_t LONG;
typedef struct {APPLE_AGX_SCHEDULER Scheduler;ADMISSION_RENDER_PACKET RenderPacket;unsigned DispatchedFence;} ADMISSION_CONTEXT;
typedef struct {volatile LONG Stopping,Resetting;int BackendStarted;struct {int Phase;} Backend;} RUNTIME;
static LONG InterlockedCompareExchange(volatile LONG *p,LONG v,LONG e){LONG o=*p;if(o==e)*p=v;return o;}
static int enter_worker(ADMISSION_CONTEXT *adapter,RUNTIME *runtime){
 BOOLEAN activated=FALSE,cancelled=FALSE,deferred=FALSE;ADMISSION_RENDER_PACKET_DESCRIPTION description;
'''
        cases=r'''
 return activated?1:(cancelled||deferred)?0:-1;
}
int main(void){
 ADMISSION_CONTEXT a={0};RUNTIME r={0,0,1,{0}};
 AppleAgxSchedulerInitialize(&a.Scheduler);AdmissionRenderPacketInitialize(&a.RenderPacket);
 assert(AppleAgxSchedulerQueueFence(&a.Scheduler,0,0,3));assert(AppleAgxSchedulerActivateFence(&a.Scheduler,0,0,3));
 a.DispatchedFence=3;assert(enter_worker(&a,&r)==0); /* empty old render, live CPU */
 assert(AppleAgxSchedulerQueueFence(&a.Scheduler,0,0,4));a.RenderPacket.State=AdmissionRenderPacketQueued;a.RenderPacket.Description.Fence=4;
 assert(enter_worker(&a,&r)==0 && a.Scheduler.ActiveFence==3);
 assert(AppleAgxSchedulerCompleteActiveFence(&a.Scheduler,0,0,3));
 assert(enter_worker(&a,&r)==0 && a.Scheduler.QueuedFence==4); /* prior notification still reserved */
 a.DispatchedFence=0;assert(enter_worker(&a,&r)==1);
 assert(a.Scheduler.ActiveFence==4 && a.DispatchedFence==4);return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            program=Path(tmp)/'wake.c';program.write_text(shim+classification+cases);binary=Path(tmp)/'wake'
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined',
                            '-I',str(ROOT/'drivers/apple-agx/shared/include'),'-I',str(ROOT/'drivers/apple-agx/render-admission/include'),
                            str(program),str(ROOT/'drivers/apple-agx/shared/src/apple_agx_scheduler.c'),
                            str(ROOT/'drivers/apple-agx/render-admission/src/render_submission.c'),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_successor_and_nested_return_do_not_publish_premature_idle(self):
        source=(ROOT/'drivers/apple-agx/render-admission/src/backend_platform_windows.c').read_text()
        function=re.search(r'static VOID AdmissionPlatformWorkerFinished\(.*?^}',source,re.S|re.M).group(0)
        shim=r'''
#include <assert.h>
#include <stdint.h>
#define VOID void
#define FALSE 0
#define IO_NO_INCREMENT 0
typedef int32_t LONG;typedef int KIRQL;
typedef struct {int SchedulerLock;} ADMISSION_CONTEXT;
typedef struct {ADMISSION_CONTEXT *Adapter;volatile LONG WorkScheduled,WorkersActive,Stopping,Resetting;int WorkIdle;} ADMISSION_PLATFORM_RUNTIME;
static int locks,dispatches,successor,nested;
static ADMISSION_PLATFORM_RUNTIME *runtime;
static void KeAcquireSpinLock(int *p,int *irql){(void)p;*irql=2;++locks;}
static void KeReleaseSpinLock(int *p,int irql){(void)p;(void)irql;--locks;}
static LONG InterlockedExchange(volatile LONG *p,LONG v){LONG old=*p;*p=v;return old;}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG v,LONG expected){LONG old=*p;if(old==expected)*p=v;return old;}
static LONG InterlockedDecrement(volatile LONG *p){assert(*p>0);return --*p;}
static void KeSetEvent(int *p,int n,int w){(void)n;(void)w;assert(locks==1);assert(!runtime->WorkersActive && !runtime->WorkScheduled);*p=1;}
static void AdmissionSchedulerWorkerFinished(ADMISSION_CONTEXT *c){(void)c;assert(!locks);}
static void AdmissionPlatformWorkerFinished(ADMISSION_PLATFORM_RUNTIME *Runtime);
static void AdmissionDispatchQueuedWork(ADMISSION_CONTEXT *c){
 assert(!locks && c==runtime->Adapter);++dispatches;
 if(successor){successor=0;runtime->WorkScheduled=1;runtime->WorkIdle=0;
   if(nested){++runtime->WorkersActive;AdmissionPlatformWorkerFinished(runtime);assert(!runtime->WorkIdle);}}
}
'''
        cases=r'''
int main(void){
 ADMISSION_CONTEXT c={0};ADMISSION_PLATFORM_RUNTIME r={&c,1,1,0,0,0};runtime=&r;
 successor=1;AdmissionPlatformWorkerFinished(&r);
 assert(r.WorkScheduled==1 && r.WorkersActive==0 && !r.WorkIdle && dispatches==1);
 ++r.WorkersActive;AdmissionPlatformWorkerFinished(&r);
 assert(!r.WorkScheduled && !r.WorkersActive && r.WorkIdle && dispatches==2);
 r=(ADMISSION_PLATFORM_RUNTIME){&c,1,1,0,0,0};successor=1;nested=1;
 AdmissionPlatformWorkerFinished(&r);
 assert(r.WorkIdle && !r.WorkersActive && !r.WorkScheduled && dispatches==4);
 r=(ADMISSION_PLATFORM_RUNTIME){&c,1,1,1,0,0};AdmissionPlatformWorkerFinished(&r);
 assert(r.WorkIdle && dispatches==4);
 r=(ADMISSION_PLATFORM_RUNTIME){&c,1,1,0,1,0};AdmissionPlatformWorkerFinished(&r);
 assert(r.WorkIdle && dispatches==4 && locks==0);return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            program=Path(tmp)/'worker.c';program.write_text(shim+function+cases);binary=Path(tmp)/'worker'
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                            '-fsanitize=address,undefined',str(program),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)

    def test_ready_observation_preserves_busy_and_null_runtime_guards(self):
        # A diagnostic must not admit a busy worker or dereference an absent runtime.
        from test_g4_submit_virtual_replay import function_body
        source=(ROOT/'drivers/apple-agx/render-admission/src/backend_platform_windows.c').read_text()
        functions='\n'.join(function_body(source,n) for n in
            ('AdmissionPlatformRuntimeReadyEx','AdmissionPlatformRuntimeReady'))
        shim=r"""
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#define _Use_decl_annotations_
#define APPLE_AGX_SUBMIT_QUALIFICATION 1
#define TRUE 1
#define FALSE 0
#define AppleAgxBackendRuntimeReady 2
#define AdmissionCompletedOutputEmpty 0
typedef int BOOLEAN;typedef unsigned ULONG;typedef int32_t LONG;typedef int KIRQL;
typedef struct {int Idle,Running;} QUEUE;
typedef struct {int ProviderReady,BackendStarted;struct {int Phase;} Backend;
 void *WorkItem;volatile LONG Stopping,Resetting,WorkScheduled;int OutputLock;
 QUEUE OutputQueue;struct {int Phase;} CompletedOutput;} ADMISSION_PLATFORM_RUNTIME;
typedef struct {void *PlatformRuntime;} ADMISSION_CONTEXT;
static int locks;
static void KeAcquireSpinLock(int *p,int *i){(void)p;*i=2;assert(!locks);++locks;}
static void KeReleaseSpinLock(int *p,int i){(void)p;(void)i;assert(locks==1);--locks;}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG v,LONG e){LONG o=*p;if(o==e)*p=v;return o;}
static int AdmissionOutputQueueIsIdle(QUEUE *q){assert(locks==1);return q->Idle;}
static int AdmissionOutputQueueThreadRunning(QUEUE *q){assert(locks==1);return q->Running;}
"""
        cases=r"""
int main(void){
 ULONG reason=99;ADMISSION_CONTEXT a={0};
 assert(!AdmissionPlatformRuntimeReadyEx(NULL,&reason) && reason==1);
 assert(!AdmissionPlatformRuntimeReadyEx(&a,&reason) && reason==1);
 for(unsigned bits=0;bits<1024;++bits){
  ADMISSION_PLATFORM_RUNTIME r={0};a.PlatformRuntime=&r;
  r.ProviderReady=!(bits&1);r.BackendStarted=!(bits&2);
  r.Backend.Phase=(bits&4)?3:2;r.WorkItem=(bits&8)?NULL:&r;
  r.Stopping=!!(bits&16);r.Resetting=!!(bits&32);r.WorkScheduled=!!(bits&64);
  r.OutputQueue.Idle=!(bits&128);r.OutputQueue.Running=!(bits&256);
  r.CompletedOutput.Phase=!!(bits&512);
  ULONG expected=0;for(unsigned i=0;i<10;++i)if(bits&(1u<<i)){expected=i+2;break;}
  assert(AdmissionPlatformRuntimeReadyEx(&a,&reason)==(bits==0));
  assert(reason==expected && locks==0);
  assert(AdmissionPlatformRuntimeReady(&a)==(bits==0));
 }
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            program=Path(tmp)/'ready.c';program.write_text(shim+functions+cases);binary=Path(tmp)/'ready'
            subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                            '-fsanitize=address,undefined',str(program),'-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
