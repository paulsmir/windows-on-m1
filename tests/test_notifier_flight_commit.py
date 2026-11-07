"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Every work/ring flush, notifier flush, pointer and doorbell failure must either
restore an unpublished notifier or retain a visible flight, including a successor
behind an existing flight. This executes the production queue state machine and
flight transaction with failing transport operations, not a queue model.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_render_job_depth2_caps_rc98 import SHARED, SHARED_SOURCES

EXTRA = r'''
static unsigned threshold;
static APPLE_AGX_RENDER_SHARED_MEMORY_OWNER owner;
static APPLE_AGX_BOOL nf_flush(void *ctx,const void *address,APPLE_AGX_U32 bytes) {
  TEST_IO *io=ctx;
  assert(address==&threshold && bytes==4);
  return ++io->Step==io->FailAt ? 0 : 1;
}
static APPLE_AGX_BACKEND_BOOL nf_prepare(void *ctx,APPLE_AGX_BACKEND_U32 fence) {
  TEST_IO *io=ctx;
  assert(io->PointerWrites==0); /* last prep, before first pointer */
  assert(io->WorkFlushes && io->RingFlushes);
  return AppleAgxRenderFlightPublish(&owner,fence);
}
static APPLE_AGX_BACKEND_BOOL nf_finish(void *ctx,APPLE_AGX_BACKEND_U32 fence,
                                      APPLE_AGX_BACKEND_BOOL visible) {
  TEST_IO *io=ctx;
  assert(visible==io->WasVisible);
  if(visible) { AppleAgxRenderFlightVisible(&owner,fence); return 1; }
  return AppleAgxRenderFlightCancel(&owner,fence);
}
static void owner_init(TEST_IO *io) {
  memset(&owner,0,sizeof(owner)); threshold=3;
  owner.NotifierFlight=1; owner.ObjectCount=40;
  owner.Objects[12].CpuAddress=&threshold; owner.Objects[12].Length=4;
  owner.Objects[12].State=AppleAgxMemoryGpuMapped;
  owner.VirtualAddresses[12]=0x1500000000ULL;
  owner.Objects[12].GpuVirtualAddress=owner.VirtualAddresses[12];
  owner.NotifierComputeCount=3;
  owner.NotifierFlush=nf_flush;owner.NotifierFlushContext=io;
  assert(AppleAgxRenderFlightBegin(&owner,60));
  owner.NotifierComputeCount=4; owner.Flights[0].EventCount=4;
}
static unsigned trial(unsigned kind,unsigned successor,unsigned fail) {
  TEST_FIXTURE f; APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION s;
  APPLE_AGX_G13_QUEUE_RUNTIME_RESULT result;
  TestInitialize(&f); TestEnableCompute(&f);
  if(successor) {
    s=TestSubmission(&f,59);
    assert(AppleAgxG13QueueRuntimeSubmit(&f.Runtime,&s)==AppleAgxG13QueueRuntimeResultOk);
  }
  memset(&f.IoState,0,sizeof(f.IoState));
  f.IoState.FailAt=fail;f.IoState.CheckNotifier=1;
  s=TestSubmission(&f,60);
  if(kind) {
    s.Compute.Source=f.ComputeSource;s.Compute.Destination=f.ComputeDestination;
    s.Compute.Bytes=64;s.Compute.GpuAddresses[0]=0x1500300000ULL;
    s.Compute.GpuAddressCount=1;s.Compute.ExpectedStamp=0x600;
    if(kind==1) s.ComputeOnly=1;
    else { s.Ta.GpuAddresses[2]=0x1500400000ULL;
      s.Ta.GpuAddressCount=3;s.TaLeadingBarrier=1; }
  }
  if(successor) {
    s.Successor=1; s.Ta.GpuAddressCount=2;
    if(kind) { s.Compute.GpuAddressCount=2; s.Compute.GpuAddresses[1]=0x1500301000ULL; }
    else { s.Ta.GpuAddresses[0]=0x1500301000ULL; s.TaLeadingBarrier=0; }
  }
  owner_init(&f.IoState);
  f.Runtime.Io.PreparePublish=nf_prepare;f.Runtime.Io.FinishPublish=nf_finish;
  result=kind==1 ? AppleAgxG13QueueRuntimeSubmitCompute(&f.Runtime,&s) :
         kind==2 ? AppleAgxG13QueueRuntimeSubmitRenderWithCompute(&f.Runtime,&s) :
                   AppleAgxG13QueueRuntimeSubmit(&f.Runtime,&s);
  if(fail) assert(result!=AppleAgxG13QueueRuntimeResultOk);
  else assert(result==AppleAgxG13QueueRuntimeResultOk);
  if(f.IoState.WasVisible) {
    assert(threshold==4 && owner.Flights[0].Visible && owner.Flights[0].Fence==60);
    assert(!AppleAgxRenderFlightCancel(&owner,60));
    /* An uncertain reset cannot grant reuse; only a stop proof does. */
    f.IoState.FailQuiesce=1;
    assert(owner.Flights[0].Fence==60);
    AppleAgxRenderFlightStopped(&owner);
  } else {
    assert(threshold==3 && owner.NotifierComputeCount==3 && !owner.BuildingFlight);
    assert(!owner.Flights[0].Fence);
    if(successor) assert(f.Runtime.PendingFence==59);
  }
  return f.IoState.Step;
}
int main(void) {
  unsigned k,s,i,n;
  for(k=0;k<3;++k) for(s=0;s<2;++s) {
    n=trial(k,s,0);
    for(i=1;i<=n;++i) trial(k,s,i);
  }
  if(QueueRuntimeFailures) fprintf(stderr,"failure count: %u\n",QueueRuntimeFailures);
  return QueueRuntimeFailures ? 1 : 0;
}
'''

class NotifierCommit(unittest.TestCase):
    def test_each_commit_stage(self):
        source=(SHARED/'tests/apple_agx_g13_queue_runtime_test.c').read_text()
        source=source.replace('if (!(x)) ++QueueRuntimeFailures;', 'if (!(x)) { ++QueueRuntimeFailures; fprintf(stderr, "line %d: %s\\n", __LINE__, #x); }')
        source='#include "apple_agx_render_shared_memory.h"\n'+source
        source=source.replace('  unsigned int Sequence;', '  unsigned int Step,FailAt,WasVisible,CheckNotifier;\n  unsigned int Sequence;')
        source=source.replace('  ++io->Sequence;', '  if(++io->Step==io->FailAt) return APPLE_AGX_BACKEND_FALSE;\n  ++io->Sequence;',1)
        source=source.replace('  ++io->PointerWrites;', '''  if(++io->Step==io->FailAt) return APPLE_AGX_BACKEND_FALSE;
  if(io->CheckNotifier) { assert(threshold==4);io->WasVisible=1; }
  ++io->PointerWrites;''')
        source=source.replace('  ++io->Sends;', '  if(++io->Step==io->FailAt) return APPLE_AGX_BACKEND_FALSE;\n  ++io->Sends;')
        source='static unsigned threshold;\n'+source+EXTRA.replace('static unsigned threshold;','')
        with tempfile.TemporaryDirectory() as d:
            c=Path(d)/'test.c';c.write_text(source);binary=Path(d)/'test'
            names=SHARED_SOURCES+['apple_agx_g13_queue_runtime.c','apple_agx_g13_codec.c',
                'apple_agx_backend_runtime.c','apple_agx_firmware.c','apple_agx_rtkit.c']
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',
                '-fsanitize=address,undefined','-ftrivial-auto-var-init=pattern',
                '-I',str(SHARED/'include'),str(c),*[str(SHARED/'src'/n) for n in names],
                '-o',str(binary)],check=True)
            subprocess.run([str(binary)],check=True)
