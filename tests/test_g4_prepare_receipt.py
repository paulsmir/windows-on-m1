"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A Prepare rejection must preserve the exact rejecting path and lock-held
predicates before rollback/another worker erases them. Execute both real
render and compute submit bodies and the real scheduler, with deterministic
interleavings at the scheduler-lock boundary. This diagnoses, not fixes,
EXP2092; the OS/runtime and packet-prepare boundary are host substitutes.
"""
from pathlib import Path
import unittest
import test_g4_depth2_packet_cap as cap
from test_g4_depth2_packet_cap import ROOT, RA, SHARED


class PrepareReceipt(unittest.TestCase):
    def test_render_and_compute_prepare_rejections(self):
        shim = (ROOT / 'tests/g4_submit_virtual_replay.c').read_text()
        prefix, main = shim.split('int main(void) {', 1)
        # Test the production receipt layout, not the shim's mirror.
        header = (RA / 'include/render_admission.h').read_text()
        receipt = '  struct _ADMISSION_G4_SUBMIT_FAILURE {' + header.split(
            'struct _ADMISSION_G4_SUBMIT_FAILURE {', 1)[1].split('} G4SubmitFailure;', 1)[0] + '} G4SubmitFailure;'
        start = prefix.index('  struct _ADMISSION_G4_SUBMIT_FAILURE {')
        end = prefix.index('} G4SubmitFailure;', start) + len('} G4SubmitFailure;')
        prefix = prefix[:start] + receipt + prefix[end:]
        prefix = prefix.replace('static int KeGetCurrentIrql(void)', r'''
static ADMISSION_CONTEXT *race_adapter;
static ADMISSION_RENDER_CONTEXT *race_context;
static unsigned lock_calls, race_at, race_kind;
static int packet_prepare_ok=1;
static void race(void) {
  if (++lock_calls != race_at) return;
  switch(race_kind) {
  case 1: race_context->Object.FenceOutstanding=5; race_context->GpuvaG3SecondFence=6; break;
  case 2: race_adapter->G4PendingCount=ADMISSION_G4_PENDING_CAPACITY; break;
  case 3: race_adapter->G4Pending[0].State=AdmissionG4PendingFilling; break;
  case 4: race_adapter->SchedulerFaulted=1; break;
  case 5: race_adapter->Scheduler.PreemptionPhase=AppleAgxPreemptionWaitCurrentBoundary; break;
  case 6: race_adapter->Scheduler.QueueCount=APPLE_AGX_SCHEDULER_QUEUE_CAPACITY; break;
  case 7: race_adapter->Scheduler.LastSubmittedFence=7; break;
  case 8: race_adapter->G4Pending[0].State=AdmissionG4PendingDropped; break;
  case 9: race_adapter->RenderPacket.State=AdmissionRenderPacketActive; break;
  case 10: packet_prepare_ok=0; break;
  case 11: race_adapter->Scheduler.PreemptionPhase=AppleAgxPreemptionReadyToNotify; break;
  }
}
static int KeGetCurrentIrql(void)''')
        prefix = prefix.replace('assert(!*lock);*lock=1;*irql=0;',
                                'assert(!*lock);*lock=1;*irql=0;race();')
        prefix = prefix.replace('(void)irql;assert(*lock);*lock=0;', r'''
  (void)irql;assert(*lock);
  /* The receipt must survive state changing immediately after unlock. */
  if (race_at && lock_calls==race_at) race_adapter->SchedulerFaulted=0;
  *lock=0;''')
        prefix = prefix.replace('packet->Description=*description;packet->State=AdmissionRenderPacketPrepared;',
            'if(!packet_prepare_ok)return 0;\n  packet->Description=*description;packet->State=AdmissionRenderPacketPrepared;')
        prefix = prefix.replace('#include "g4_submit_virtual_functions.inc"', r'''static int AdmissionRenderPacketActivate(ADMISSION_RENDER_PACKET *p, unsigned f) {
  if(p->State!=AdmissionRenderPacketQueued || p->Description.Fence!=f) return 0;
  p->State=AdmissionRenderPacketActive; return 1;
}
#include "g4_submit_virtual_functions.inc"
''')
        setup = main.split('  assert(AdmissionG4SubmitVirtualEnvelope', 1)[0]
        code = prefix + '\nint main(void) {\n' + setup + r'''
  _Static_assert(offsetof(struct _ADMISSION_G4_SUBMIT_FAILURE, PrepareMask)==392, "v3 prefix");
  _Static_assert(sizeof(struct _ADMISSION_G4_SUBMIT_FAILURE)==456, "v4 size");
  APPLE_AGX_G4_COMPUTE_ENVELOPE e={0};
  APPLE_AGX_G4_PROCESS_RANGE preempt={0x60000ULL,0x10000u,0u};
  assert(AppleAgxG4ComposeHeaderCompute(&e.Header,0x20000ULL,sizeof(e.Native)+sizeof(e.Compute),&preempt));
  e.Native.Type=APPLE_AGX_G4_COMPUTE;e.Native.Size=sizeof(e.Compute);
  e.Compute.CdmCtrlStreamBase=0x50000ULL;e.Compute.CdmCtrlStreamEnd=0x50040ULL;
  struct {unsigned kind, at, deferred, site, mask;} cases[]={
    {1,2,1,2,0x9d}, {2,2,1,2,0xa5}, {3,2,1,2,0xc5}, {4,2,1,2,0x185},
    {5,3,1,3,0x86cd}, {6,3,1,3,0x46cd}, {7,3,1,3,0x206cd},
    {8,3,1,3,0x8cd}, {11,3,1,3,0x86cd},
    {1,1,0,4,0x1a}, {2,1,0,4,0x22}, {9,1,0,4,0x82},
    {4,1,0,4,0x102}, {10,1,0,4,0x2002}
  };
  for(unsigned compute=0;compute<2;++compute) {
    args.Flags.Value=0;
    args.pDmaBufferPrivateData=compute?(const void *)&e:(const void *)&packet;
    args.DmaBufferPrivateDataSize=args.DmaBufferUmdPrivateDataSize=compute?sizeof(e):sizeof(packet);
    args.DmaBufferSize=compute?sizeof(e.Native)+sizeof(e.Compute):packet.Header.Base.CommandBytes;
    for(unsigned n=0;n<sizeof(cases)/sizeof(cases[0]);++n) {
      memset(&adapter.G4SubmitFailure,0xa5,sizeof(adapter.G4SubmitFailure));
      adapter.G4SubmitFailureClaim=adapter.G4SubmitFailureCount=0;
      adapter.G4PendingHead=adapter.G4PendingCount=0;
      memset(adapter.G4Pending,0,sizeof(adapter.G4Pending));
      adapter.RenderPacket.State=cases[n].deferred?AdmissionRenderPacketActive:AdmissionRenderPacketEmpty;
      adapter.SchedulerFaulted=0; adapter.RuntimeReady=1;adapter.RuntimeQueueable=cases[n].deferred;
      context.Object.FenceOutstanding=context.GpuvaG3SecondFence=0;
      AppleAgxSchedulerInitialize(&adapter.Scheduler);
      race_adapter=&adapter;race_context=&context;lock_calls=0;
      race_at=cases[n].at;race_kind=cases[n].kind;packet_prepare_ok=1;
      int old_dispatches=dispatches;
      assert(AdmissionG4SubmitVirtualEnvelope(&adapter,&context,&args)==STATUS_INVALID_PARAMETER);
      struct _ADMISSION_G4_SUBMIT_FAILURE *r=&adapter.G4SubmitFailure;
      if(r->PrepareMask!=cases[n].mask) fprintf(stderr,"compute=%u case=%u mask=%x expected=%x site=%u\n",compute,n,r->PrepareMask,cases[n].mask,r->PrepareSite);
      assert(r->Version==4 && r->Bytes==456 && r->Branch==10 && r->Status==(unsigned)STATUS_INVALID_PARAMETER);
      assert(r->PrepareSite==cases[n].site && r->PrepareMask==cases[n].mask);
      assert(r->PrepareFence==7 && r->PreparePendingCapacity==2 && r->PrepareSchedulerQueueCapacity==64);
      assert(!r->Subsite && !r->TrackFound && !r->Va && !r->DownstreamStatus);
      assert(dispatches==old_dispatches && adapter.G4SubmitFailureClaim==2);
      if(cases[n].kind==4) assert(r->PrepareSchedulerFaulted==1 && !adapter.SchedulerFaulted);
      if(cases[n].site==3) {
        assert(r->PrepareFenceOutstanding==7 && !context.Object.FenceOutstanding);
        assert(r->PrepareTailState==(cases[n].kind==8?AdmissionG4PendingDropped:AdmissionG4PendingFilling));
        assert(adapter.G4Pending[0].State==AdmissionG4PendingFree && !adapter.G4PendingCount);
      }
      if(cases[n].kind==1) assert(r->PrepareFenceOutstanding==5 && r->PrepareSecondFence==6 && context.Object.FenceOutstanding==5 && context.GpuvaG3SecondFence==6);
      /* A later failure increments the count without overwriting the snapshot. */
      struct _ADMISSION_G4_SUBMIT_FAILURE first=*r;
      AdmissionG4SubmitReject(&adapter,&context,&args,AdmissionG4RejectOuter,STATUS_INVALID_PARAMETER,0,TRUE);
      assert(adapter.G4SubmitFailureCount==2 && !memcmp(&first,r,sizeof(first)));
    }
  }

  /* Queue-only invalid-shape guards are unreachable after a successful
   * parser today, but must still carry a diagnostic if the caller drifts. */
  race_at=0;packet_prepare_ok=1;
  ADMISSION_RENDER_PACKET_DESCRIPTION d={0};
  ADMISSION_PRESENT_COMPUTE present={0};
  struct _ADMISSION_G4_SUBMIT_FAILURE detail={0};
  adapter.G4PendingCount=adapter.G4PendingHead=0;
  memset(adapter.G4Pending,0,sizeof(adapter.G4Pending));
  adapter.RenderPacket.State=AdmissionRenderPacketEmpty;
  context.Object.FenceOutstanding=context.GpuvaG3SecondFence=0;
  for(unsigned shape=0;shape<3;++shape) {
    memset(&detail,0,sizeof(detail));
    assert(!AdmissionG4PendingQueue(&adapter,&state,&context,NULL,&args,&d,NULL,
        shape==0?NULL:&e,shape==2?&present:NULL,0,&detail));
    assert(detail.PrepareSite==1 && detail.PrepareMask==0x1005);
    assert(!context.Object.FenceOutstanding && !adapter.G4PendingCount);
  }
  d.Kind=ADMISSION_RENDER_PACKET_KIND_COMPUTE;
  struct {unsigned fence,last,phase,expected;} fences[]={
    {0,0,0,0x30645}, {7,7,0,0x2064d}, {6,7,0,0x2064d},
    {0x80000007u,7,0,0x2064d}, {1,0xfffffffeu,0,0},
    {7,6,AppleAgxPreemptionNotificationClaimed,0}
  };
  for(unsigned n=0;n<sizeof(fences)/sizeof(fences[0]);++n) {
    AppleAgxSchedulerInitialize(&adapter.Scheduler);
    adapter.Scheduler.LastSubmittedFence=fences[n].last;
    adapter.Scheduler.PreemptionPhase=fences[n].phase;
    adapter.G4PendingCount=adapter.G4PendingHead=0;
    memset(adapter.G4Pending,0,sizeof(adapter.G4Pending));
    context.Object.FenceOutstanding=context.GpuvaG3SecondFence=0;
    args.SubmissionFenceId=fences[n].fence;
    memset(&detail,0,sizeof(detail));
    BOOLEAN accepted=AdmissionG4PendingQueue(&adapter,&state,&context,NULL,&args,
        &d,NULL,&e,NULL,0,&detail);
    assert(accepted==!fences[n].expected);
    assert(detail.PrepareMask==fences[n].expected);
    if(accepted) {
      assert(!detail.PrepareSite && adapter.G4PendingCount==1);
      assert(context.Object.FenceOutstanding==args.SubmissionFenceId);
    } else {
      assert(detail.PrepareSite==3 && !adapter.G4PendingCount);
      assert(!context.Object.FenceOutstanding);
    }
  }
  puts("Q8B_PREPARE_RECEIPT_OK");
}
'''
        cap.Depth2PacketCap().build_run(code, sources=[RA / 'src/render_job_ahead.c',
                                                   SHARED / 'src/apple_agx_g4_submit.c',
                                                   SHARED / 'src/apple_agx_scheduler.c'])
