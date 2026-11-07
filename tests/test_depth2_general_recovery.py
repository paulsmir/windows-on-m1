"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A successor that writes success stamps after its predecessor faults must still
be reported dropped, never as successful dependent work. The real queue/provider/
backend/scheduler model proves ordered exact-once terminal reports, preemption,
TDR and resumption with two users, for every typed pair including preludes.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_render_job_window import RC102_MODEL, RC102_SOURCES, SHARED, INCLUDE

class GeneralRecovery(unittest.TestCase):
    def test_fault_suffix_and_all_pairs(self):
        source=RC102_MODEL.read_text()
        source=source.replace('config.HasCompute = APPLE_AGX_BACKEND_TRUE;',
            'config.AbortFaultSuffix = 1; config.HasCompute = APPLE_AGX_BACKEND_TRUE;')
        source=source.replace('if (!runs_after && !w->repd[4])', 'if (!w->repd[4])')
        # Keep existing legacy scenarios; enable resident-slot admission only
        # in the new pair matrix, whose manager lifetime is separately tested.
        source=source.replace('  s.Enabled = 1; s.Resetting',
            '  s.General = (w->caps & ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER) != 0;\n'
            '  s.Prelude = 1; s.SameManagerSlot = s.General;\n  s.Enabled = 1; s.Resetting')
        source=source.replace('if (AdmissionJobWindowUsesManager(w->job[a].kind) &&',
            'if (!(w->caps & ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER) &&\n      AdmissionJobWindowUsesManager(w->job[a].kind) &&')
        source=source.replace('int main(void) {',r'''
static unsigned all_pairs(void) {
  unsigned bad=0;
  for(unsigned a=1;a<=3;++a) for(unsigned b=1;b<=3;++b)
  for(unsigned outcome=0;outcome<3;++outcome)
  for(unsigned runs=0;runs<2;++runs) {
    world *w=&W;
    start(w,"general-pair");
    w->caps |= ADMISSION_JOB_WINDOW_CAP_LIVE_MANAGER;
    w->runs_after_drop=(int)runs;
    /* Warm manager/queue epoch, then a bound pair. */
    add(w,K_R,OUT_OK,7); run(w,20);
    add(w,a,outcome,7); add(w,b,OUT_OK,7);
    run(w,200); finish(w);
    if(!w->kicked_behind[3]) violate(w,"pair not exercised",3);
    if(outcome==OUT_FAULT && (!w->repd[2] || !w->repd[3]))
      violate(w,"fault suffix escaped",3);
    unsigned reported=w->nr;
    run(w,10);
    if(reported!=w->nr) violate(w,"duplicate terminal",3);
    /* New work in the same recovered model remains live (DWM checkpoint
     * on hardware still required; this proves scheduler recovery only). */
    add(w,K_R,OUT_OK,9); run(w,100); finish(w);
    bad+=w->violations;
  }
  return bad;
}
int main(void) {''')
        source=source.replace('  bad += s_order();','  bad += all_pairs();\n  bad += s_order();')
        with tempfile.TemporaryDirectory() as d:
            p=Path(d);(p/'test.c').write_text(source)
            result=subprocess.run([os.environ.get('CC','clang'),'-std=c11','-Wall','-Wextra',
                '-Werror','-ftrivial-auto-var-init=pattern','-fsanitize=address,undefined',
                '-I',str(SHARED/'include'),'-I',str(INCLUDE),str(p/'test.c'),
                *map(str,RC102_SOURCES.values()),'-o',str(p/'test')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
            result=subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(result.returncode,0,result.stdout+result.stderr)
