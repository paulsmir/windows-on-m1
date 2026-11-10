"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2100 admitted an unproven render-prelude/compute pair. General admission
must serialize either prelude endpoint by default, count the typed refusal,
and preserve ordinary pairs and all other guards when explicitly opted in.
"""
import importlib.util
import unittest
from test_render_job_depth2_exp2047 import build_run, RENDER, ROOT, WORKER
from test_render_job_window import RC102_MODEL, RC102_SOURCES


class PreludeGate(unittest.TestCase):
    def test_platform_state_and_opt_in_preserve_guards(self):
        text = WORKER.read_text()
        start = text.index('  s.General = runtime->Depth2General')
        state = text[start:text.index('  s.SameManagerSlot =', start)]
        code = r'''
#include <assert.h>
#include "render_job_window.h"
typedef struct {unsigned char Depth2General,Depth2Prelude;} Runtime;
static unsigned decide(Runtime *runtime, unsigned a, unsigned b, unsigned guard) {
 ADMISSION_JOB_WINDOW_KICK_STATE s={0};
 s.Enabled=s.InFlight=s.NextPrebound=s.SameManagerSlot=1;
 s.Running.Fence=11;s.Next.Fence=s.QueuedFence=12;
 s.Running.Process=s.Next.Process=17;s.Running.SceneId=s.Next.SceneId=20;
 s.Running.Kind=a;s.Next.Kind=b;
 s.Capabilities=ADMISSION_JOB_WINDOW_CAPS_RC98|ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER;
 s.KickClasses=AdmissionJobWindowGeneralClasses(1,1,1);
''' + state + r'''
 if(guard==1) s.NextNeedsIdle=1;
 if(guard==2) s.SetsValid=1; /* both word sets zero */
 if(guard==3) s.KickClasses=1; /* no supported class */
 if(guard==4) s.SameManagerSlot=0;
 if(guard==5) s.Capabilities=0;
 return AdmissionJobWindowKickRefusal(&s);
}
int main(void) {
 Runtime r={1,0};
 for(unsigned a=1;a<=3;++a) for(unsigned b=1;b<=3;++b) {
   assert(decide(&r,a,b,0)==((a==2||b==2)?ADMISSION_JOB_WINDOW_REFUSAL_PRELUDE_UNPROVEN:0));
   r.Depth2Prelude=1;assert(!decide(&r,a,b,0));
   assert(decide(&r,a,b,1)==61);
   assert(decide(&r,a,b,2)==62);
   assert(decide(&r,a,b,3)==64);
   r.Depth2Prelude=0;
 }
 r.Depth2Prelude=1;assert(decide(&r,2,2,4)==58);assert(decide(&r,2,3,5)==57);
 r.Depth2General=0;r.Depth2Prelude=0;assert(!decide(&r,2,3,0));
 return 0;
}
'''
        ran = build_run(code, [RENDER/'src/render_job_window.c'],
                        ['-Wconversion', '-ftrivial-auto-var-init=pattern'])
        self.assertEqual(ran.returncode, 0, ran.stdout+ran.stderr)

    def test_default_pair_matrix_and_receipt(self):
        text = WORKER.read_text()
        kick = text[text.index('static BOOLEAN AdmissionJobWindowKickWindows('):]
        at = kick.index('    AdmissionRcDecisionTyped(')
        record = kick[at:kick.index('  AdmissionJobAheadNoteRefusalWindows(', at)]
        record = record[:record.rindex('  }')]
        code = r'''
#include <assert.h>
#include <stdio.h>
#include "render_job_window.h"
#include "render_rc_progress.h"
#define TRUE 1
static unsigned long long AdmissionJobQpc(void) { return 10; }
int main(void) {
 struct { ADMISSION_RC_DECISIONS RcDecisions; unsigned RcDecisionPid,RcExportPending,Depth2Capabilities; }
 storage={0}, *runtime=&storage;
 runtime->RcDecisionPid=42;
 AdmissionRcDecisionsInitialize(&runtime->RcDecisions);
 unsigned prebindRefusal=0, fence=11;
 ADMISSION_JOB_WINDOW_KICK_STATE s={0};
 s.Enabled=s.InFlight=s.NextPrebound=s.General=s.SameManagerSlot=1;
 s.Running.Fence=11; s.Next.Fence=s.QueuedFence=12;
 s.Running.Process=s.Next.Process=17;
 s.Running.SceneId=s.Next.SceneId=20;
 s.Capabilities=ADMISSION_JOB_WINDOW_CAPS_RC98|ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER;
 s.KickClasses=AdmissionJobWindowGeneralClasses(1,1,1);
 for(unsigned a=1;a<=3;++a) for(unsigned b=1;b<=3;++b) {
   s.Running.Kind=a; s.Next.Kind=b;
   unsigned reason=AdmissionJobWindowKickRefusal(&s);
   assert(reason==((a==2 || b==2)?5u:0u));
   unsigned refusal=reason;
''' + record + r'''
   assert(runtime->RcExportPending);
 }
 ADMISSION_RC_DECISIONS r=runtime->RcDecisions;
 assert(r.Histogram[5]==5 && r.Histogram[0]==4 && r.Count==9);
 assert(fwrite(&r,sizeof(r),1,stdout)==1);
 return 0;
}
'''
        # Binary receipt is transported through hex to the real decoder.
        code = code.replace('assert(fwrite(&r,sizeof(r),1,stdout)==1);',
            'for(unsigned i=0;i<sizeof(r);++i) printf("%02x",((unsigned char *)&r)[i]);')
        ran = build_run(code, [RENDER/'src/render_job_window.c'],
                        ['-ftrivial-auto-var-init=pattern', '-Wconversion'])
        self.assertEqual(ran.returncode, 0, ran.stderr)
        spec = importlib.util.spec_from_file_location('rc', ROOT/'tools/agx_rc_progress.py')
        decoder = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(decoder)
        decoded = decoder.decode_decisions(bytes.fromhex(ran.stdout), 42)
        self.assertEqual(decoded['histogram']['5']['count'], 5)
        self.assertTrue(all(d['meaning'] == 'prelude unproven'
                            for d in decoded['decisions'] if d['reason'] == 5))

    def test_real_runtime_default_serializes_preludes(self):
        source = '#include <assert.h>\n' + RC102_MODEL.read_text()
        source = source.replace('  s.Enabled = 1; s.Resetting',
            '  s.General=general; s.Prelude=prelude; s.SameManagerSlot=1;\n  s.Enabled = 1; s.Resetting')
        source = source.replace('if (AdmissionJobWindowUsesManager(w->job[a].kind) &&',
            'if (!(w->caps & ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER) &&\n'
            '      AdmissionJobWindowUsesManager(w->job[a].kind) &&')
        source = 'static unsigned general, prelude;\n' + source
        source = source[:source.index('int main(void) {')] + r'''
int main(void) {
 for(general=0;general<2;++general) for(prelude=0;prelude<2;++prelude)
 for(unsigned a=1;a<=3;++a) for(unsigned b=1;b<=3;++b) {
   world *w=&W; start(w,"q14-default");
   w->caps|=ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER;
   add(w,K_R,OUT_OK,7); run(w,20);
   add(w,a,OUT_OK,7); add(w,b,OUT_OK,7);
   run(w,200); finish(w);
   assert(!w->violations && w->nr==3);
   assert(!!w->kicked_behind[3]==(!general || prelude || (a!=K_RP && b!=K_RP)));
   unsigned reports=w->nr;run(w,10);assert(reports==w->nr);
 }
 return 0;
}
'''
        ran = build_run(source, RC102_SOURCES.values(),
                        ['-ftrivial-auto-var-init=pattern', '-Wno-unused-function'])
        self.assertEqual(ran.returncode, 0, ran.stdout+ran.stderr)


class PreludeNotifyGate(unittest.TestCase):
    """R14 (Wom1PreludeNotifyGate).

    WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? With Q2c a prelude
    render notifies on Queue B as well as A. A job published on B before
    that notification raises B's threshold, the firmware (count ==
    threshold) defers the prelude's event to the successor's end and the
    head render is reported with the successor (batched). Invariants: no
    Queue-B publish behind a prelude that has not notified; a held head is
    released after the bound; the switch off changes no decision; no
    notification is lost (every job ends, no reset)."""

    def test_predicate_table_and_default_off_identity(self):
        code = r"""
#include <stdio.h>
#include "render_job_window.h"
static unsigned decide(unsigned a, unsigned b, unsigned caps, unsigned guard,
                       unsigned gate, unsigned complete, unsigned expired) {
 ADMISSION_JOB_WINDOW_KICK_STATE s={0};
 s.Enabled=s.InFlight=s.NextPrebound=s.SameManagerSlot=s.General=s.Prelude=1;
 s.Running.Fence=11;s.Next.Fence=s.QueuedFence=12;
 s.Running.Process=s.Next.Process=17;s.Running.SceneId=s.Next.SceneId=20;
 s.Running.Kind=a;s.Next.Kind=b;s.Capabilities=caps;
 s.KickClasses=AdmissionJobWindowGeneralClasses(1,1,1);
 if(guard==1) s.NextNeedsIdle=1;
 if(guard==2) s.SetsValid=1;
 if(guard==3) s.KickClasses=1;
 if(guard==4) s.SameManagerSlot=0;
 if(guard==5) s.PreemptionPhase=1;
 if(guard==6) s.PagingPending=1;
 if(guard==7) s.NextPrebound=0;
 if(guard==8) s.Next.Process=18;
 s.PreludeNotifyGate=gate;s.RunningPreludeComplete=complete;s.PreludeGateExpired=expired;
 return AdmissionJobWindowKickRefusal(&s);
}
int main(void) {
 const unsigned base_caps=ADMISSION_JOB_WINDOW_CAPS_RC98|ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER;
 unsigned held=0, held_kinds=0;
 if (ADMISSION_JOB_WINDOW_REFUSAL_PRELUDE_NOTIFY_GATE!=7u) return 2;
 for(unsigned p=0;p<2;++p) for(unsigned a=1;a<=5;++a) for(unsigned b=1;b<=5;++b)
 for(unsigned guard=0;guard<=8;++guard) {
   unsigned caps=base_caps|(p?ADMISSION_JOB_WINDOW_CAP_PRESENT_PAIRS:0u);
   unsigned base=decide(a,b,caps,guard,0,0,0);
   unsigned joins_b=b==3||(b==4&&p);
   for(unsigned c=0;c<2;++c) for(unsigned e=0;e<2;++e) {
     unsigned want=base==0&&a==2&&joins_b&&!c&&!e?7u:base;
     unsigned got=decide(a,b,caps,guard,1,c,e);
     /* switch off: the two inputs change nothing */
     if (decide(a,b,caps,guard,0,c,e)!=base) { printf("off a%u b%u g%u\n",a,b,guard); return 3; }
     if (got!=want) { printf("a%u b%u p%u g%u c%u e%u got %u want %u\n",a,b,p,guard,c,e,got,want); return 4; }
     if (got==7u) { ++held; held_kinds|=1u<<b; }
   }
 }
 /* the gate holds a compute and (P1) a present successor, never a render */
 if (!held || held_kinds!=((1u<<3)|(1u<<4))) { printf("held %u kinds %x\n",held,held_kinds); return 5; }
 return 0;
}
"""
        ran = build_run(code, [RENDER/'src/render_job_window.c'],
                        ['-Wconversion', '-ftrivial-auto-var-init=pattern'])
        self.assertEqual(ran.returncode, 0, ran.stdout+ran.stderr)

    def test_platform_wiring(self):
        text = WORKER.read_text()
        kick = text[text.index('static BOOLEAN AdmissionJobWindowKickWindows('):]
        kick = kick[:kick.index('\n}\n')]
        # input: the running job's runtime record, no stamp read, and only
        # with Q2c (without it a prelude never notifies on its own)
        fill = kick[kick.index('  s.PreludeNotifyGate ='):kick.index('  refusal = AdmissionJobWindowKickRefusal(&s);')]
        self.assertIn('runtime->PreludeNotifyGate', fill)
        self.assertRegex(fill, r'Runtime\.ComputeInRender\s*&&\s*runtime->Provider\.QueueProvider\.Runtime\.ComputePending\.Complete')
        self.assertIn('s.PreludeGateExpired =', fill)
        self.assertIn('ADMISSION_PRELUDE_GATE_BOUND_US', fill)
        self.assertNotIn('Stamp', fill)
        # decided by the pure predicate, before the pending head is claimed
        self.assertLess(kick.index('AdmissionJobWindowKickRefusal(&s)'),
                        kick.index('AdmissionG4PendingKickLocked(adapter, next)'))
        # retried by the next poll, not a final cross-process decision
        gate = 'ADMISSION_JOB_WINDOW_REFUSAL_PRELUDE_NOTIFY_GATE'
        tail = kick[kick.rindex('  return refusal == 4u'):]
        self.assertIn('refusal == ' + gate, tail)
        self.assertIn('? FALSE : TRUE', tail)
        note = kick[kick.index('  if (cross && refusal != 4u'):]
        note = note[:note.index(')\n')]
        self.assertIn('refusal != ' + gate, note)
        # bound: a held head is released after 1000 us
        self.assertIn('#define ADMISSION_PRELUDE_GATE_BOUND_US 1000u', text)
        # switch: default off, read at start, needs the compute Queue
        switch = text[text.index('runtime->PreludeNotifyGate =\n'):]
        switch = switch[:switch.index(';')]
        self.assertIn('AdmissionPlatformSwitchEnabled(Context, L"Wom1PreludeNotifyGate")', switch)
        self.assertIn('runtime->Initdata.RenderSharedMemory.ComputeOwnQueue', switch)
        # cadence: while the last decision was the gate the poll plan is capped
        loop = text[text.index('unsigned int planUs = 0u, sleepCapUs = 0u, action;'):]
        loop = loop[:loop.index('AdmissionJobPollPlan(')]
        self.assertRegex(loop, r'if \(runtime->PreludeGateHeld\)\s*sleepCapUs = 100u;')

    def _model(self, main):
        source = '#include <assert.h>\n#define MODEL_Q2C 1\n' + RC102_MODEL.read_text()
        old = 'if (AdmissionJobWindowUsesManager(w->job[a].kind) &&'
        self.assertIn(old, source)
        source = source.replace(old,
            'if (!(w->caps & ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER) &&\n      ' + old[4:])
        source = source[:source.index('int main(void) {')] + main
        ran = build_run(source, RC102_SOURCES.values(),
                        ['-ftrivial-auto-var-init=pattern', '-Wno-unused-function'])
        self.assertEqual(ran.returncode, 0, ran.stdout+ran.stderr)
        return ran.stdout

    def test_real_runtime_gate_ends_the_batching(self):
        self._model(r"""
int main(void) {
 for(unsigned gate=0;gate<2;++gate) for(unsigned b=K_RP;b<=K_C;++b) {
   world *w=&W; start(w,"r14"); w->notify_gate=gate; w->prelude_dur=3;
   w->caps|=ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER;
   add(w,K_R,OUT_OK,7); run(w,20);
   w->max_batch=0;
   add(w,K_RP,OUT_OK,7); add(w,b,OUT_OK,7);
   run(w,200); finish(w);
   assert(!w->violations && w->nr==3 && !w->resets);
   /* the successor is still kicked behind the running render */
   assert(w->kicked_behind[3]);
   /* without the gate the head is reported with its successor; with it
    * a compute successor leaves the head its own event. A prelude
    * successor also raises Queue A's threshold (render after render), so
    * the gate does not hold it: batched with and without the gate. */
   assert(w->max_batch==(gate&&b==K_C?1u:2u));
   assert(!(gate&&b==K_C) || w->report_tick[2]<w->report_tick[3]);
   assert(!!w->gate_refusals==(gate&&b==K_C));
 }
 return 0;
}
""")

    def test_real_runtime_no_notification_lost(self):
        self._model(r"""
int main(void) {
 for(unsigned gate=0;gate<2;++gate) for(unsigned seed=1;seed<=40;++seed) {
   world *w=&W; unsigned x=seed*2654435761u, i;
   start(w,"r14-mix"); w->notify_gate=gate; w->prelude_dur=1+seed%4;
   w->caps|=ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER;
   add(w,K_R,OUT_OK,7);
   for(i=0;i<40;++i) { x=x*1664525u+1013904223u; add(w,1+(x>>24)%3,OUT_OK,7); }
   run(w,1500); finish(w);
   assert(!w->violations && w->nr==41 && !w->resets);
   /* every notifier ends with count == threshold: nothing lost or extra */
   assert(w->ncount[0]==w->nthr[0] && w->ncount[1]==w->nthr[1]);
   assert(gate || !w->gate_refusals);
 }
 return 0;
}
""")
