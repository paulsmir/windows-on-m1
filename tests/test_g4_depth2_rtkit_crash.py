"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2100: a fatal RTKit notification is distinct from a recoverable queue Timeout.
The real RTKit session must latch it while both published fences remain owned;
the actual KMD reset capture must export the raw session and typed pair. A
receipt that contains only PollFailure cannot disambiguate the mailbox protocol.
This is diagnostic/safety coverage, NOT an assertion that crashed firmware can
restart. Real platform/provider/queue/backend/scheduler run; firmware, MMIO and
WDK services are modeled. Reset DDI retention is covered by the reset replay.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest

from test_g4_rr_event_drain import model_source
import test_g4_post_resume_no_sgx_mmio as capture
from test_render_job_window import RC102_SOURCES, SHARED, INCLUDE

CASES = r'''
static unsigned consumed;
static APPLE_AGX_ASC_BOOL crash_read32(void *ctx, unsigned off, unsigned *v) {
  (void)ctx;
  assert(off==J313_AGX_G2_ASC_OUTBOX_CTRL_OFFSET || off==J313_AGX_G2_ASC_INBOX_CTRL_OFFSET);
  *v=consumed ? APPLE_AGX_ASC_MAILBOX_EMPTY : 0;
  return 1;
}
static APPLE_AGX_ASC_BOOL crash_read64(void *ctx, unsigned off, unsigned long long *v) {
  (void)ctx;
  if(off==J313_AGX_G2_ASC_OUTBOX0_OFFSET) *v=1ULL<<52;
  else {assert(off==J313_AGX_G2_ASC_OUTBOX1_OFFSET);*v=1;++consumed;}
  return 1;
}
static void crash_pair(unsigned head, unsigned next, unsigned paired) {
  world *w=&W; start(w,"q13-crash");
  w->caps |= ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER;
  add(w,K_R,OUT_OK,7);run(w,20);
  add(w,head,OUT_OK,7);add(w,next,OUT_OK,7);
  vidsch(w);dispatch(w);
  if(paired) {kick_successor(w);++w->now;kick_successor(w);}
  assert(!w->violations && w->win.Count==1+paired && w->nr==1);
  APPLE_AGX_G13_QUEUE_RUNTIME *q=&w->platform.QueueProvider.Runtime;
  assert(q->PendingFence==2 && q->Successor.Fence==(paired?3u:0u));
  ADMISSION_PLATFORM_RUNTIME r={0};
  APPLE_AGX_ASC_IO io={0};
  ADMISSION_GPU_POST_RESUME_RECEIPT receipt;
  AppleAgxRtkitSessionInitialize(&r.Rtkit);
  r.Rtkit.Running=r.Rtkit.CpuReady=r.Rtkit.CrashlogReplySent=1;
  r.Rtkit.Boot.Phase=AppleAgxRtkitBootReady;
  io.Read32=crash_read32;io.Read64=crash_read64;consumed=0;
  unsigned received=0;
  for(unsigned attempt=0;attempt<3;++attempt) {
    assert(AppleAgxRtkitSessionDrainRuntime(&r.Rtkit,&io,64,&received)==
           AppleAgxRtkitSessionResultFirmwareCrashed);
    assert(received==(attempt?0u:1u) && consumed==1);
    /* Real timeout accounting cannot conjure retirement for an ASC crash. */
    assert(AppleAgxG13QueueRuntimeCheckTimeout(q,10000+attempt)==0);
    assert(q->PendingFence==2 && q->Successor.Fence==(paired?3u:0u));
    assert(!q->CompletionAvailable && !q->RecoveryPending);
    r.Provider.QueueProvider.Runtime=*q;
    r.TransportIo.ReadU32=read_u32; /* shared words, no MMIO */
    memset(&receipt,0xa5,sizeof(receipt));
    AdmissionCapturePostResume(&r,2,0,&receipt);
    assert(receipt.Version==4 && receipt.Bytes==312);
    /* Q15: wire suffix has head then successor, each TA/3D/compute
     * expected stamps followed by their event slots. No synthetic drill. */
    const unsigned *stamps=(const unsigned *)((const unsigned char *)&receipt+264);
    const APPLE_AGX_G13_QUEUE_PENDING *pending[]={&q->TaPending,&q->D3Pending,
      &q->ComputePending,&q->Successor.TaPending,&q->Successor.D3Pending,&q->Successor.ComputePending};
    for(unsigned j=0;j<6;++j) {
      unsigned kind=j<3?head:(paired?next:0);
      unsigned active=kind && (j%3==2 ? kind!=K_R : kind!=K_C);
      assert(stamps[j]==(active?pending[j]->ExpectedStamp:0));
      assert(stamps[6+j]==(active?pending[j]->EventNumber:0));
    }
    assert(receipt.Rtkit.Captured && receipt.Rtkit.Crashed &&
           receipt.Rtkit.LastEndpoint==1 && receipt.Rtkit.LastPayload==(1ULL<<52));
    assert(receipt.Rtkit.ReceivedCount==1 && receipt.Rtkit.MailboxValid==3);
    assert(receipt.Rtkit.HeadFence==2 && receipt.Rtkit.HeadKind==head);
    assert(receipt.Rtkit.NextFence==(paired?3u:0u) && receipt.Rtkit.NextKind==(paired?next:0u));
    assert(w->win.Count==1+paired && w->nr==1 && !w->violations);
  }
  /* Capture an empty healthy session into dirty storage: no stale pair/crash. */
  memset(&r,0,sizeof(r));
  AdmissionCapturePostResume(&r,0,0,&receipt);
  ADMISSION_GPU_RTKIT_SNAPSHOT expected={0};expected.Captured=1;
  assert(!memcmp(&receipt.Rtkit,&expected,sizeof(expected)));
  const unsigned *empty=(const unsigned *)((const unsigned char *)&receipt+264);
  for(unsigned j=0;j<12;++j) assert(!empty[j]);
}
int main(void) {
  for(unsigned head=K_R;head<=K_C;++head)
    for(unsigned next=K_R;next<=K_C;++next)
      for(unsigned pair=0;pair<2;++pair)crash_pair(head,next,pair);
  puts("Q13_RTKIT_CRASH_PAIR_OK");
}
'''

class Depth2RtkitCrash(unittest.TestCase):
    def test_real_runtime_crash_retains_pair_and_exports_cause(self):
        backend = capture.BACKEND.read_text()
        header = (capture.RENDER/'include/render_admission.h').read_text()
        definitions = ''.join(capture.define(backend, name) for name in (
            'ADMISSION_REGIONC_FAULT_INFO_OFFSET', 'ADMISSION_REGIONC_PENDING_STAMPS_OFFSET',
            'ADMISSION_REGIONC_FAULT_CONTROL_OFFSET'))
        definitions += capture.define(header, 'ADMISSION_REGIONC_PENDING_STAMPS_BYTES')
        source = (model_source() + capture.SHIM + definitions +
                  capture.function_body(backend, 'AdmissionCapturePostResume') + CASES)
        sources = list(RC102_SOURCES.values()) + [SHARED/'src'/f'apple_agx_{n}.c'
            for n in ('rtkit_session', 'rtkit_boot', 'asc_transport', 'gfx_handoff')]
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'q13.c').write_text(source)
            subprocess.run([os.environ.get('CC','clang'), '-std=c11','-Wall','-Wextra',
                '-Werror','-Wno-unused-function','-ftrivial-auto-var-init=pattern',
                '-fsanitize=address,undefined','-I',str(SHARED/'include'),'-I',str(INCLUDE),
                str(p/'q13.c'),*map(str,sources),'-o',str(p/'q13')],check=True)
            result=subprocess.run([str(p/'q13')],capture_output=True,text=True,timeout=20)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('Q13_RTKIT_CRASH_PAIR_OK',result.stdout)

    def test_real_reset_ddis_retain_both_published_fences(self):
        import test_apple_agx_tdr_reset_quiesce as reset
        cases = reset.CASES.split('int main(void) {')[0] + r'''
int main(void) {
  DXGKARG_RESETENGINE arg={0};
  setup(0,3);
  assert(AppleAgxSchedulerKickQueuedFence(&adapter.Scheduler,0,0,0xce71du));
  adapter.G4Pending[adapter.G4PendingHead].State=AdmissionG4PendingKicked;
  scene_first.Started=1;
  runtime.Depth2KickedFence=0xce71du;
  runtime.Backend.PendingSubmission.Submission.Fence=0xce71cu;
  runtime.Backend.SuccessorPending=1;
  runtime.Backend.SuccessorSubmission.Submission.Fence=0xce71du;
  ReplayQueueSetup(0xce71cu);
  runtime.Provider.QueueProvider.Runtime=*ReplayQueueRuntime();
  runtime.Provider.QueueProvider.Phase=AppleAgxG13QueueProviderSubmitted;
  runtime.Provider.QueueProvider.PendingFence=0xce71cu;
  runtime.Provider.QueueProvider.SuccessorFence=0xce71du;
  mailbox_mode=3;
  for(unsigned retry=0;retry<3;++retry) {
    assert((retry<2 ? AdmissionDdiResetEngine(&adapter,&arg) :
                     AdmissionDdiResetFromTimeout(&adapter))==STATUS_DEVICE_HARDWARE_ERROR);
    assert(last_recorded.Stage==AdmissionGpuResetStageQuiesce &&
           last_recorded.Quiesce==AdmissionGpuResetQuiescePoll && !last_recorded.WaitMs);
    assert(last_post_resume.PollFailure==0x01000008u);
    assert(runtime.Rtkit.CrashlogCrashed && runtime.Rtkit.ReceivedCount==1);
    assert(adapter.Scheduler.ActiveFence==0xce71cu &&
           AppleAgxSchedulerKickedFence(&adapter.Scheduler)==0xce71du);
    assert(owner.Object.FenceOutstanding==0xce71cu &&
           other.Object.FenceOutstanding==0xce71du && scene_first.Started);
    assert(runtime.Provider.QueueProvider.Runtime.PendingFence==0xce71cu &&
           runtime.Provider.QueueProvider.Runtime.Successor.Fence==0xce71du);
    assert(!arg.LastAbortedFenceId && !private_resets && !polls && !rtkit_stops);
    assert(AdmissionDdiRestartFromTimeout(&adapter)==STATUS_DEVICE_NOT_READY);
  }
  puts("Q13_RESET_RETAINS_BOTH_FENCES_OK");
  return 0;
}
'''
        firmware = r'''
#include "apple_agx_g13_queue_runtime_test.c"
#undef assert
#include <assert.h>
static TEST_FIXTURE f;
APPLE_AGX_G13_QUEUE_RUNTIME *ReplayQueueRuntime(void) { return &f.Runtime; }
void ReplayQueueSetup(unsigned fence) {
  TestInitialize(&f);TestEnableCompute(&f);
  APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION s=TestSubmission(&f,fence);
  s.Compute.Source=f.ComputeSource;s.Compute.Destination=f.ComputeDestination;
  s.Compute.Bytes=sizeof(f.ComputeSource);s.Compute.GpuAddresses[0]=0x1500300000ULL;
  s.Compute.GpuAddressCount=1;s.Compute.ExpectedStamp=fence;
  s.Ta.GpuAddresses[2]=s.Ta.GpuAddresses[1];
  s.Ta.GpuAddresses[1]=s.Ta.GpuAddresses[0];
  s.Ta.GpuAddresses[0]=0x1500308800ULL;s.Ta.GpuAddressCount=3;s.TaLeadingBarrier=1;
  assert(AppleAgxG13QueueRuntimeSubmitRenderWithCompute(&f.Runtime,&s)==0);
  s.Fence=fence+1;s.Successor=1;s.ComputeOnly=1;s.TaLeadingBarrier=0;
  s.Compute.ExpectedStamp=fence+1;s.Compute.GpuAddressCount=2;
  s.Compute.GpuAddresses[0]=0x150030a820ULL;s.Compute.GpuAddresses[1]=0x150030a000ULL;
  assert(AppleAgxG13QueueRuntimeSubmitCompute(&f.Runtime,&s)==0);
  assert(f.Runtime.ComputeInRender && f.Runtime.Successor.ComputeTerminal);
  assert(f.ComputeWrite==3 && !f.ComputeDone && !f.D3Done && !f.TaDone);
}
int ReplayQueuePoll(int poll) {(void)poll;assert(0);return 0;}
'''
        reset.TdrResetQuiesce().run_replay(cases, firmware)
