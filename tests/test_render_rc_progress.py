"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Driver stamp progress/serial outputs are not firmware progress evidence. A
bracketed sample must show firmware 3D reached with successor compute pending,
stable threshold and exact visible flight, including wrap and failed reads.
The opt-in checkpoint must not admit other pair classes or bypass old guards.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT=Path(__file__).resolve().parents[1]
INC=ROOT/'drivers/apple-agx/render-admission/include'
WORKER=ROOT/'drivers/apple-agx/render-admission/src/backend_platform_windows.c'

class RcProgress(unittest.TestCase):
    def test_firmware_evidence_and_bounded_capture(self):
        self.assertTrue((INC/'render_rc_progress.h').exists())
        code=r'''
#include "render_rc_progress.h"
#include <assert.h>
#include <string.h>
static unsigned data[11], calls, fail, change;
static unsigned char read_word(void *ctx,const volatile unsigned *p,unsigned *v) {
 (void)ctx; ++calls; *v=*p;
 if(change && calls==13) ++*v;
 return calls!=fail;
}
int main(void) {
 ADMISSION_RC_SAMPLE s={0};const volatile unsigned *p[11];unsigned i;
 ADMISSION_RC_RECEIPT r={0};
 for(i=0;i<11;++i)p[i]=&data[i];
 s.ExpectedR=0xffffff00u;s.ExpectedC=0x300;s.EventCount=5;s.PreviousThreshold=4;
 s.CFlags=3;s.RFence=99;s.CFence=100;
 s.RunningKind=1;s.NextKind=3;s.Barrier=3;
 s.RFlight=1;s.CFlight=2;s.WaitLow=4096;s.WaitValue=s.ExpectedR;
 data[0]=5;data[1]=0x200;data[2]=0x100;data[3]=0; /* firmware ahead, driver held */
 AdmissionRcRead(&s,p,read_word,0);assert(AdmissionRcEvidence(&s)==2);
 for(unsigned rk=1;rk<=3;++rk) for(unsigned nk=1;nk<=3;++nk) {
  s.RunningKind=rk;s.NextKind=nk;s.Barrier=nk==1 ? (rk==3 ? 1:2):(rk==3 ? 4:3);
  assert(AdmissionRcEvidence(&s)==2);
  s.Barrier=0;assert(!AdmissionRcEvidence(&s));
 }
 s.RunningKind=1;s.NextKind=3;s.Barrier=3;
 s.CFlight=1;assert(!AdmissionRcEvidence(&s));
 s.CFlight=3;assert(!AdmissionRcEvidence(&s));
 s.CFlight=2;s.RFlight=2;assert(!AdmissionRcEvidence(&s));
 s.RFlight=3;assert(!AdmissionRcEvidence(&s));s.RFlight=1;
 data[2]=0xfffffe00u;data[3]=0x100;calls=0;
 AdmissionRcRead(&s,p,read_word,0);assert(!AdmissionRcEvidence(&s)); /* driver != fw */
 data[2]=0x100;
 for(i=1;i<=13;++i) { calls=0;fail=i;AdmissionRcRead(&s,p,read_word,0);assert(!AdmissionRcEvidence(&s)); }
 fail=0;calls=0;change=1;AdmissionRcRead(&s,p,read_word,0);assert(!AdmissionRcEvidence(&s));
 change=0;calls=0;data[1]=0x300;AdmissionRcRead(&s,p,read_word,0);assert(!AdmissionRcEvidence(&s));
 calls=0;data[1]=0x200;data[0]=4;s.CFlags=0;
 AdmissionRcRead(&s,p,read_word,0);assert(AdmissionRcEvidence(&s)==1);
 s.ExpectedC=0;assert(!AdmissionRcEvidence(&s));s.ExpectedC=0x300;
 for(i=0;i<ADMISSION_RC_SAMPLES+3;++i) {s.Stage=i;AdmissionRcAppend(&r,&s);}
 assert(r.Count==ADMISSION_RC_SAMPLES && r.Overflow==3);
 assert(r.Samples[0].Stage==0 && r.Samples[ADMISSION_RC_SAMPLES-1].Stage==ADMISSION_RC_SAMPLES-1);
 for(i=0;i<6;++i) for(unsigned j=0;j<6;++j)
  assert(AdmissionRcPairAllowed(1,i,j)==(i==1 && j==3));
 assert(AdmissionRcPairAllowed(0,2,2));
 memset(&r,0,sizeof(r));AdmissionRcInitialize(&r);
 assert(r.Version==2 && r.Bytes==19984 && sizeof(s)==156);
 s.Stage=ADMISSION_RC_POLL;AdmissionRcAppend(&r,&s);AdmissionRcAppend(&r,&s);
 assert(r.Count==1);
 s.RunningKind=3;s.NextKind=1;s.Barrier=1;AdmissionRcAppend(&r,&s);
 assert(r.Count==2); /* equal words cannot hide a new class */
 ++s.CFlight;AdmissionRcAppend(&r,&s);assert(r.Count==3);
 ++s.WaitLow;AdmissionRcAppend(&r,&s);assert(r.Count==4);
 ++s.ExpectedC;AdmissionRcAppend(&r,&s);assert(r.Count==5);

}
'''
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'rc.c';b=Path(d)/'rc';c.write_text(code)
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',
                '-ftrivial-auto-var-init=pattern','-fsanitize=address,undefined','-I',str(INC),str(c),'-o',str(b)],check=True)
            subprocess.run([str(b)],check=True)

    def test_switch_gates_and_sampling_sites(self):
        text=WORKER.read_text()
        self.assertTrue('L"Wom1RcDepth2"' in text)
        self.assertTrue('L"Wom1RcReceipt"' in text)
        self.assertIn('AdmissionRcPairAllowed(runtime->RcDepth2, s.Running.Kind, s.Next.Kind)', text)
        self.assertIn('runtime->Initdata.RenderSharedMemory.NotifierFlight &&',text)
        self.assertIn('ADMISSION_RC_BUILT',text)
        self.assertIn('ADMISSION_RC_SUBMITTED',text)
        self.assertIn('ADMISSION_RC_POLL',text)
        self.assertIn('L"Wom1RcProgress"',text)

    def test_actual_windows_sampler_uses_firmware_objects_and_exact_flights(self):
        text=WORKER.read_text()
        start=text.index('static const volatile APPLE_AGX_U32 *AdmissionRcObject(')
        end=text.index('/* EXP1075 kernel dump:',start)
        body=text[start:end]
        request=text[text.index('static VOID AdmissionRcExportRequest('):]
        request=request[:request.index('\n}\n')+3]
        code=r'''
#include "render_job_window.h"
#include "apple_agx_render_shared_memory.h"
#include "apple_agx_g13_queue_runtime.h"
#include "render_depth2_drill.h"
#include "render_rc_progress.h"
#include <assert.h>
#include <string.h>
#include <stddef.h>
typedef unsigned long ULONG; /* distinct WDK-style ULONG / U32 base types */
typedef unsigned long long ULONGLONG;
typedef void VOID;
#define TRUE 1
typedef struct {
 struct {APPLE_AGX_RENDER_SHARED_MEMORY_OWNER RenderSharedMemory;} Initdata;
 struct {struct {APPLE_AGX_G13_QUEUE_RUNTIME Runtime;} QueueProvider;} Provider;
 unsigned RcReceiptEnabled, RcExportPending;
 ADMISSION_DEPTH2_DRILL_RECEIPT Depth2Drill;
 ADMISSION_RC_SAMPLE RcCurrent;
 ADMISSION_RC_RECEIPT RcReceipt;
} ADMISSION_PLATFORM_RUNTIME;
static unsigned reads;
static APPLE_AGX_BACKEND_BOOL AdmissionTransportReadU32(void *c,
 const volatile APPLE_AGX_BACKEND_U32 *p,APPLE_AGX_BACKEND_U32 *v) {
 (void)c;++reads;*v=*p;return 1;
}
static ULONGLONG AdmissionJobQpc(void) {return 0x100000002ULL;}
'''+request+body+r'''
int main(void) {
 static ADMISSION_PLATFORM_RUNTIME r;
 static unsigned objects[40][0x10000/4];
 APPLE_AGX_RENDER_SHARED_MEMORY_OWNER *o=&r.Initdata.RenderSharedMemory;
 APPLE_AGX_G13_QUEUE_RUNTIME *q=&r.Provider.QueueProvider.Runtime;
 unsigned i;
 o->ObjectCount=40;
 for(i=0;i<40;++i){o->Objects[i].CpuAddress=objects[i];o->Objects[i].Length=sizeof(objects[i]);}
 objects[10][0]=0x100;objects[9][0]=0x100;objects[27][0]=0;
 objects[12][0]=5;objects[39][APPLE_AGX_COMPUTE_FIRMWARE_STAMP_OFFSET/4]=0x200;
 q->Config.D3.Stamp=&objects[27][0];q->Config.Compute.Stamp=&objects[39][0];
 q->Config.Ta.CpuWritePointer=&objects[25][16];q->Config.Ta.GpuDonePointer=&objects[25][0];
 q->Config.D3.CpuWritePointer=&objects[24][16];q->Config.D3.GpuDonePointer=&objects[24][0];
 q->Config.Compute.CpuWritePointer=&objects[36][16];q->Config.Compute.GpuDonePointer=&objects[36][0];
 r.RcCurrent.RFence=99;r.RcCurrent.CFence=100;
 r.RcCurrent.RunningKind=1;r.RcCurrent.NextKind=3;r.RcCurrent.Barrier=3;
 r.RcCurrent.WaitLow=4096;r.RcCurrent.WaitValue=0xffffff00;
 r.RcCurrent.ExpectedR=0xffffff00;r.RcCurrent.ExpectedC=0x300;
 o->Flights[0].Fence=99;o->Flights[0].RenderSlot=1;
 o->Flights[1].Fence=100;o->Flights[1].SidecarSet=1;
 o->Flights[1].EventCount=5;o->Flights[1].PreviousThreshold=4;
 o->Flights[1].Visible=o->Flights[1].Published=1;
 r.Depth2Drill.Mode=1;r.Depth2Drill.Queue.HeadFence=99;r.Depth2Drill.Queue.NextFence=100;
 AdmissionRcSampleWindows(&r,ADMISSION_RC_REPORTED,99,0);
 assert(r.RcExportPending && r.Depth2Drill.ReportedCount==1 && !reads);
 r.RcExportPending=0;
 AdmissionRcSampleWindows(&r,ADMISSION_RC_REPORTED,101,0);
 assert(!r.RcExportPending && !reads); /* unrelated later job */
 AdmissionRcSampleWindows(&r,ADMISSION_RC_REPORTED,100,0);
 assert(r.RcExportPending && r.Depth2Drill.ReportedCount==2 && !reads);
 r.RcExportPending=0; r.Depth2Drill.Mode=0;
 AdmissionRcSampleWindows(&r,ADMISSION_RC_POLL,0,0);assert(!reads && !r.RcReceipt.Count);
 r.RcReceiptEnabled=1;AdmissionRcSampleWindows(&r,ADMISSION_RC_POLL,0,0);
 assert(reads==13 && r.RcReceipt.Count==1 && r.RcExportPending);
 ADMISSION_RC_SAMPLE *s=&r.RcReceipt.Samples[0];
 assert(AdmissionRcEvidence(s)==2 && s->RFlight==1 && s->CFlight==2);
 assert(s->RSlot==1 && s->CSet==1 && s->QpcLow==2 && s->QpcHigh==1);
 assert(s->Words[2]==0x100 && s->Words[3]==0);
 AdmissionRcSampleWindows(&r,ADMISSION_RC_POLL,0,0);assert(r.RcReceipt.Count==1);
 o->Objects[10].Length=3;
 AdmissionRcSampleWindows(&r,ADMISSION_RC_POLL,0,0);assert(r.RcReceipt.Count==2);
 assert(!AdmissionRcEvidence(&r.RcReceipt.Samples[1]));
 assert(AdmissionRcObject(o,40,0)==NULL && AdmissionRcObject(o,10,4)==NULL);
 o->Objects[10].Length=sizeof(objects[10]);
 objects[9][0]=0x100;objects[10][0]=0x300;
 objects[39][APPLE_AGX_COMPUTE_FIRMWARE_STAMP_OFFSET/4]=0x200;
 objects[39][0]=0x123;objects[27][0]=0x456; /* distinct driver sentinels */
 for(unsigned rk=1;rk<=3;++rk) for(unsigned nk=1;nk<=3;++nk) {
  r.RcCurrent.RunningKind=rk;r.RcCurrent.NextKind=nk;
  r.RcCurrent.Barrier=AdmissionJobWindowBarrier(rk,nk);
  r.RcCurrent.ExpectedR=rk==3 ? 0x200:0x300;
  r.RcCurrent.ExpectedC=0x500;r.RcCurrent.WaitValue=r.RcCurrent.ExpectedR;
  AdmissionRcSampleWindows(&r,ADMISSION_RC_BUILT,0,0);
  s=&r.RcReceipt.Samples[r.RcReceipt.Count-1];
  assert(s->Words[1]==(nk==1 ? 0x100u:0x200u));
  assert(s->Words[11]==s->Words[1]);
  assert(s->Words[2]==(rk==3 ? 0x200u:0x300u));
  assert(s->Words[3]==(rk==3 ? 0x123u:0x456u));
  assert(AdmissionRcTypedPair(rk,nk,s->Barrier));
  assert(AdmissionRcEvidence(s)==2);
 }

}
'''
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'rc.c';b=Path(d)/'rc';c.write_text(code)
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',
                '-ftrivial-auto-var-init=pattern','-fsanitize=address,undefined','-I',str(INC),
                '-I',str(ROOT/'drivers/apple-agx/shared/include'),str(c),
                str(ROOT/'drivers/apple-agx/render-admission/src/render_job_window.c'),'-o',str(b)],check=True)
            subprocess.run([str(b)],check=True)

    def test_dirty_receipt_bypasses_periodic_export_throttle(self):
        text=WORKER.read_text()
        start=text.index('  if (',text.index('  ++runtime->JobTimingWorkers;'))
        end=text.index('  runtime->JobTimingExportQpc = before;',start)
        gate=text[start:end]
        self.assertTrue('RcExportPending' in gate)
        code = r'''
#include <assert.h>
typedef struct {unsigned RcExportPending,JobTimingExportQpc,JobTimingLock;
 struct {unsigned QpcFrequency;} JobTiming;} R;
static void KeReleaseSpinLock(unsigned *l,unsigned i) {(void)l;(void)i;}
static unsigned exported;
static void run(R *runtime) {unsigned before=11,oldIrql=0;
'''+gate+r'''
 ++exported;
}
int main(void) {R r={0,10,0,{100}};run(&r);assert(!exported);
 r.RcExportPending=1;run(&r);assert(exported==1);}
'''
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'gate.c';b=Path(d)/'gate';c.write_text(code)
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',str(c),'-o',str(b)],check=True)
            subprocess.run([str(b)],check=True)

    def test_actual_pair_targets_follow_first_and_last_queue(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A render successor's final 3D target must not substitute for its first
        TA/prelude target, and a compute predecessor must use compute completion.
        """
        text=WORKER.read_text()
        start=text.index('    runtime->RcCurrent.RunningKind =')
        metadata=text[start:text.index('    if (context &&',start)]
        start=text.index('    runtime->RcCurrent.ExpectedC = Job->')
        render_target=text[start:text.index(';',start)+1]
        self.assertIn('runtime->RcCurrent.ExpectedC = input.StampValue;',text)
        code=r'''
#include "render_rc_progress.h"
#include "render_job_window.h"
#include <assert.h>
typedef struct {unsigned ComputeExpectedStamp,TaExpectedStamp,D3ExpectedStamp,ComputeWorkAddressCount;} JOB;
typedef struct {ADMISSION_RC_SAMPLE RcCurrent;struct {JOB PendingJob;} Backend;} RUNTIME;
static void metadata(RUNTIME *runtime,unsigned running,unsigned next) {
 struct {struct {unsigned Kind;} Running,Next;} s={{running},{next}};
 unsigned barrier=AdmissionJobWindowBarrier(running,next);
'''+metadata+r'''
}
static void target(RUNTIME *runtime,JOB *Job) {
'''+render_target+r'''
}
int main(void) {
 RUNTIME r={0};JOB job={0x200,0x300,0x400,0};
 r.Backend.PendingJob=job;
 for(unsigned running=1;running<=3;++running) for(unsigned next=1;next<=3;++next) {
  metadata(&r,running,next);
  assert(r.RcCurrent.ExpectedR==(running==3 ? 0x200u:0x400u));
  assert(AdmissionRcTypedPair(running,next,r.RcCurrent.Barrier));
 }
 target(&r,&job);assert(r.RcCurrent.ExpectedC==0x300);
 job.ComputeWorkAddressCount=1;target(&r,&job);assert(r.RcCurrent.ExpectedC==0x200);
}
'''
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'targets.c';b=Path(d)/'targets';c.write_text(code)
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',
                '-ftrivial-auto-var-init=pattern','-fsanitize=address,undefined','-I',str(INC),
                str(c),str(ROOT/'drivers/apple-agx/render-admission/src/render_job_window.c'),
                '-o',str(b)],check=True)
            subprocess.run([str(b)],check=True)
