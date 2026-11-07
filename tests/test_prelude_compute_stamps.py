"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2118 publishes adjacent Windows fences as compute stamps for RP/C. The
opt-in path must use distinct 0x100-unit queue events everywhere firmware sees
an endpoint, while cancellation, sidecar ownership and completion retain the
original fences. Exercise real builders, Windows barrier glue and queue runtime;
this is not a simulation of the proprietary firmware assertion.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest
from test_render_job_depth2_caps_rc98 import SHARED, SHARED_SOURCES, WORKER, RENDER

MODEL = r'''
static APPLE_AGX_RENDER_SHARED_MEMORY_OWNER owner;
static unsigned char objects[40][0x10000], saved[0x1420];
static unsigned read32(const void *p) { unsigned v; memcpy(&v,p,4); return v; }
static unsigned long long read64(const void *p) { unsigned long long v; memcpy(&v,p,8); return v; }
static void setup_owner(unsigned enabled) {
  memset(&owner,0,sizeof(owner)); memset(objects,0,sizeof(objects));
  owner.ObjectCount=40;owner.Initialized=owner.Built=owner.NotifierFlight=1;
  owner.ComputeQueueStamps=(APPLE_AGX_BOOL)enabled;
  for(unsigned n=0;n<40;++n) {
    owner.Objects[n].CpuAddress=objects[n];owner.Objects[n].Length=sizeof(objects[n]);
    owner.Objects[n].State=AppleAgxMemoryGpuMapped;
    owner.VirtualAddresses[n]=0xffffffa000000000ULL+(unsigned long long)n*0x10000;
    owner.Objects[n].GpuVirtualAddress=owner.VirtualAddresses[n];
  }
}
static APPLE_AGX_RENDER_COMPUTE_INPUT input(unsigned fence,unsigned timestamps) {
  APPLE_AGX_RENDER_COMPUTE_INPUT in={0};
  in.Counter=fence;in.StampValue=in.EventSequence=fence;in.ClientSequence=fence&255;
  in.EventNumber=2;in.VmSlot=1;in.CdmStreamBase=0x1160970000ULL;
  in.CdmStreamBytes=0x48;in.UscExecutionBase=0x1100000000ULL;
  in.PreemptionGpuAddress=0x11b3e40000ULL;in.Timestamps=timestamps;
  in.JobWordSet=AppleAgxRenderSharedMemoryNextJobWordSet(&owner,fence,1);
  return in;
}
static unsigned output_stamp(const APPLE_AGX_RENDER_COMPUTE_OUTPUT *out) {
  return out->StampValue;
}
static void check_words(const APPLE_AGX_RENDER_COMPUTE_INPUT *in,
                        const APPLE_AGX_RENDER_COMPUTE_OUTPUT *out,unsigned stamp) {
  const unsigned char *p=objects[39]+out->JobWordSet*0x2000;
  assert(read32(p+0x8298)==stamp);
  assert(read32(p+(in->Timestamps?0x9000:0x8400)+(in->Timestamps?0x218:0x1a0))==stamp);
  assert(read32(p+0x8004)==in->StampValue); /* original counter/sequence */
  assert(read32(p+0x82ac)==in->EventSequence);
  assert(output_stamp(out)==stamp);
  assert(owner.JobWordStamp==in->StampValue && owner.NotifierComputeStamp==in->StampValue);
}
static void pair(unsigned enabled,unsigned timestamps,unsigned seed,unsigned first,unsigned second) {
  TEST_FIXTURE f; APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION p,s;
  APPLE_AGX_RENDER_COMPUTE_INPUT a,b;
  APPLE_AGX_RENDER_COMPUTE_OUTPUT ao={0},bo={0};
  APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION c;
  ADMISSION_PLATFORM_RUNTIME rt={0};
  APPLE_AGX_G13_QUEUE_JOB_PLAN plan={0};
  APPLE_AGX_BACKEND_JOB_IMAGE head={0},next={0};
  unsigned char event[APPLE_AGX_G13_EVENT_MESSAGE_SIZE];
  setup_owner(enabled);TestInitialize(&f);TestEnableCompute(&f);
  owner.ComputeQueueStamp=seed;
  f.Runtime.Config.Ta.EventNumber=0;f.Runtime.Config.D3.EventNumber=1;
  f.Runtime.Config.Compute.EventNumber=2;
  f.Runtime.TaFirstRun=f.Runtime.D3FirstRun=f.Runtime.ComputeFirstRun=0;
  f.Runtime.BufferManagerInitialized=1;
  f.ComputeWrite=f.ComputeDone=0x25;f.TaWrite=f.TaDone=0x44f;f.D3Write=f.D3Done=0xb8;
  f.TaStamp=0x7a0a5c00;f.D3Stamp=0x3d0a5c00;f.ComputeStamp=enabled?seed:0x1820;
  owner.NotifierRenderCount=0x14b8;owner.NotifierComputeCount=0x20;
  owner.NotifierComputeStamp=owner.JobWordStamp=0x1820;owner.JobWordSet=1;
  *(unsigned *)objects[12]=0x14d8;
  assert(AppleAgxRenderFlightBegin(&owner,0x1829));
  owner.NotifierRenderCount+=2; /* the render's existing TA+3D notifications */
  a=input(0x1829,timestamps);assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&a,&ao));
  check_words(&a,&ao,first);
  head.Kind=AppleAgxBackendJobRender;head.TaExpectedStamp=0x7a0a5d00;
  head.D3ExpectedStamp=0x3d0a5d00;head.D3Event=1;head.ComputeEvent=2;
  head.ComputeExpectedStamp=output_stamp(&ao);plan.TaBarrierExpectedDonePointer=0x451;
  rt.Initdata.RenderSharedMemory=owner;
  assert(AdmissionBuildTaComputeBarrier(&rt,&a,&ao,&plan,&head));
  owner=rt.Initdata.RenderSharedMemory;
  assert(read32(objects[39]+0x880c)==first && read32(objects[39]+0x8810)==2);
  assert(read32(objects[39]+0x8814)==0x7a0a5d00);
  assert(AppleAgxRenderFlightPublish(&owner,0x1829));AppleAgxRenderFlightVisible(&owner,0x1829);
  memcpy(saved,objects[39]+0x8000,sizeof(saved));
  p=TestSubmission(&f,0x1829);p.Ta.GpuAddresses[0]=head.TaBarrierAddress;p.TaLeadingBarrier=1;
  p.Ta.ExpectedStamp=head.TaExpectedStamp;p.D3.ExpectedStamp=head.D3ExpectedStamp;
  p.Compute.Source=p.Compute.Destination=objects[39];p.Compute.Bytes=sizeof(objects[39]);
  p.Compute.GpuAddresses[0]=ao.WorkGpuAddress;p.Compute.GpuAddressCount=1;p.Compute.ExpectedStamp=first;
  assert(AppleAgxG13QueueRuntimeSubmitRenderWithCompute(&f.Runtime,&p)==AppleAgxG13QueueRuntimeResultOk);
  assert(AppleAgxRenderFlightBegin(&owner,0x182a));
  b=input(0x182a,timestamps);assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&b,&bo));
  check_words(&b,&bo,second);assert(ao.JobWordSet!=bo.JobWordSet);
  rt.Initdata.RenderSharedMemory=owner;rt.Backend.PendingJob=head;
  rt.Backend.Phase=AppleAgxBackendRuntimeSubmitted;
  rt.Depth2KickBarrier=ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D;
  assert(AdmissionBuildLeadBarrier(&rt,ADMISSION_JOB_WINDOW_QUEUE_COMPUTE,
                                  bo.JobWordSet,output_stamp(&bo),&next));
  owner=rt.Initdata.RenderSharedMemory;
  assert(read32(objects[39]+0xa82c)==0x3d0a5d00 && read32(objects[39]+0xa830)==1);
  assert(read32(objects[39]+0xa834)==second);
  assert(read64(objects[39]+0xa824)==AppleAgxRenderSharedMemoryFirmwareStampAddress(&owner,1));
  assert(memcmp(saved,objects[39]+0x8000,sizeof(saved))==0);
  assert(AppleAgxRenderFlightPublish(&owner,0x182a));AppleAgxRenderFlightVisible(&owner,0x182a);
  assert(read32(objects[12])==0x14dc && owner.JobWordStamp==0x182a);
  s=p;s.Fence=0x182a;s.Successor=1;s.ComputeOnly=1;s.TaLeadingBarrier=0;
  s.Compute.GpuAddresses[0]=next.LeadBarrierAddress;s.Compute.GpuAddresses[1]=bo.WorkGpuAddress;
  s.Compute.GpuAddressCount=2;s.Compute.ExpectedStamp=second;
  assert(AppleAgxG13QueueRuntimeSubmitCompute(&f.Runtime,&s)==AppleAgxG13QueueRuntimeResultOk);
  assert(f.ComputeRing[0x25]==ao.WorkGpuAddress && f.ComputeRing[0x26]==next.LeadBarrierAddress &&
         f.ComputeRing[0x27]==bo.WorkGpuAddress && f.ComputeWrite==0x28);
  assert(f.Runtime.PendingFence==0x1829 && f.Runtime.Successor.Fence==0x182a);
  TestEvent(event,0,1);
  assert(AppleAgxG13QueueRuntimeHandleEvent(&f.Runtime,event,sizeof(event))==AppleAgxG13QueueRuntimeResultOk);
  TestEvent(event,2,APPLE_AGX_G13_EVENT_COUNT);
  assert(AppleAgxG13QueueRuntimeHandleEvent(&f.Runtime,event,sizeof(event))==AppleAgxG13QueueRuntimeResultOk);
  assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&c));
  /* Real stamps without done pointers do not grant retirement. */
  f.TaStamp=p.Ta.ExpectedStamp;f.D3Stamp=p.D3.ExpectedStamp;f.ComputeStamp=first;
  assert(AppleAgxG13QueueRuntimeHandleEvent(&f.Runtime,event,sizeof(event))==AppleAgxG13QueueRuntimeResultOk);
  assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&c));
  f.TaDone=f.TaWrite;f.D3Done=f.D3Write;f.ComputeDone=0x26;
  TestEvent(event,0,1);event[4]|=4;
  assert(AppleAgxG13QueueRuntimeHandleEvent(&f.Runtime,event,sizeof(event))==AppleAgxG13QueueRuntimeResultOk);
  assert(AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&c) && c.Fence==0x1829);
  assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&c));
  f.ComputeStamp=second;f.ComputeDone=0x28;
  assert(AppleAgxG13QueueRuntimeHandleEvent(&f.Runtime,event,sizeof(event))==AppleAgxG13QueueRuntimeResultOk);
  assert(AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&c) && c.Fence==0x182a);
  assert(!AppleAgxG13QueueRuntimeTakeCompletion(&f.Runtime,&c));
}
static void rollback_and_wrap(void) {
  APPLE_AGX_RENDER_COMPUTE_INPUT in;
  APPLE_AGX_RENDER_COMPUTE_OUTPUT out={0};
  setup_owner(1);
  owner.ComputeQueueStamp=0xfffffe00u;
  assert(AppleAgxRenderFlightBegin(&owner,0xfffffffeu));
  in=input(0xfffffffeu,0);assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&in,&out));
  check_words(&in,&out,0xffffff00u);
  assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&in,&out));
  check_words(&in,&out,0xffffff00u);assert(owner.NotifierComputeCount==1);
  assert(AppleAgxRenderFlightPublish(&owner,0xfffffffeu));
  assert(AppleAgxRenderFlightCancel(&owner,0xfffffffeu));
  assert(owner.ComputeQueueStamp==0xfffffe00u && !owner.JobWordStamp && !owner.NotifierComputeCount);
  assert(AppleAgxRenderFlightBegin(&owner,0xfffffffeu));
  assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&in,&out));check_words(&in,&out,0xffffff00u);
  assert(AppleAgxRenderFlightPublish(&owner,0xfffffffeu));AppleAgxRenderFlightVisible(&owner,0xfffffffeu);
  assert(!AppleAgxRenderFlightCancel(&owner,0xfffffffeu));
  assert(AppleAgxRenderFlightBegin(&owner,1));in=input(1,1);
  assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&in,&out));check_words(&in,&out,0x100);
  assert(AppleAgxRenderFlightCancel(&owner,1));assert(owner.ComputeQueueStamp==0xffffff00u);
  AppleAgxRenderFlightRetire(&owner,0xfffffffeu);
  for(unsigned i=0;i<40;++i) { owner.Objects[i].State=AppleAgxMemoryPrepared;owner.Objects[i].GpuVirtualAddress=0; }
  assert(AppleAgxRenderSharedMemoryInitializeComputeQueue(&owner));
  for(unsigned i=0;i<40;++i) { owner.Objects[i].State=AppleAgxMemoryGpuMapped;owner.Objects[i].GpuVirtualAddress=owner.VirtualAddresses[i]; }
  assert(!owner.ComputeQueueStamp);
  /* A new queue lifetime may reuse the old raw fence; stamp zero is not
   * an already-built event. Large raw fences must also avoid truncation. */
  const unsigned restart_fences[]={0xfffffffeu,0x1000000u};
  for(unsigned i=0;i<2;++i) {
    assert(AppleAgxRenderFlightBegin(&owner,restart_fences[i]));in=input(restart_fences[i],0);
    assert(AppleAgxRenderSharedMemoryBuildCompute(&owner,&in,&out));check_words(&in,&out,0x100);
    assert(AppleAgxRenderFlightCancel(&owner,restart_fences[i]));
  }
}
int main(void) {
  pair(0,0,0,0x1829,0x182a);pair(0,1,0,0x1829,0x182a);
  pair(1,0,0x2000,0x2100,0x2200);pair(1,1,0x2000,0x2100,0x2200);
  pair(1,0,0xfffffe00u,0xffffff00u,0x100);pair(1,1,0xfffffe00u,0xffffff00u,0x100);
  rollback_and_wrap();
  puts("Q23_STAMPS_OK");return 0;
}
'''

class PreludeComputeStamps(unittest.TestCase):
    def test_windows_consumers_use_emitted_stamp_and_effective_opt_in(self):
        """Compile the actual three endpoint assignments, two leading-barrier
        arguments and startup assignment; any stale input fence must fail."""
        worker=WORKER.read_text()
        endpoints=re.findall(r'Job->ComputeExpectedStamp\s*=\s*([^;]+);',worker)
        leads=re.findall(r'output.JobWordSet, ([^,]+), Job\)\)',worker)
        self.assertEqual(len(endpoints),3)
        self.assertEqual(len(leads),2)
        gate=re.search(r'runtime->Initdata.RenderSharedMemory.ComputeQueueStamps\s*=.*?;',worker,re.S)[0]
        checks='\n'.join('Job->ComputeExpectedStamp='+e+'; assert(Job->ComputeExpectedStamp==0x2100);' for e in endpoints)
        checks+='\n'+'\n'.join('assert(('+e+')==0x2100);' for e in leads)
        source=r'''
#include <assert.h>
#include "apple_agx_render_shared_memory.h"
int main(void) {
 APPLE_AGX_BACKEND_JOB_IMAGE job={0}, *Job=&job;
 APPLE_AGX_RENDER_COMPUTE_INPUT input={0};
 APPLE_AGX_RENDER_COMPUTE_OUTPUT output={0};
 struct {unsigned char Depth2Prelude; struct {APPLE_AGX_RENDER_SHARED_MEMORY_OWNER RenderSharedMemory;} Initdata;}
 storage={0}, *runtime=&storage;
 input.StampValue=0x1829;output.StampValue=0x2100;(void)input;
'''+checks+'\n'+gate+r'''
 assert(!runtime->Initdata.RenderSharedMemory.ComputeQueueStamps);
 runtime->Depth2Prelude=1;
'''+gate+r'''
 assert(runtime->Initdata.RenderSharedMemory.ComputeQueueStamps);
 return 0;
}
'''
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'test.c').write_text(source)
            run=subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                '-Wconversion','-ftrivial-auto-var-init=pattern','-I',str(SHARED/'include'),
                str(p/'test.c'),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stderr)
            run=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)

    def test_real_builder_barriers_and_queue_runtime(self):
        worker=WORKER.read_text()
        functions='\n'.join(re.search(r'^static APPLE_AGX_BACKEND_BOOL '+name+r'\(.*?^}',worker,re.M|re.S)[0]
                            for name in ('AdmissionBuildLeadBarrier','AdmissionBuildTaComputeBarrier'))
        shims=r'''
#include <stdlib.h>
#include "apple_agx_render_shared_memory.h"
#include "apple_agx_g13_queue_provider.h"
#include "render_job_window.h"
#define RtlZeroMemory(p,n) memset(p,0,n)
typedef unsigned long ULONG;
typedef struct {
 struct {APPLE_AGX_RENDER_SHARED_MEMORY_OWNER RenderSharedMemory;} Initdata;
 struct {APPLE_AGX_BACKEND_JOB_IMAGE PendingJob; APPLE_AGX_BACKEND_RUNTIME_PHASE Phase;} Backend;
 ULONG Depth2KickBarrier; unsigned RcReceiptEnabled;
 struct {unsigned CFence,WaitLow,WaitHigh,WaitValue;} RcCurrent;
} ADMISSION_PLATFORM_RUNTIME;
'''
        runtime=(SHARED/'tests/apple_agx_g13_queue_runtime_test.c').read_text()
        runtime=runtime.replace('#define assert(x) do { if (!(x)) ++QueueRuntimeFailures; } while (0)',
            '#define assert(x) do { if (!(x)) { fprintf(stderr,"line %d: %s\\n",__LINE__,#x); exit(1); } } while (0)')
        source=shims+runtime+functions+MODEL
        names=SHARED_SOURCES+['apple_agx_g13_queue_runtime.c','apple_agx_g13_codec.c',
              'apple_agx_backend_runtime.c','apple_agx_firmware.c','apple_agx_rtkit.c']
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'test.c').write_text(source)
            run=subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-ftrivial-auto-var-init=pattern',
                '-I',str(SHARED/'include'),'-I',str(RENDER/'include'),str(p/'test.c'),
                *[str(SHARED/'src'/n) for n in names],str(RENDER/'src/render_job_window.c'),
                '-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            run=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
            self.assertIn('Q23_STAMPS_OK',run.stdout)
