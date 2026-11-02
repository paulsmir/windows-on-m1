"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Two render jobs share queue stamps. With both terminal messages queued, the
first can retire both fences and the second must drain without a spurious
engine reset. The old rc102 model skipped Created events, hiding the defect.
Execute the production platform poll/drain/composer and real provider/runtime/
backend/scheduler; only transport, firmware, manager binding and WDK are modeled.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_render_job_window import RC102_MODEL, RC102_SOURCES, SHARED, INCLUDE
from test_g4_tdr_drill_poisoned_completion import extract


def model_source():
    src = RC102_MODEL.read_text()
    src = src.replace('#include <stdio.h>', '#include <stdio.h>\n#include <assert.h>\n#include "apple_agx_platform_provider.h"\n#include "j313_agx_g2.generated.h"')
    src = src.replace('APPLE_AGX_G13_QUEUE_PROVIDER prov;',
                      'APPLE_AGX_PLATFORM_PROVIDER platform;\n  unsigned state[J313_AGX_G2_CHANNEL_STATE_STRIDE/4];')
    src = src.replace('w->prov', 'w->platform.QueueProvider')
    src = src.replace('events[64]', 'events[APPLE_AGX_PLATFORM_EVENT_RING_ENTRY_COUNT]')
    src = src.replace('% 64u', '% APPLE_AGX_PLATFORM_EVENT_RING_ENTRY_COUNT')
    src = src.replace('  s.Enabled = 1; s.Resetting',
        '  s.General = (w->caps & ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER) != 0;\n'
        '  s.SameManagerSlot = s.General;\n  s.Enabled = 1; s.Resetting')
    src = src.replace('if (AdmissionJobWindowUsesManager(w->job[a].kind) &&',
        'if (!(w->caps & ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER) &&\n      AdmissionJobWindowUsesManager(w->job[a].kind) &&')
    platform=(SHARED/'src/apple_agx_platform_provider.c').read_text()
    composer=(SHARED/'src/apple_agx_platform_composer.c').read_text()
    real='\n#define PLATFORM_NULL ((void *)0)\n#define COMPOSER_NULL ((void *)0)\n'
    real+='\n'.join(extract(composer, f) for f in
        ['AppleAgxPlatformComposerPrepareEvent','AppleAgxPlatformComposerApplyEvent'])
    real+='\n'+'\n'.join(extract(platform, f) for f in
        ['AppleAgxPlatformZero','AppleAgxPlatformCopy','AppleAgxPlatformTransportValid',
         'AppleAgxPlatformProviderDrainEvents','AppleAgxPlatformProviderPoll'])
    a=src.index('static void apply(');b=src.index('/* The worker after', a)
    src=src[:a]+real+r'''
static APPLE_AGX_BACKEND_BOOL doorbell(void *c, unsigned d) {(void)c;(void)d;return 1;}
static APPLE_AGX_BACKEND_U64 now_ticks(void *c) {return 1000ULL+((world *)c)->now;}
static void poll(world *w) {
  APPLE_AGX_PLATFORM_PROVIDER *p=&w->platform;
  unsigned drained=0, completed=0;
  p->Initialized=1;p->Runtime=&w->be;
  p->Composer.Initialized=1;p->Composer.Runtime=&w->be;
  p->Composer.QueueProvider=&p->QueueProvider;
  p->Channels.Event.StateCpuAddress=(unsigned char *)w->state;
  p->Channels.Event.RingCpuAddress=(unsigned char *)w->events;
  p->Transport.Context=w;p->Transport.FlushForDevice=io_flush;
  p->Transport.FlushForCpu=io_flush;p->Transport.MemoryBarrier=io_barrier;
  p->Transport.PublishU32=io_publish;p->Transport.ReadU32=io_read;
  p->Transport.RingDoorbell=doorbell;p->Transport.Quiesce=io_quiesce;p->Transport.NowTicks=now_ticks;
  w->state[APPLE_AGX_PLATFORM_CHANNEL_READ_POINTER_OFFSET/4]=w->evr;
  w->state[APPLE_AGX_PLATFORM_CHANNEL_WRITE_POINTER_OFFSET/4]=w->nev;
  w->reports_this_poll=0;
  if(!AppleAgxPlatformProviderPoll(p,64,&drained,&completed)) {
    printf("REJECT poll=%u drain=%u ingest=%u runtime=%u provider=%u backend=%u\n",
      p->LastPollGuard,p->LastDrainGuard,p->QueueProvider.LastIngestGuard,
      p->QueueProvider.LastIngestRuntimeResult,p->QueueProvider.Phase,w->be.Phase);
    violate(w,"real platform drain failed",p->QueueProvider.PendingFence);w->faulted=1;
  }
  w->evr=w->state[APPLE_AGX_PLATFORM_CHANNEL_READ_POINTER_OFFSET/4];
  if(w->reports_this_poll>w->max_batch)w->max_batch=w->reports_this_poll;
}
''' + src[b:]
    src=src.replace('int main(void) {','static int legacy_main(void) {')
    return src

MAIN=r'''
static void pair(unsigned successor, unsigned order, unsigned merged) {
  world *w=&W;start(w,"q10-terminal-order");
  w->caps |= ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER;
  add(w,K_R,OUT_OK,7);run(w,20); /* warm the resident manager/queue epoch */
  assert(w->nr==1 && !w->violations);
  add(w,K_R,OUT_OK,7);add(w,successor,OUT_OK,7);
  vidsch(w);dispatch(w);
  /* The predecessor event is queued, but the CPU has not drained it when
   * the successor is published. Both are outstanding in the real provider. */
  if(!merged) for(unsigned i=0;i<15;++i) {++w->now;firmware(w);}
  kick_successor(w);++w->now;kick_successor(w);
  assert(w->kicked_behind[3] && w->win.Count==2);
  if(successor==K_R) {
    int barrier=0;
    for(unsigned i=0;i<w->nitems;++i) {
      item *it=&w->items[i];
      if(it->fence==3 && it->type==IT_BARRIER && it->queue==Q_TA) {
        assert(it->wait_queue==Q_3D && it->wait_value==2);barrier=1;
      }
    }
    assert(barrier);
  }
  for(unsigned i=0;i<20;++i) {++w->now;firmware(w);}
  assert(!in_firmware(w));
  unsigned queued=w->nev-w->evr;
  assert(queued==(merged?1u:2u));
  if(order && queued==2) {
    unsigned char tmp[APPLE_AGX_G13_EVENT_MESSAGE_SIZE];
    memcpy(tmp,w->events[w->evr],sizeof(tmp));
    memcpy(w->events[w->evr],w->events[w->evr+1],sizeof(tmp));
    memcpy(w->events[w->evr+1],tmp,sizeof(tmp));
  }
  poll(w);
  assert(!w->faulted && !w->violations && !w->resets);
  assert(w->evr==w->nev && w->nr==3 && w->max_fw==2);
  assert(w->rep[0]==1 && w->rep[1]==2 && w->rep[2]==3);
  assert(!w->repd[2] && !w->repd[3]);
  assert(w->platform.QueueProvider.Phase==AppleAgxG13QueueProviderCreated);
  assert(w->be.Phase==AppleAgxBackendRuntimeReady);
  poll(w);assert(w->nr==3); /* no duplicate DMA completion */
  add(w,K_R,OUT_OK,7);run(w,30);finish(w);
  assert(w->nr==4 && w->rep[3]==4 && !w->violations && !w->resets);
}

static void reject_idle_message(unsigned kind, unsigned long long bits) {
  world *w=&W;start(w,"q10-invalid-idle");
  add(w,K_R,OUT_OK,7);run(w,20);assert(w->nr==1);
  send_event(w,kind,bits);
  unsigned read=w->evr;
  poll(w);
  assert(w->faulted && w->violations==1 && w->evr==read && w->nr==1);
  assert(w->platform.LastDrainGuard==AppleAgxPlatformDrainGuardPrepareEvent);
  assert(w->platform.LastEventMessageValid);
  assert(!memcmp(w->platform.LastEventMessage,w->events[read],APPLE_AGX_G13_EVENT_MESSAGE_SIZE));
}
static void boundaries(void) {
  world *w=&W;
  const unsigned kinds[]={AppleAgxG13EventFault,AppleAgxG13EventTimeout,
      AppleAgxG13EventGrowTvb,AppleAgxG13EventChannelError,0xffffffffu};
  for(unsigned i=0;i<sizeof(kinds)/sizeof(kinds[0]);++i)reject_idle_message(kinds[i],0);
  reject_idle_message(AppleAgxG13EventFlag,1ULL<<63); /* foreign slot */
  start(w,"q10-active-stale");add(w,K_R,OUT_OK,7);run(w,20);
  add(w,K_R,OUT_OK,7);vidsch(w);dispatch(w);
  /* Old bits alone cannot retire fresh work; its stamps/done must reach it. */
  send_event(w,AppleAgxG13EventFlag,(1ULL<<EVENT_NUMBER[Q_TA])|(1ULL<<EVENT_NUMBER[Q_3D]));
  poll(w);assert(!w->faulted && w->nr==1 && w->win.Count==1);
  run(w,30);assert(w->nr==2 && !w->violations);
  APPLE_AGX_G13_QUEUE_PROVIDER *p=&w->platform.QueueProvider;
  APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH b;
  unsigned char msg[APPLE_AGX_G13_EVENT_MESSAGE_SIZE]={0};
  put32(msg,AppleAgxG13EventFlag);put64(msg+4,1ULL<<EVENT_NUMBER[Q_TA]);
  assert(!AppleAgxG13QueueProviderIngestEvent(p,msg,sizeof(msg)-1,&b));
  assert(!AppleAgxG13QueueProviderIngestEvent(p,NULL,sizeof(msg),&b));
  assert(!AppleAgxG13QueueProviderIngestEvent(p,msg,sizeof(msg),NULL));
  assert(!AppleAgxG13QueueProviderIngestEvent(NULL,msg,sizeof(msg),&b));
  for(unsigned i=0;i<6;++i) {
    APPLE_AGX_G13_QUEUE_PROVIDER saved=*p;
    switch(i) {
    case 0:p->PendingFence=2;break;
    case 1:p->SuccessorFence=3;break;
    case 2:p->Runtime.PendingFence=2;break;
    case 3:p->Runtime.Successor.Fence=3;break;
    case 4:p->Runtime.CompletionAvailable=1;break;
    case 5:p->Runtime.Phase=AppleAgxG13QueueRuntimeFaulted;break;
    }
    assert(!AppleAgxG13QueueProviderIngestEvent(p,msg,sizeof(msg),&b));*p=saved;
  }
  p->Phase=AppleAgxG13QueueProviderInitialized;
  assert(!AppleAgxG13QueueProviderIngestEvent(p,msg,sizeof(msg),&b));
  p->Phase=AppleAgxG13QueueProviderFaulted;
  assert(!AppleAgxG13QueueProviderIngestEvent(p,msg,sizeof(msg),&b));
  p->Phase=AppleAgxG13QueueProviderCreated;
  /* Cover both words of the bitmap and an absent compute lease. */
  p->Config.Ta.EventNumber=70;p->Config.D3.EventNumber=127;
  put64(msg+4,0);put64(msg+12,(1ULL<<6)|(1ULL<<63));
  memset(&b,0xa5,sizeof(b));assert(AppleAgxG13QueueProviderIngestEvent(p,msg,sizeof(msg),&b));
  APPLE_AGX_G13_QUEUE_PROVIDER_EVENT_BATCH zero={0};assert(!memcmp(&b,&zero,sizeof(b)));
  put64(msg+12,1ULL<<7);assert(!AppleAgxG13QueueProviderIngestEvent(p,msg,sizeof(msg),&b));
  put64(msg+12,0);put64(msg+4,1ULL<<EVENT_NUMBER[Q_CO]);p->Config.HasCompute=0;
  assert(!AppleAgxG13QueueProviderIngestEvent(p,msg,sizeof(msg),&b));
  p->Config.HasCompute=1;assert(AppleAgxG13QueueProviderIngestEvent(p,msg,sizeof(msg),&b));
  put64(msg+4,0);assert(AppleAgxG13QueueProviderIngestEvent(p,msg,sizeof(msg),&b));
}
int main(void) {
  for(unsigned k=K_R;k<=K_C;k+=K_C-K_R)
    for(unsigned order=0;order<2;++order)
      for(unsigned merged=0;merged<2;++merged)pair(k,order,merged);
  boundaries();
  puts("Q10_RR_EVENT_DRAIN_OK");
}
'''

class RenderEventDrain(unittest.TestCase):
    def test_real_drain_two_terminal_notifications(self):
        with tempfile.TemporaryDirectory() as td:
            p=Path(td);(p/'test.c').write_text(model_source()+MAIN)
            result=subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra',
                '-Werror','-Wno-unused-function','-ftrivial-auto-var-init=pattern',
                '-fsanitize=address,undefined','-I',str(SHARED/'include'),'-I',str(INCLUDE),
                str(p/'test.c'),*map(str,RC102_SOURCES.values()),'-o',str(p/'test')],
                capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('Q10_RR_EVENT_DRAIN_OK',result.stdout)
