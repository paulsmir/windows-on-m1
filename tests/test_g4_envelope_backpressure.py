"""EXP1013/EXP1014: transient backend busy states are backpressure, not errors.

EXP1012/1013 receipts: 211 SubmitCommandVirtual rejections, first at branch 7
(EnvelopeState) predicate 13 with runtime predicate 8 (WorkScheduled != 0).
The backend worker reports the completed fence to dxgkrnl before
AdmissionPlatformWorkerFinished clears WorkScheduled, so VidSch submits the
next job inside that window; the failed submission marks the device in error
(EXP1009 ETW VidSchErrorFailedSubmitCommandVirtual), which then leaves its new
allocations unpaged and fails the copy QUERY.
EXP1014 after accepting WorkScheduled alone: 96 rejections at runtime
predicate 4 (backend still Submitted): VidSch also submits while the previous
job runs. DXGKDDI_SUBMITCOMMANDVIRTUAL allows STATUS_INVALID_PARAMETER only
for malformed data and then puts the device in error.
Invariants:
- with the runtime ready and the render slot empty, admission returns at once;
- WorkScheduled, a Submitted backend, or an occupied render slot are waited
  out (bounded) and then admitted;
- a busy state that outlives the timeout is refused with its predicate;
- stopping, resetting, failed or missing runtimes are refused without waiting;
- the G4 envelope uses the bounded wait.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/render-admission/src/backend_platform_windows.c'
G3 = ROOT / 'drivers/apple-agx/render-admission/src/gpuva_g3_windows.c'


def function(text, name):
    m = re.search(r'(?m)^_Use_decl_annotations_ BOOLEAN ' + name + r'\([^;{]*\)\s*\{', text)
    if not m:
        return ''
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]


BODY = r'''
#include <cassert>
#include <cstdio>
typedef unsigned char BOOLEAN; typedef unsigned long ULONG; typedef long LONG; typedef unsigned long long ULONGLONG;
typedef unsigned char KIRQL; typedef long NTSTATUS;
#define TRUE 1
#define FALSE 0
#define NULL 0
#define PASSIVE_LEVEL 0
#define _Use_decl_annotations_
enum { Executive, KernelMode };
enum { AppleAgxBackendRuntimeReady = 3, AppleAgxBackendRuntimeSubmitted = 4, AppleAgxBackendRuntimeFailed = 6 };
enum { AdmissionRenderPacketEmpty = 0, AdmissionRenderPacketActive = 3 };
struct LARGE_INTEGER { long long QuadPart; };
static LONG InterlockedCompareExchange(volatile LONG *p, LONG, LONG){ return *p; }
struct BACKEND { int Phase; };
struct KEVENT { int x; };
struct ADMISSION_PLATFORM_RUNTIME { BOOLEAN ProviderReady, BackendStarted; BACKEND Backend; void *WorkItem;
  volatile LONG Stopping, Resetting, WorkScheduled; KEVENT WorkIdle; };
struct PACKET { int State; };
struct ADMISSION_CONTEXT { void *PlatformRuntime; int SchedulerLock; PACKET RenderPacket; };
static ULONGLONG now; static int waits, delays, finish_after;
static ADMISSION_PLATFORM_RUNTIME *rt; static ADMISSION_CONTEXT *ctx;
static ULONGLONG KeQueryInterruptTime(void){ return now; }
static KIRQL KeGetCurrentIrql(void){ return PASSIVE_LEVEL; }
static void KeAcquireSpinLock(int*, KIRQL*){} static void KeReleaseSpinLock(int*, KIRQL){}
static int AdmissionRenderPacketState(PACKET *p){ return p->State; }
static void tick(){ now += 100000ULL; if(finish_after && --finish_after==0){ rt->WorkScheduled=0;
  rt->Backend.Phase=AppleAgxBackendRuntimeReady; ctx->RenderPacket.State=AdmissionRenderPacketEmpty; } }
static NTSTATUS KeWaitForSingleObject(KEVENT*, int, int, BOOLEAN, LARGE_INTEGER*){ ++waits; tick(); return 0; }
static NTSTATUS KeDelayExecutionThread(int, BOOLEAN, LARGE_INTEGER*){ ++delays; tick(); return 0; }
@@FUNCS@@
int main(){
  ADMISSION_PLATFORM_RUNTIME r={1,1,{AppleAgxBackendRuntimeReady},(void*)1,0,0,0,{0}};
  ADMISSION_CONTEXT c={&r,0,{AdmissionRenderPacketEmpty}}; rt=&r; ctx=&c; ULONG why=99;
  assert(AdmissionPlatformRuntimeAwaitWork(&c,1000,&why) && why==0 && waits+delays==0);
  /* Worker returning after the fence notification. */
  r.WorkScheduled=1; finish_after=3;
  if(!AdmissionPlatformRuntimeAwaitWork(&c,1000,&why)){printf("returning worker refused new work (%lu)\n",why);return 1;}
  assert(why==0 && waits==3);
  /* VidSch submits while the previous job still runs. */
  waits=0; r.WorkScheduled=1; r.Backend.Phase=AppleAgxBackendRuntimeSubmitted; c.RenderPacket.State=AdmissionRenderPacketActive; finish_after=5;
  if(!AdmissionPlatformRuntimeAwaitWork(&c,1000,&why)){printf("running job refused next submission (%lu)\n",why);return 1;}
  /* Render slot busy with an idle worker: timer delay, not a spin on the idle event. */
  waits=delays=0; c.RenderPacket.State=AdmissionRenderPacketActive; finish_after=2;
  assert(AdmissionPlatformRuntimeAwaitWork(&c,1000,&why) && delays==2 && waits==0);
  /* A job outliving the bound is refused with its predicate. */
  r.Backend.Phase=AppleAgxBackendRuntimeSubmitted; finish_after=0; now=0;
  assert(!AdmissionPlatformRuntimeAwaitWork(&c,50,&why) && why==4);
  r.Backend.Phase=AppleAgxBackendRuntimeReady;
  /* Non-transient refusals never wait. */
  waits=delays=0;
  r.Stopping=1; assert(!AdmissionPlatformRuntimeAwaitWork(&c,1000,&why) && why==6); r.Stopping=0;
  r.Resetting=1; assert(!AdmissionPlatformRuntimeAwaitWork(&c,1000,&why) && why==7); r.Resetting=0;
  r.Backend.Phase=AppleAgxBackendRuntimeFailed; assert(!AdmissionPlatformRuntimeAwaitWork(&c,1000,&why) && why==4);
  r.Backend.Phase=AppleAgxBackendRuntimeReady; r.ProviderReady=0; assert(!AdmissionPlatformRuntimeAwaitWork(&c,1000,NULL));
  c.PlatformRuntime=NULL; assert(!AdmissionPlatformRuntimeAwaitWork(&c,1000,&why) && why==1);
  assert(waits+delays==0);
  puts("EXP1014 envelope backpressure: PASS");
}
'''


class EnvelopeBackpressure(unittest.TestCase):
    def test_transient_busy_states_are_waited_out(self):
        text = SRC.read_text()
        accepts = function(text, 'AdmissionPlatformRuntimeAwaitWork')
        self.assertTrue(accepts, 'bounded backpressure for transient busy states is missing')
        ready = function(text, 'AdmissionPlatformRuntimeReadyEx')
        ready = re.sub(r'#if defined\(APPLE_AGX_SUBMIT_QUALIFICATION\).*?#endif\n', '', ready, flags=re.S)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'replay.cpp'; exe = Path(tmp) / 'replay'
            src.write_text(BODY.replace('@@FUNCS@@', ready + '\n' + accepts))
            built = subprocess.run(['clang++', '-std=c++17', '-Wall', '-Wno-unused-function',
                                    '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP1014 envelope backpressure: PASS', ran.stdout)

    def test_envelope_uses_admission_check(self):
        g3 = G3.read_text()
        start = g3.index('static NTSTATUS AdmissionG4SubmitVirtualEnvelope(')
        envelope = g3[start:g3.index('ADMISSION_G4_REJECTS(14u', start)]
        macros = re.findall(r'#define ADMISSION_G4_RUNTIME_READY\(\)(?:[^\n]*\\\n)*[^\n]*', envelope)
        self.assertEqual(len(macros), 2)
        for macro in macros:
            self.assertIn('AdmissionPlatformRuntimeAwaitWork(', macro)


if __name__ == '__main__':
    unittest.main()
