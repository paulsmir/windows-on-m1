"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Depth-two infrastructure used to sample receipts and acquire diagnostic locks
on every poll with functional modes enabled. Execute the real worker poll block
and timing helpers: diagnostics off must leave only functional prebind/kick work;
submit/complete identity must still feed predictive polling and UMD completions.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest
from test_job_timing_export_rate import function, SRC

ROOT = Path(__file__).resolve().parents[1]
RA = ROOT / 'drivers/apple-agx/render-admission'


def run_c(source, extra=()):
    with tempfile.TemporaryDirectory() as d:
        c = Path(d) / 'test.c'
        exe = Path(d) / 'test'
        c.write_text(source)
        build = subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra', '-Werror',
            '-Wno-unused-function', '-ftrivial-auto-var-init=pattern',
            '-fsanitize=address,undefined', '-I', str(RA / 'include'), str(c),
            *map(str, extra), '-o', str(exe)], capture_output=True, text=True)
        if build.returncode:
            raise AssertionError(build.stderr)
        return subprocess.run([str(exe)], capture_output=True, text=True)


class JobDiagnosticsOff(unittest.TestCase):
    def test_real_worker_poll_has_no_diagnostic_calls_when_disabled(self):
        text = SRC.read_text()
        start = text.index('    /* rc90 step 2: bind the successor')
        end = text.index('    if (AdmissionDepth2DrillTakeWindows', start)
        body = text[start:end]
        source = r'''
#include <assert.h>
#define FALSE 0
#define TRUE 1
#define ADMISSION_RC_POLL 4
struct runtime { unsigned JobAhead, JobDepth2, JobDepth2Kick, Depth2KickedFence;
 unsigned JobDiagnostics, RcReceiptEnabled; };
static unsigned binds,kicks,samples,depth_samples,decisions;
static void AdmissionJobAheadPrebindWindows(void *a,struct runtime *r,unsigned f,
 unsigned *d,unsigned *ref) {(void)a;(void)r;(void)f;(void)ref;++binds;*d=1;}
static void AdmissionRcSampleWindows(struct runtime *r,unsigned s,unsigned f,unsigned e)
 {(void)r;(void)s;(void)f;(void)e;++samples;}
static void AdmissionJobDepth2SampleWindows(struct runtime *r,unsigned f)
 {(void)r;(void)f;++depth_samples;}
static unsigned AdmissionJobWindowKickWindows(void *a,struct runtime *r,unsigned f,unsigned ref)
 {(void)a;(void)r;(void)f;(void)ref;++kicks;return 1;}
static void AdmissionRcSkippedKickWindows(void *a,struct runtime *r,unsigned f,unsigned ref)
 {(void)a;(void)r;(void)f;(void)ref;++decisions;}
static void poll(struct runtime *runtime) {
 void *adapter=0; struct {unsigned Fence;} description={17};
 unsigned prebindDone=0,prebindRefusal=0,kickDone=0,kickedBehind=0;
''' + body + r'''
 (void)kickedBehind;
}
int main(void) {
 struct runtime r={0}; r.JobAhead=1;
 for(unsigned depth=0;depth<2;++depth) {
  r.JobDepth2=r.JobDepth2Kick=depth;
  for(unsigned i=0;i<160;++i) poll(&r);
 }
 assert(binds==320 && kicks==160);
 assert(!samples && !depth_samples && !decisions);
 r.JobDiagnostics=r.RcReceiptEnabled=1; poll(&r);
 assert(samples==1 && depth_samples==1 && kicks==161);
 r.JobDepth2Kick=0; poll(&r); assert(decisions==1);
}
'''
        ran = run_c(source)
        self.assertEqual(ran.returncode, 0, ran.stderr)

    def test_timing_helpers_keep_functional_identity_without_optional_work(self):
        text = SRC.read_text()
        names = ['AdmissionJobTimingStartWindows', 'AdmissionJobTimingMarkWindows',
                 'AdmissionJobTimingSubAtWindows', 'AdmissionJobTimingSubWindows',
                 'AdmissionJobTimingDelayWindows', 'AdmissionJobTimingTargetWindows',
                 'AdmissionJobTimingCpuWindows', 'AdmissionJobTimingNoteWindows',
                 'AdmissionJobAheadNoteRefusalWindows']
        funcs = '\n'.join(function(text, ('VOID ' if n in (names[0], names[7]) else 'static VOID ') + n + '(') for n in names)
        source = r'''
#include <assert.h>
#include <stddef.h>
#include "render_job_timing.h"
typedef void VOID; typedef unsigned long ULONG; typedef unsigned long ULONG_PTR;
typedef unsigned long long ULONGLONG; typedef unsigned char KIRQL;
typedef unsigned char BOOLEAN;
#define ADMISSION_JOB_CPU_CLASSES 256
struct context {unsigned unused;};
typedef struct context ADMISSION_RENDER_CONTEXT;
typedef struct {unsigned JobDiagnostics; unsigned JobTimingLock;
 ADMISSION_JOB_TIMING_STATE JobTiming; unsigned char JobCpuClass[256];} ADMISSION_PLATFORM_RUNTIME;
typedef struct {void *PlatformRuntime;} ADMISSION_CONTEXT;
static unsigned clocks,locks,cpus;
static ULONGLONG AdmissionJobQpc(void) {return ++clocks;}
static void KeAcquireSpinLock(unsigned *p,KIRQL *i) {(void)p;*i=0;++locks;}
static void KeReleaseSpinLock(unsigned *p,KIRQL i) {(void)p;(void)i;}
static ULONG KeGetCurrentProcessorNumberEx(void *p) {(void)p;++cpus;return 0;}
''' + funcs + r'''
int main(void) {
 ADMISSION_PLATFORM_RUNTIME r={0}; ADMISSION_CONTEXT a={&r};
 ADMISSION_RENDER_CONTEXT c={0};
 AdmissionJobTimingInitialize(&r.JobTiming,1000);
 AdmissionJobTimingStartWindows(&a,&c,42,7,8192);
 AdmissionJobTimingMarkWindows(&r,7,AdmissionJobPhaseComplete);
 const ADMISSION_JOB_TIMING_SLOT *slot=AdmissionJobTimingFind(&r.JobTiming,7);
 assert(slot && slot->ProcessId==42 && slot->ContextToken==(ULONG_PTR)&c);
 assert(slot->DmaBytes==8192 && slot->Qpc[AdmissionJobPhaseSubmit]);
 assert(slot->Qpc[AdmissionJobPhaseComplete]);
 unsigned before_clocks=clocks, before_locks=locks;
 for(unsigned i=0;i<160;++i) {
  AdmissionJobTimingMarkWindows(&r,7,AdmissionJobPhaseWorker);
  AdmissionJobTimingSubWindows(&r,7,AdmissionJobSubSaved);
  AdmissionJobTimingSubAtWindows(&r,7,AdmissionJobSubReleased,1);
  AdmissionJobTimingDelayWindows(&r,7);
  AdmissionJobTimingTargetWindows(&r,7,8192);
  AdmissionJobTimingCpuWindows(&r,7,0);
  AdmissionJobTimingNoteWindows(&a,7,1);
  AdmissionJobAheadNoteRefusalWindows(&r,7,0,66);
 }
 assert(clocks==before_clocks && locks==before_locks && !cpus);
 r.JobDiagnostics=1;
 AdmissionJobTimingMarkWindows(&r,7,AdmissionJobPhaseWorker);
 AdmissionJobTimingSubWindows(&r,7,AdmissionJobSubSaved);
 assert(slot->Qpc[AdmissionJobPhaseWorker] && clocks>before_clocks && locks>before_locks);
}
'''
        ran = run_c(source, [RA / 'src/render_job_timing.c'])
        self.assertEqual(ran.returncode, 0, ran.stderr)
