"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The compositor diagnostic must join queue time to the exact observed latch,
never a stale interrupt or a periodic vsync. Otherwise it misattributes DWM
delivery gaps to DCP latency. Executes the production ISR with fake MMIO only.
"""
import unittest
import subprocess
import tempfile
from pathlib import Path
from test_scanout_vsync_replay import compile_and_run, function, ROOT


class ScanoutCadenceTests(unittest.TestCase):
    def test_deferred_queue_stamps_before_broker_and_skips_busy(self):
        source = (ROOT/'drivers/apple-agx/render-admission/src/scanout_windows.c').read_text()
        body = function(source, '_Use_decl_annotations_ NTSTATUS AdmissionScanoutQueueDeferred(')
        shim = r"""
#include <assert.h>
#include <stddef.h>
typedef unsigned long long APPLE_AGX_U64,ULONGLONG;
typedef long long LONG64;
typedef int NTSTATUS,APPLE_AGX_FIXED_PANEL_RESULT;
#define _Use_decl_annotations_
#define STATUS_SUCCESS 0
#define STATUS_DEVICE_BUSY -1
#define STATUS_DEVICE_HARDWARE_ERROR -2
#define AppleAgxFixedPanelOk 0
#define AppleAgxFixedPanelPresentPending 1
#define ADMISSION_MEMORY_LOCAL_SEGMENT 2
typedef struct {int SourceVisible;} ADMISSION_CONTEXT;
typedef struct {int IrqEnabled,Stopping,PresentGate,Panel,PendingValid,NextMpo,PendingMpo;
 LONG64 DeferredPrimaryAddress,PendingPhysicalAddress,PendingSequence,NextMpoPresentId,PendingMpoPresentId,PendingQueuedTime100ns;} ADMISSION_SCANOUT_RUNTIME;
static ADMISSION_SCANOUT_RUNTIME r;
static unsigned calls,clock_calls;
static ADMISSION_SCANOUT_RUNTIME* AdmissionScanoutGet(ADMISSION_CONTEXT*c){(void)c;return &r;}
static int InterlockedCompareExchange(int*p,int v,int c){int o=*p;if(o==c)*p=v;return o;}
static int InterlockedExchange(int*p,int v){int o=*p;*p=v;return o;}
static LONG64 InterlockedExchange64(LONG64*p,LONG64 v){LONG64 o=*p;*p=v;return o;}
static ULONGLONG AdmissionScanoutTime100ns(void){++clock_calls;return 1234;}
static int AppleAgxFixedPanelTakeDeferred(int*p,APPLE_AGX_U64*o){(void)p;*o=42;return 1;}
static int AppleAgxFixedPanelQueuePresent(int*p,int s,APPLE_AGX_U64 o,APPLE_AGX_U64*q){
 (void)p;assert(s==2 && o==42);assert(r.PendingQueuedTime100ns==1234);
 ++calls;*q=99;return 0;
}
"""
        scenario = r"""
int main(void){
 ADMISSION_CONTEXT c={1}; r.IrqEnabled=1;r.DeferredPrimaryAddress=0x123000;
 assert(AdmissionScanoutQueueDeferred(&c)==0);
 assert(calls==1 && clock_calls==1 && r.PendingSequence==99 && r.PendingValid);
 assert(AdmissionScanoutQueueDeferred(&c)==STATUS_DEVICE_BUSY);
 c.SourceVisible=0;assert(AdmissionScanoutQueueDeferred(&c)==0);
 assert(calls==1 && clock_calls==1);
 return 0;
}
"""
        with tempfile.TemporaryDirectory() as tmp:
            p = Path(tmp)/'queue.c'; p.write_text(shim+body+scenario)
            out = Path(tmp)/'queue'
            subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
                            str(p), '-o', str(out)], check=True)
            subprocess.run([str(out)], check=True)

    def test_exact_latch_keeps_queue_timestamp_and_identity(self):
        compile_and_run(r"""
int main(void) {
 ADMISSION_CONTEXT c; ADMISSION_SCANOUT_RUNTIME r;
 setup(&c,&r);
 (void)&finish_start; (void)&AdmissionScanoutTimerDpc;
 pending(&r,2,0x1500000000ULL);
 r.PendingQueuedTime100ns=9000000;
 assert(AdmissionScanoutInterrupt(&c));
 assert(r.VsyncReceipt.Events[0].Kind==8);
 assert(r.VsyncReceipt.Events[0].Time100ns==9000000);
 assert(r.VsyncReceipt.Events[0].PendingSequence==2);
 assert(r.VsyncReceipt.Events[0].PendingAddress==0x1500000000ULL);
 assert(r.VsyncReceipt.Events[1].Kind==3);
 assert(r.VsyncReceipt.Events[1].Time100ns==10000000);
 assert(r.VsyncReceipt.Events[1].ActiveSequence==2);
 assert(!r.PendingQueuedTime100ns);
 assert(notifications==1 && last_address==0x1500000000ULL);
 return 0;
}
""")

    def test_stale_latch_and_timer_do_not_consume_queue_time(self):
        compile_and_run(r"""
int main(void) {
 ADMISSION_CONTEXT c; ADMISSION_SCANOUT_RUNTIME r;
 setup(&c,&r);
 (void)&finish_start; (void)&AdmissionScanoutTimerDpc;
 pending(&r,4,0x1500000000ULL);
 r.PendingQueuedTime100ns=9000000;
 latch=3;
 assert(AdmissionScanoutInterrupt(&c));
 assert(r.PendingQueuedTime100ns==9000000 && r.PendingValid);
 for(unsigned i=0;i<r.VsyncReceipt.EventCount;++i)
   assert(r.VsyncReceipt.Events[i].Kind!=8 && r.VsyncReceipt.Events[i].Kind!=3);
 irq=0; (void)AdmissionScanoutProcessInterrupt(&c,TRUE);
 assert(r.PendingQueuedTime100ns==9000000);
 irq=APPLE_AGX_SCANOUT_IRQ_LATCHED; latch=4;
 assert(AdmissionScanoutInterrupt(&c));
 unsigned queues=0,latches=0;
 for(unsigned i=0;i<r.VsyncReceipt.EventCount;++i) {
   queues+=r.VsyncReceipt.Events[i].Kind==8;
   latches+=r.VsyncReceipt.Events[i].Kind==3;
 }
 assert(queues==1 && latches==1 && notifications==1);
 return 0;
}
""")
