"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A depth-two drill must reject already-retired pairs, hold both original fences
until reset, and only drop the selected job (plus a head's dependent suffix)
after BOTH published endpoints retire. Run the actual platform drain, queue
runtime/provider, backend and scheduler, not an independent recovery model.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_g4_rr_event_drain import model_source
from test_render_job_window import RC102_SOURCES, SHARED, INCLUDE

MAIN = r'''
static void setup_pair(unsigned successor) {
  world *w=&W; start(w,"q12-drill");
  w->caps |= ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER;
  w->platform.QueueProvider.Runtime.Config.AbortFaultSuffix=1;
  add(w,K_R,OUT_OK,7);run(w,20);
  add(w,K_R,OUT_OK,7);add(w,successor,OUT_OK,7);
  vidsch(w);dispatch(w);kick_successor(w);++w->now;kick_successor(w);
  assert(w->kicked_behind[3] && w->win.Count==2 && !w->violations);
}

static void drill(unsigned successor, unsigned target, unsigned order) {
  world *w=&W; setup_pair(successor);
  APPLE_AGX_G13_QUEUE_RUNTIME *q=&w->platform.QueueProvider.Runtime;
  assert(!AppleAgxG13QueueRuntimeDrillArm(q,0));
  assert(!AppleAgxG13QueueRuntimeDrillArm(q,3));
  assert(AppleAgxG13QueueRuntimeDrillArm(q,target));
  assert(q->Drill.HeadFence==2 && q->Drill.NextFence==3);
  assert(!AppleAgxG13QueueRuntimeDrillArm(q,target));
  /* Worker has stopped. Reset cannot free bytes while GPU still owns them. */
  assert(AppleAgxG13QueueRuntimeDrillRelease(q));
  poll(w);assert(w->nr==1 && w->win.Count==2);
  /* Terminal flags can be separate, reversed or merged. */
  for(unsigned i=0;i<40;++i) {++w->now;firmware(w);}
  assert(w->nev-w->evr==1); /* model firmware merged the notifier */
  if(order) {
    w->nev=w->evr;
    unsigned long long head=(1ULL<<EVENT_NUMBER[Q_TA])|(1ULL<<EVENT_NUMBER[Q_3D]);
    unsigned long long next=successor==K_C ? 1ULL<<EVENT_NUMBER[Q_CO] : head;
    if(successor==K_RP) next|=1ULL<<EVENT_NUMBER[Q_CO];
    /* Transport schedules split/duplicate owned Flags in either order.
     * Order 3 loses the Flag: reset's timeout poll must still prove retirement. */
    if(order<3) {
      send_event(w,AppleAgxG13EventFlag,order==1?head:next);
      send_event(w,AppleAgxG13EventFlag,order==1?next:head);
      assert(w->nev-w->evr==2);
    }
  }
  poll(w);adopt(w);
  assert(w->nr==3 && w->rep[1]==2 && w->rep[2]==3);
  assert(w->repd[2]==(target==1) && w->repd[3]);
  assert(q->Drill.TerminalCount==2 && q->Drill.TerminalFence[0]==2 &&
         q->Drill.TerminalFence[1]==3);
  assert(q->Drill.TerminalStatus[0]==(target==1 ?
      AppleAgxG13QueueCompletionRecovered:AppleAgxG13QueueCompletionSuccess));
  assert(q->Drill.TerminalStatus[1]==AppleAgxG13QueueCompletionRecovered);
  for(unsigned i=0;i<4;++i) poll(w);
  assert(w->nr==3 && q->Drill.TerminalCount==2 && !w->win.Count);
  assert(!AppleAgxG13QueueRuntimeDrillArm(q,target));
  add(w,K_R,OUT_OK,9);run(w,100);finish(w);
  assert(w->nr==4 && !w->repd[4] && !w->violations);
}

static void real_fault_during_drill(unsigned fault_fence, unsigned runs) {
  world *w=&W;setup_pair(K_R);w->runs_after_drop=(int)runs;
  APPLE_AGX_G13_QUEUE_RUNTIME *q=&w->platform.QueueProvider.Runtime;
  assert(AppleAgxG13QueueRuntimeDrillArm(q,2));
  for(unsigned i=0;i<w->nitems;++i)
    if(w->items[i].fence==fault_fence && w->items[i].queue==Q_3D && w->items[i].type==IT_WORK)
      w->items[i].outcome=OUT_FAULT;
  reset_engine(w);
  assert(w->nr==3 && w->repd[2] && w->repd[3] && !w->violations);
  /* Shared stamps remain ambiguous after a REAL fault, even for target 2. */
  assert(q->RecoveredFaults && q->Drill.TerminalCount==2);
}

static void partial_retirement(void) {
  world *w=&W;setup_pair(K_R);
  APPLE_AGX_G13_QUEUE_RUNTIME *q=&w->platform.QueueProvider.Runtime;
  assert(AppleAgxG13QueueRuntimeDrillArm(q,1));
  assert(AppleAgxG13QueueRuntimeDrillRelease(q));
  unsigned partial=0;
  for(unsigned i=0;i<40;++i) {
    ++w->now;firmware(w);
    if(w->done[Q_3D]==q->Drill.ExpectedDone[0] && in_firmware(w))partial=1;
    unsigned busy=in_firmware(w);poll(w);
    if(busy)assert(w->nr==1 && w->win.Count==2);
  }
  assert(partial && w->nr==3 && !w->violations);
}
static void held_until_reset(void) {
  world *w=&W;setup_pair(K_R);
  APPLE_AGX_G13_QUEUE_RUNTIME *q=&w->platform.QueueProvider.Runtime;
  assert(AppleAgxG13QueueRuntimeDrillArm(q,1));
  for(unsigned i=0;i<40;++i) {++w->now;firmware(w);poll(w);}
  assert(w->nr==1 && w->win.Count==2 && !q->CompletionAvailable);
  reset_engine(w);
  assert(w->nr==3 && w->resets==1 && w->repd[2] && w->repd[3]);
  assert(!w->violations);
}
static void missed_pair(void) {
  world *w=&W;setup_pair(K_R);
  APPLE_AGX_G13_QUEUE_RUNTIME *q=&w->platform.QueueProvider.Runtime;
  for(unsigned i=0;i<40;++i) {++w->now;firmware(w);}
  assert(!AppleAgxG13QueueRuntimeDrillArm(q,1));
  assert(!q->Drill.Target && !AppleAgxG13QueueRuntimeDrillRelease(q));
  poll(w);assert(w->nr==3 && !w->repd[2] && !w->repd[3]);
}

static unsigned read_no, fail_read, retire_head;
static APPLE_AGX_BACKEND_BOOL drill_read(void *c,
    const volatile APPLE_AGX_BACKEND_U32 *p, APPLE_AGX_BACKEND_U32 *v) {
  ++read_no;
  if(read_no==fail_read)return 0;
  *v=*p;
  if(retire_head && read_no==5) *v=((world *)c)->platform.QueueProvider.Runtime.D3Pending.ExpectedDonePointer;
  return 1;
}
static void refused_reads(void) {
  for(unsigned fail=1;fail<=7;++fail) {
    world *w=&W;setup_pair(K_R);
    APPLE_AGX_G13_QUEUE_RUNTIME *q=&w->platform.QueueProvider.Runtime;
    q->Io.ReadU32=drill_read;read_no=0;fail_read=fail;retire_head=fail==7;
    assert(!AppleAgxG13QueueRuntimeDrillArm(q,1));
    assert(!q->Drill.Target);q->Io.ReadU32=io_read;
    for(unsigned i=0;i<40;++i) {++w->now;firmware(w);}
    poll(w);assert(w->nr==3 && !w->repd[2] && !w->repd[3]);
  }
}
static void preempt_before_kick(void) {
  world *w=&W;start(w,"q12-preempt-before");
  w->caps |= ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER;
  add(w,K_R,OUT_OK,7);run(w,20);
  add(w,K_R,OUT_OK,7);add(w,K_R,OUT_OK,7);
  vidsch(w);dispatch(w);begin_preempt(w);
  run(w,150);finish(w);
  assert(!w->kicked_behind[3] && w->resub[3] && !w->violations);
}
static void reused_fence_ids(void) {
  world *w=&W;setup_pair(K_R);
  APPLE_AGX_G13_QUEUE_RUNTIME *q=&w->platform.QueueProvider.Runtime;
  /* A completed receipt is history, never an instruction for wrapped IDs. */
  q->Drill.Target=1;q->Drill.HeadFence=2;q->Drill.NextFence=3;
  q->Drill.TerminalCount=2;
  for(unsigned i=0;i<40;++i) {++w->now;firmware(w);}
  poll(w);assert(w->nr==3 && !w->repd[2] && !w->repd[3]);
}
int main(void) {
  for(unsigned next=1;next<=3;++next)
    for(unsigned target=1;target<=2;++target)
      for(unsigned order=0;order<4;++order)
        drill(next,target,order);
  for(unsigned f=2;f<=3;++f) for(unsigned runs=0;runs<2;++runs) real_fault_during_drill(f,runs);
  partial_retirement();refused_reads();preempt_before_kick();
  reused_fence_ids();held_until_reset();missed_pair();
  assert(!s_preempt());assert(!s_drill(0));assert(!s_drill(1));
  puts("DEPTH2_DRILL_OK");return 0;
}
'''

class Depth2RecoveryDrill(unittest.TestCase):
    def test_real_runtime(self):
        source=model_source()
        source=source.replace('++w->resets;','++w->resets;\n  (void)AppleAgxG13QueueRuntimeDrillRelease(&w->platform.QueueProvider.Runtime);')
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'drill.c').write_text(source+MAIN)
            result=subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra',
                '-Werror','-Wno-unused-function','-ftrivial-auto-var-init=pattern',
                '-fsanitize=address,undefined','-I',str(SHARED/'include'),'-I',str(INCLUDE),
                str(p/'drill.c'),*map(str,RC102_SOURCES.values()),'-o',str(p/'drill')],
                capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(p/'drill')],capture_output=True,text=True,timeout=30)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            self.assertIn('DEPTH2_DRILL_OK',result.stdout)
