"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Completion telemetry must survive asynchronous collection, never expose another
process's jobs, silently lose overwritten records, or duplicate retried completion.
Run the production bounded ring under ASan/UBSan, including invalid clocks/status.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
ROOT = Path(__file__).resolve().parents[1]
INC = ROOT/'drivers/apple-agx/render-admission/include'
SRC = ROOT/'drivers/apple-agx/render-admission/src/render_job_timing.c'

class CompletionStream(unittest.TestCase):
    def test_ring_contract(self):
        self.assertIn('AdmissionGpuCompletionRead', SRC.read_text(),
                      'No loss-accounted process-filtered completion stream exists')
        code = r'''
#include <assert.h>
#include <string.h>
#include "render_job_timing.h"
int main(void) {
 static ADMISSION_GPU_COMPLETION_RING ring;
 ADMISSION_GPU_COMPLETION r={0};
 ADMISSION_GPU_COMPLETION_QUERY q={0};
 unsigned i;
 r.Fence=1; r.ProcessId=7; r.CommandVa=0x123456789ULL;
 r.SubmitQpc=100; r.CompleteQpc=200; r.Kind=2;
 r.Start=90; r.End=180;
 assert(AdmissionGpuCompletionRecord(&ring,&r));
 assert(!AdmissionGpuCompletionRecord(&ring,&r)); /* retry */
 AdmissionGpuCompletionRead(&ring,8,&q);
 assert(!q.Count && q.Cursor==1 && !q.Lost);
 memset(&q,0,sizeof(q));
 AdmissionGpuCompletionRead(&ring,7,&q);
 assert(q.Count==1 && q.Record[0].CommandVa==r.CommandVa);
 assert(q.Record[0].SubmitQpc==100 && q.Record[0].CompleteQpc==200);
 AdmissionGpuCompletionRead(&ring,7,&q);
 assert(!q.Count);
 /* A dropped job and invalid timestamp remain explicitly marked, not zero-ms busy. */
 r.Fence=2;r.Status=1;r.Start=500;r.End=0;
 assert(AdmissionGpuCompletionRecord(&ring,&r));
 AdmissionGpuCompletionRead(&ring,7,&q);
 assert(q.Count==1 && q.Record[0].Status==1 && !q.Record[0].End);
 for(i=3;i<ADMISSION_GPU_COMPLETION_CAPACITY+40;i++) {
  r.Fence=i;r.SubmitQpc=i;r.ProcessId=(i%2)?7:8;
  assert(AdmissionGpuCompletionRecord(&ring,&r));
 }
 memset(&q,0,sizeof(q));
 AdmissionGpuCompletionRead(&ring,7,&q);
 assert(q.Lost==39 && q.Count==ADMISSION_GPU_COMPLETION_PAGE);
 for(i=0;i<q.Count;i++) assert(q.Record[i].ProcessId==7);
 { unsigned seen=q.Count; unsigned long long last=q.Cursor;
   while(q.Cursor<q.Newest) {
    AdmissionGpuCompletionRead(&ring,7,&q);
    assert(!q.Lost && q.Cursor>last);last=q.Cursor;seen+=q.Count;
    for(i=0;i<q.Count;i++) assert(q.Record[i].ProcessId==7);
   }
   assert(seen==ADMISSION_GPU_COMPLETION_CAPACITY/2);
 }
 /* Future cursor (adapter reset or corrupt caller) resynchronizes explicitly. */
 q.Cursor=ring.Sequence+1;
 AdmissionGpuCompletionRead(&ring,7,&q);
 assert(q.Reset==1);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'test.c').write_text(code)
            z=subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-I',str(INC),str(p/'test.c'),str(SRC),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
            z=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)


class CompletionWiring(unittest.TestCase):
    def test_capture_precedes_release_and_exports_without_wait(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        Telemetry must be produced on successful/dropped KMD completion before
        firmware storage reuse, and collection must not wait or require queries.
        """
        from test_d3d12_owner_allocation_failure import function
        k=(ROOT/'drivers/apple-agx/render-admission/src/backend_platform_windows.c').read_text()
        capture=function(k,'static VOID AdmissionGpuCompletionCaptureWindows(')
        self.assertIn('AdmissionJobPhaseComplete',capture)
        self.assertIn('AdmissionJobPhaseSubmit',capture)
        self.assertIn('AdmissionGpuCompletionRecord',capture)
        complete=function(k,'static APPLE_AGX_BACKEND_BOOL AdmissionBackendComplete(')
        self.assertIn('AdmissionGpuCompletionCaptureWindows(runtime, Fence, Status)',complete)
        u=(ROOT/'drivers/apple-agx/render-admission/umd/src/umd_gpuva_windows.c').read_text()
        drain=function(u,'VOID AdmissionUmdGpuCompletionDrain(')
        self.assertIn('page < 4u',drain)
        self.assertIn('AdmissionUmdDiagnosticEnabled()',drain)
        self.assertNotIn('Wait',drain)
        self.assertIn('measure-gpu-completion',drain)
        self.assertIn('measure-gpu-stages',drain)
        owner=(ROOT/'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        self.assertIn('AdmissionUmdGpuCompletionDrain',function(owner,'static HRESULT submit_owner('))

class CompletionCapture(unittest.TestCase):
    def test_real_capture_reports_missing_slot_and_preserves_raw_stages(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A delayed completion whose 64-entry timing slot was overwritten must
        report lost timing coverage. Capture must use firmware storage and the
        first KMD completion clock, and suppress successful timing on abort.
        """
        from test_d3d12_owner_allocation_failure import function
        text=(ROOT/'drivers/apple-agx/render-admission/src/backend_platform_windows.c').read_text()
        capture=function(text,'static VOID AdmissionGpuCompletionCaptureWindows(')
        self.assertIn('MissingTiming',capture,'Missing submit slots silently lose telemetry')
        pre=r'''
#include <assert.h>
#include <string.h>
#include "render_job_timing.h"
typedef void VOID;typedef unsigned ULONG;typedef unsigned char UCHAR;
typedef unsigned long long ULONGLONG;typedef int KIRQL;
typedef unsigned APPLE_AGX_BACKEND_COMPLETION_STATUS;
#define AppleAgxBackendCompletionSuccess 0
#define AppleAgxBackendJobCompute 2
#define ADMISSION_QUEUE_OBJECT_TA_TIMESTAMP_START 30
#define ADMISSION_QUEUE_OBJECT_TA_TIMESTAMP_END 31
#define RtlCopyMemory memcpy
#define KeAcquireSpinLock(l,i) ((void)(l),(void)(i))
#define KeReleaseSpinLock(l,i) ((void)(l),(void)(i))
typedef struct {unsigned Kind;ULONGLONG CommandVa;void *ComputeTimestampBlock;} APPLE_AGX_BACKEND_JOB_IMAGE;
typedef struct {void *Data;unsigned Size;} APPLE_AGX_EXP208_RELOCATION_OBJECT;
typedef struct {
 struct {APPLE_AGX_BACKEND_JOB_IMAGE PendingJob;} Backend;
 ADMISSION_JOB_TIMING_STATE JobTiming;ADMISSION_GPU_COMPLETION_RING GpuCompletions;
 int JobTimingLock;APPLE_AGX_EXP208_RELOCATION_OBJECT QueueObjects[32];
} ADMISSION_PLATFORM_RUNTIME;
'''
        code=pre+capture+r'''
int main(void){
 static ADMISSION_PLATFORM_RUNTIME r;
 ULONGLONG comp[2]={120,190},render[4]={140,190,110,170};
 ADMISSION_GPU_COMPLETION_QUERY q={0};
 unsigned i;
 r.Backend.PendingJob.Kind=2;r.Backend.PendingJob.CommandVa=99;
 r.Backend.PendingJob.ComputeTimestampBlock=comp;
 AdmissionJobTimingInitialize(&r.JobTiming,1000);
 AdmissionGpuCompletionCaptureWindows(&r,1,0);
 assert(r.GpuCompletions.MissingTiming==1);
 assert(AdmissionJobTimingBegin(&r.JobTiming,1,7,42,4096,100));
 assert(AdmissionJobTimingMark(&r.JobTiming,1,AdmissionJobPhaseComplete,200));
 AdmissionGpuCompletionCaptureWindows(&r,1,0);
 AdmissionGpuCompletionCaptureWindows(&r,1,0);
 AdmissionGpuCompletionRead(&r.GpuCompletions,7,&q);
 assert(q.Count==1 && q.MissingTiming==1 && q.Record[0].Start==120 && q.Record[0].CompleteQpc==200);
 assert(q.Record[0].SubmitQpc==100 && q.Record[0].End==190);
 r.Backend.PendingJob.Kind=1;
 for(i=0;i<4;i++){r.QueueObjects[28+i].Data=&render[i];r.QueueObjects[28+i].Size=8;}
 assert(AdmissionJobTimingBegin(&r.JobTiming,2,7,42,4096,100));
 assert(AdmissionJobTimingMark(&r.JobTiming,2,AdmissionJobPhaseComplete,201));
 AdmissionGpuCompletionCaptureWindows(&r,2,0);
 AdmissionGpuCompletionRead(&r.GpuCompletions,7,&q);
 assert(q.Count==1 && q.Record[0].Start==140 && q.Record[0].TaStart==110 && q.Record[0].TaEnd==170);
 assert(AdmissionJobTimingBegin(&r.JobTiming,3,7,42,4096,100));
 assert(AdmissionJobTimingMark(&r.JobTiming,3,AdmissionJobPhaseComplete,202));
 AdmissionGpuCompletionCaptureWindows(&r,3,1);
 AdmissionGpuCompletionRead(&r.GpuCompletions,7,&q);
 assert(q.Count==1 && q.Record[0].Status && !q.Record[0].Start && !q.Record[0].TaStart);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'test.c').write_text(code)
            z=subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror','-fsanitize=address,undefined','-I',str(INC),str(p/'test.c'),str(SRC),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)
            z=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(z.returncode,0,z.stderr)

if __name__=='__main__': unittest.main()
