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
 struct { ADMISSION_RC_DECISIONS RcDecisions; unsigned RcDecisionPid,RcExportPending; }
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
