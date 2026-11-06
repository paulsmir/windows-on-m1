"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2203: four GravityMark jobs per frame (R2, C3, R4, R6) reached the KMD
only 0-0.2 ms after their predecessor completed, about 3.7 ms of GPU idle per
frame, and no receipt could say whether the UMD called SubmitCommandCb late
(a replay wait) or dxgkrnl delivered the packet late. measure-umd-submit
(EXP2210) is receipt-only:
- one row per SubmitCommandCb while diagnostics measure, none otherwise;
- the row carries the command VA the KMD completion stream joins on, QPC
  before <= after around the call, and the render fence it will signal;
- the wait classes are the time completed since the previous submission on
  the thread, restarted for a new Execute, never double counted;
- with or without the row the submission path behaves identically
  (jobs, residency, failure state).
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

import test_umd_cpu_pool_rehome as rehome

ROOT = Path(__file__).resolve().parents[1]
INCLUDE = ROOT / 'drivers/apple-agx/render-admission/umd/include'

SIDE = rehome.UMD_SIDE.replace(
    '#define watch_slots(...) ((void)0)',
    '#define watch_slots(...) ((void)0)\n#define ADMISSION_UMD_SUBMIT_RECEIPT_ON 1').replace(
    'static bool AdmissionUmdMeasureEnabled() {return false;}',
    'static bool measuring;\nstatic bool AdmissionUmdMeasureEnabled() {return measuring;}\n'
    'using LONGLONG=long long; union LARGE_INTEGER { long long QuadPart; };\n'
    'static long long qpc_clock=1000;\n'
    'static int QueryPerformanceCounter(LARGE_INTEGER *q) {q->QuadPart=(qpc_clock+=7);return 1;}\n'
    'static unsigned wait_calls;\n'
    'extern "C" unsigned AdmissionUmdWaitSubmitDelta(unsigned *serial,unsigned out[6]) {\n'
    ' ++wait_calls;*serial=5;for(unsigned i=0;i<6;++i)out[i]=10+i;return 2;}').replace(
    'static void AdmissionUmdDiagnostic(const char *name,HRESULT hr,const UINT *v,size_t n) {',
    'static unsigned submit_rows; static UINT submit_row[24]; static HRESULT submit_hr;\n'
    'static void AdmissionUmdDiagnostic(const char *name,HRESULT hr,const UINT *v,size_t n) {\n'
    ' if(!strcmp(name,"measure-umd-submit")) {assert(n==24 && n<=32);++submit_rows;\n'
    '  memcpy(submit_row,v,sizeof(submit_row));submit_hr=hr;return;}')

CASES = r'''
 /* Not measuring: no row, no clock, same job. */
 init(d);hold(d);measuring=false;
 assert(submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence)==1 && jobs==1);
 assert(!submit_rows && !wait_calls);
 const uint64_t plain_fence=fence;
 /* Measuring: exactly one row with the join key and the bracketing QPC. */
 init(d);hold(d);measuring=true;
 assert(submit(&d,nullptr,0,0x10000,256,&private_data,4,&fence)==1 && jobs==1 && fence==plain_fence);
 assert(submit_rows==1 && wait_calls==1 && submit_hr==S_OK);
 assert(submit_row[0]==1 && submit_row[2]==0x10000 && submit_row[3]==0 && submit_row[4]==256);
 assert(submit_row[5]==4 && submit_row[6]==private_data && submit_row[8]==0);
 unsigned long long before=submit_row[10]|((unsigned long long)submit_row[11]<<32);
 unsigned long long after=submit_row[12]|((unsigned long long)submit_row[13]<<32);
 assert(before && after>before);
 /* the fence this submission signals (NextRenderFence + 1 at the call) */
 assert((submit_row[14]|((unsigned long long)submit_row[15]<<32))==fence);
 assert(submit_row[16]==5 && submit_row[17]==10 && submit_row[22]==15 && submit_row[23]==2);
'''


class UmdSubmitReceipt(unittest.TestCase):
    def test_one_row_per_measured_submission_without_behaviour_change(self):
        rehome.CpuPoolRehome('test_read_only_open_is_proved_for_reading_only').build(
            CASES, extra_names=('submit_receipt',), side=SIDE)

    def test_wait_delta_is_per_submission_and_restarts_per_execute(self):
        source = r'''
#include <assert.h>
#include "umd_wait_profile.h"
int main(void) {
  static AgxWaitProfile p;
  unsigned long long prev[AGX_WAIT_SUBMIT_CLASSES] = {0};
  unsigned serial = 0, us[AGX_WAIT_SUBMIT_CLASSES];
  assert(AgxWaitSubmitDelta(&p, prev, &serial, us) == 1u && us[0] == 0u);
  AgxWaitReset(&p, 7u, 1000000u);
  p.Phase = AgxWaitPhaseDraw;
  unsigned outer = AgxWaitPush(&p, AgxWaitFlush, 100u);
  unsigned inner = AgxWaitPush(&p, AgxWaitInflightSlot, 110u);
  unsigned leaf = AgxWaitPush(&p, AgxWaitRenderFence, 120u);
  AgxWaitPop(&p, leaf, 620u);
  AgxWaitPop(&p, inner, 630u);
  AgxWaitPop(&p, outer, 700u);
  assert(AgxWaitSubmitDelta(&p, prev, &serial, us) == 2u);
  assert(us[0] == 520u && us[2] == 600u && us[4] == 500u && us[1] == 0u);
  /* nothing completed since: zero, no double count */
  assert(AgxWaitSubmitDelta(&p, prev, &serial, us) == 0u && us[0] == 0u && us[2] == 0u);
  p.Phase = AgxWaitPhaseQueryResolve;
  unsigned q = AgxWaitPush(&p, AgxWaitQueryWriters, 800u);
  AgxWaitPop(&p, q, 1100u);
  assert(AgxWaitSubmitDelta(&p, prev, &serial, us) == 0u && us[1] == 300u && us[0] == 0u);
  /* next Execute: baseline restarts */
  AgxWaitReset(&p, 8u, 1000000u);
  p.Phase = AgxWaitPhaseSubmit;
  q = AgxWaitPush(&p, AgxWaitBatchPoll, 0u);
  AgxWaitPop(&p, q, 40u);
  assert(AgxWaitSubmitDelta(&p, prev, &serial, us) == 2u && serial == 8u && us[3] == 40u);
  return 0;
}
'''
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'delta.c'
            exe = Path(tmp) / 'delta'
            src.write_text(source)
            build = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra',
                                    '-Werror', '-I', str(INCLUDE), str(src),
                                    '-o', str(exe)], capture_output=True, text=True)
            self.assertEqual(build.returncode, 0, build.stderr)
            self.assertEqual(subprocess.run([str(exe)]).returncode, 0)


if __name__ == '__main__':
    unittest.main()
