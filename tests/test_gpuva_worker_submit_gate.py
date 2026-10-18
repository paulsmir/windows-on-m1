"""Replay the actual worker pre-submit span with an observed management timeout."""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT=Path(__file__).resolve().parents[1]
SOURCE=ROOT/'drivers/apple-agx/render-admission/src/backend_platform_windows.c'

class GpuvaWorkerSubmitGateTests(unittest.TestCase):
    def test_ready_g3_work_and_real_submission_guards(self):
        source=SOURCE.read_text();start=source.index('  submission.DmaSubmissionEnd = description.DmaEnd;')
        start=source.index('\n',start)+1
        end=source.index('  result = AppleAgxBackendRuntimeSubmit(&runtime->Backend, &submission);',start)
        end=source.index('\n',end)
        span=source[start:end]
        program=r'''
#include <assert.h>
#include <stdint.h>
#include <stddef.h>
#define MAXULONG UINT32_MAX
#define FALSE 0
#define NT_SUCCESS(s) ((s)>=0)
#define J313_AGX_G2_HEARTBEAT_TIMEOUT_MS 500
#define APPLE_AGX_SUBMIT_QUALIFICATION_NOT_ENABLED 1
typedef uint32_t ULONG;
typedef uint32_t APPLE_AGX_RTKIT_U32;
#define APPLE_AGX_RTKIT_RUNTIME_DRAIN_LIMIT 64u
typedef uintptr_t ULONG_PTR;
typedef int APPLE_AGX_RTKIT_SESSION_RESULT;
enum { AppleAgxRtkitSessionResultOk=0 };
typedef struct {void *GpuvaG3Process;} ADMISSION_RENDER_CONTEXT;
struct adapter {int SchedulerFaulted, CompletedFence;};
struct runtime {
 struct {int Sequence;unsigned Calls,Fence,Result;uint64_t StartMs,EndMs,DeadlineMs;unsigned RxBefore,RxAfter;uint64_t LastRxPayload;unsigned LastRxEndpoint;} HeartbeatReceipt;
 struct {unsigned ReceivedCount,LastRxEndpoint;uint64_t LastRxPayload;} Rtkit;
 int AscIo,Backend;
};
static int ping_result, begin_result, prepare_result, submitted, pings;
static int notification_result, pending_notifications, pending_at_submit;
static int AppleAgxRtkitSessionDrainRuntime(void *a,void *b,unsigned max,unsigned *drained){(void)a;(void)b;*drained=0;if(notification_result)return notification_result;*drained=(unsigned)pending_notifications<max?(unsigned)pending_notifications:max;pending_notifications-=(int)*drained;return 0;}
static void InterlockedIncrement(int *v){++*v;}
static void InterlockedCompareExchange(int *v,int n,int old){if(*v==old)*v=n;}
static uint64_t AdmissionPlatformNowMs(void){return 153791;}
static int AppleAgxRtkitSessionHeartbeat(void *a,void *b,uint64_t d){(void)a;(void)b;(void)d;++pings;return ping_result;}
static void AdmissionFlushGdiReceipt(void *a){(void)a;}
static void AdmissionRenderCorrelationWorkerWindows(void *a,unsigned b,int c,unsigned d){(void)a;(void)b;(void)c;(void)d;}
static void AdmissionPlatformWorkerFinished(void *r){(void)r;}
static int AdmissionGpuvaG3BeginJob(void *a,void *c,unsigned f){(void)a;(void)c;(void)f;return begin_result;}
static int AdmissionPrepareG4Manager(void *r){(void)r;return prepare_result;}
static int AppleAgxBackendRuntimeSubmit(void *r,void *s){(void)r;(void)s;++submitted;pending_at_submit=pending_notifications;return 0;}
static void worker(struct adapter *adapter,struct runtime *runtime){
 ADMISSION_RENDER_CONTEXT c={(void*)1};
 struct {unsigned Fence;uintptr_t ContextToken;} description={9189,(uintptr_t)&c};
 int submission=0,result=0;
 APPLE_AGX_RTKIT_SESSION_RESULT heartbeatResult=0;
''' +span+r'''
 (void)result;(void)heartbeatResult;
}
static void run(int ping,int begin,int prepare,int expected_submit,int expected_fault){
 struct adapter a={0,9188};struct runtime r={0};
 ping_result=ping;begin_result=begin;prepare_result=prepare;submitted=pings=0;pending_notifications=8;pending_at_submit=-1;
 worker(&a,&r);assert(submitted==expected_submit);assert((a.SchedulerFaulted!=0)==expected_fault);
 #ifdef APPLE_AGX_GPUVA_G3_QUALIFICATION
 if(expected_submit)assert(pending_at_submit==0); /* Preserve the sole ASC consumer. */
#endif
 assert(a.CompletedFence==9188); /* Never invent a completed fence. */
}
int main(void){
#ifdef APPLE_AGX_GPUVA_G3_QUALIFICATION
 run(3,0,1,1,0); /* Observed missed pong must not discard ready G3 work. */
 run(0,-1,1,0,1); /* Actual ownership failure remains fail-closed. */
 run(0,0,0,0,1); /* Actual manager preparation failure remains fail-closed. */
 notification_result=8;run(0,0,1,0,1); /* Actual firmware crash remains fatal. */
 notification_result=0;
#else
 run(3,0,1,0,1); /* Legacy diagnostic profile unchanged. */
 run(0,0,1,1,0);
#endif
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            c=Path(tmp)/'worker.c';c.write_text(program)
            for model in ('g3','legacy'):
                exe=Path(tmp)/model
                flags=['-DAPPLE_AGX_GPUVA_G3_QUALIFICATION=1'] if model=='g3' else []
                subprocess.run(['clang','-std=c11','-Werror','-Wno-unused-function',*flags,str(c),'-o',str(exe)],check=True,capture_output=True,text=True)
                r=subprocess.run([str(exe)],capture_output=True,text=True)
                self.assertEqual(r.returncode,0,model+': '+r.stderr)
if __name__=='__main__':unittest.main()
