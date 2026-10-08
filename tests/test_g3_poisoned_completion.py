"""EXP996 kernel dump: a poisoned process must not withhold a finished fence.

Fence 0x712 (pid 0x148c) finished on the GPU (scene Started/GpuDone, scheduler
committed, Completion phase Reported), but AdmissionGpuvaG3PrivateReported
returned FALSE because AdmissionG3PrivateReap fails for a poisoned process.
The completion transaction never finished, the next firmware event made the
backend fault the shared scheduler (SchedulerFaulted 0x40D6B), the next paging
submission was refused with STATUS_DEVICE_BUSY and VidSch bugchecked 0x119.
PrivateCompletionFence also stayed set, which blocks BeginJob for every process.
Invariant: once the scene's GPU work is done, reporting succeeds and clears the
private completion state; reclaim failures only keep the scene's storage.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/src/gpuva_g3_windows.c'


def function(text, name):
    m = re.search(r'(?m)^BOOLEAN ' + name + r'\([^;{]*\)\s*\{', text)
    if not m:
        return ''
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]


BODY = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
typedef unsigned char BOOLEAN; typedef unsigned long ULONG; typedef long LONG;
#define TRUE 1
#define FALSE 0
#define PASSIVE_LEVEL 0
static int KeGetCurrentIrql(void){return PASSIVE_LEVEL;}
typedef struct { int held; } FAST_MUTEX;
static void ExAcquireFastMutex(FAST_MUTEX *m){assert(!m->held);m->held=1;}
static void ExReleaseFastMutex(FAST_MUTEX *m){assert(m->held);m->held=0;}
static LONG InterlockedCompareExchange(volatile LONG *p,LONG x,LONG c){LONG o=*p;if(o==c)*p=x;return o;}
static LONG InterlockedExchange(volatile LONG *p,LONG v){LONG o=*p;*p=v;return o;}
typedef struct _ADMISSION_CONTEXT { int unused; } ADMISSION_CONTEXT;
typedef struct { FAST_MUTEX Lock; ADMISSION_CONTEXT *Adapter; ULONG PrivateCompletionFence; } ADMISSION_G3_STATE;
typedef struct _ADMISSION_RENDER_CONTEXT { void *GpuvaG3Process; volatile LONG GpuvaG3PrivateFence; } ADMISSION_RENDER_CONTEXT;
typedef struct _ADMISSION_G3_PRIVATE_SCENE { struct _ADMISSION_G3_PRIVATE_SCENE *Next; ADMISSION_RENDER_CONTEXT *Context;
  ULONG Fence, ResumeFence, Submitting, Queued, Started, GpuDone, Reported, ReleaseRequested, Quarantined; } ADMISSION_G3_PRIVATE_SCENE;
typedef struct { ADMISSION_G3_STATE *State; ADMISSION_G3_PRIVATE_SCENE *PrivateScenes; BOOLEAN Poisoned; ULONG PoisonSite, PoisonBrokerStatus, OsProcessId; } ADMISSION_G3_PROCESS;
static int reaps;
static BOOLEAN AdmissionG3PrivateReap(ADMISSION_G3_PROCESS *p){++reaps;return !p->Poisoned;}
@@FUNC@@
int main(void){
  ADMISSION_CONTEXT a; ADMISSION_G3_STATE st; memset(&st,0,sizeof st); st.Adapter=&a;
  ADMISSION_G3_PROCESS p; memset(&p,0,sizeof p); p.State=&st;
  ADMISSION_RENDER_CONTEXT c; memset(&c,0,sizeof c); c.GpuvaG3Process=&p; c.GpuvaG3PrivateFence=0x712;
  ADMISSION_G3_PRIVATE_SCENE s; memset(&s,0,sizeof s); s.Context=&c; s.Fence=0x712;
  s.Queued=s.Started=s.GpuDone=1; p.PrivateScenes=&s; st.PrivateCompletionFence=0x712;
  p.Poisoned=TRUE;
  if(!AdmissionGpuvaG3PrivateReported(&a,&c,0x712)){puts("finished fence withheld by poisoned process");return 1;}
  assert(s.Reported && !s.Queued && c.GpuvaG3PrivateFence==0 && st.PrivateCompletionFence==0);
  assert(!st.Lock.held);
  /* A legacy (non-private) job of a poisoned process is reportable too. */
  assert(AdmissionGpuvaG3PrivateReported(&a,&c,0x713));
  /* GPU work not finished: still withheld. */
  ADMISSION_G3_PRIVATE_SCENE t; memset(&t,0,sizeof t); t.Context=&c; t.Fence=0x714; t.Queued=t.Started=1;
  p.PrivateScenes=&t; c.GpuvaG3PrivateFence=0x714;
  assert(!AdmissionGpuvaG3PrivateReported(&a,&c,0x714) && t.Queued && !t.Reported);
  /* Quarantined after GPU completion (e.g. reset raced): reported, storage kept. */
  t.GpuDone=1; t.Quarantined=1;
  assert(AdmissionGpuvaG3PrivateReported(&a,&c,0x714) && t.Reported && t.Quarantined);
  puts("EXP997 poisoned completion: PASS");
  return 0;
}
'''


class PoisonedCompletion(unittest.TestCase):
    def test_finished_fence_reported_despite_poison(self):
        func = function(SRC.read_text(), 'AdmissionGpuvaG3PrivateReported')
        self.assertTrue(func)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'replay.c'; exe = Path(tmp) / 'replay'
            src.write_text(BODY.replace('@@FUNC@@', func))
            built = subprocess.run(['clang', '-std=c11', '-Wall', '-Wno-unused-function',
                                    '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP997 poisoned completion: PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
