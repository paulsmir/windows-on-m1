"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A warm, same-process C->R pair must honor the TA-on-compute class switch,
wait on the compute firmware stamp, and report the original fences exactly
once in order. A faulted compute drops its published render suffix even if
firmware resumes it. This characterizes the already-wired runtime; it does
not model Windows prebind arrival or claim to reproduce EXP2093.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_render_job_window import RC102_MODEL, RC102_SOURCES, SHARED, INCLUDE


class ComputeRenderAdmission(unittest.TestCase):
    def test_gate_exact_fences_and_fault_suffix(self):
        source = RC102_MODEL.read_text()
        source = source.replace('static void kick_successor(world *w) {',
            'static unsigned cr_classes;\nstatic void kick_successor(world *w) {')
        source = source.replace('  s.Enabled = 1; s.Resetting',
            '  s.KickClasses = cr_classes;\n  s.Enabled = 1; s.Resetting')
        source = source.replace('int main(void) {', r'''
static unsigned cr_admission(void) {
  unsigned bad = 0;
  for (unsigned gate=0; gate<2; ++gate)
  for (unsigned fault=0; fault<2; ++fault)
  for (unsigned resume=0; resume<2; ++resume) {
    world *w=&W;
    start(w,"cr-admission");
    w->prov.Runtime.Config.AbortFaultSuffix=1;
    w->runs_after_drop=(int)resume;
    cr_classes=AdmissionJobWindowGeneralClasses(gate,0,0);
    /* Warm queue epoch; compute does not require a render predecessor. */
    add(w,K_R,OUT_OK,7);run(w,20);
    add(w,K_C,fault ? OUT_FAULT:OUT_OK,7);add(w,K_R,OUT_OK,7);
    run(w,200);finish(w);
    if (w->kicked_behind[3]!=gate) violate(w,"TA-on-compute gate",3);
    if (w->rec.Barriers[1]!=gate) violate(w,"wrong typed barrier",3);
    if (w->nr!=3 || w->rep[0]!=1 || w->rep[1]!=2 || w->rep[2]!=3)
      violate(w,"original fence order",3);
    if (w->repd[2]!=fault || w->repd[3]!=(unsigned)(fault && gate))
      violate(w,"fault suffix",3);
    unsigned reports=w->nr;
    run(w,10);
    if(w->nr!=reports) violate(w,"duplicate terminal",3);
    bad+=w->violations;
  }
  cr_classes=0;
  return bad;
}
int main(void) {''')
        source = source.replace('  bad += s_order();',
            '  bad += cr_admission();\n  bad += s_order();')
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            (p/'test.c').write_text(source)
            subprocess.run(['clang','-std=c11','-Wall','-Wextra','-Werror',
                '-ftrivial-auto-var-init=pattern','-fsanitize=address,undefined',
                '-I',str(SHARED/'include'),'-I',str(INCLUDE),str(p/'test.c'),
                *map(str,RC102_SOURCES.values()),'-o',str(p/'test')],check=True)
            run = subprocess.run([str(p/'test')],capture_output=True,text=True)
            self.assertEqual(run.returncode,0,run.stdout+run.stderr)
