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
- EXP1120 analysis: every wait is on the runtime's slot event, cleared before
  the test and set when the slot frees or the worker finishes; a job queued
  behind a concurrent submitter waited ~10 ms in a timer sleep (9.7-10.5 ms
  Notify -> next Kick in EXP1118/EXP1119). No timer sleep remains;
- a busy state that outlives the timeout is refused with its predicate;
- stopping, resetting, failed or missing runtimes are refused without waiting;
- TDR stage 2b: while a firmware restart owns the runtime (FwRestartActive)
  its refusals (resetting, backend not started) are transient: a submission
  that arrives during the restart is admitted when the restart ends, is
  refused at once with the real predicate when the restart failed, and never
  waits past the bound. Without this, DWM's submission during the H1 drill
  fails and the run cannot tell that from a firmware that did not come back;
- Phase 5a: queued (deferred-bind) submissions keep the slot busy;
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
  volatile LONG Stopping, Resetting, WorkScheduled; KEVENT WorkIdle, SlotEvent;
  volatile LONG FwRestartActive; };
struct PACKET { int State; };
typedef long long LONG64, LONGLONG;
struct SUBMIT_PATH { volatile LONG Waits[4]; volatile LONG64 WaitQpc[4]; volatile LONG64 MaxWaitQpc; };
struct ADMISSION_CONTEXT { void *PlatformRuntime; int SchedulerLock; PACKET RenderPacket; ULONG G4PendingCount; SUBMIT_PATH SubmitPath; };
static LONG InterlockedIncrement(volatile LONG *p){ return ++*p; }
static LONG64 InterlockedExchangeAdd64(volatile LONG64 *p, LONG64 v){ LONG64 o=*p; *p+=v; return o; }
static LONG64 InterlockedCompareExchange64(volatile LONG64 *p, LONG64 x, LONG64 c){ LONG64 o=*p; if(o==c)*p=x; return o; }
static ULONGLONG now; static int waits, delays, finish_after, restart_after, restart_fails;
static ADMISSION_PLATFORM_RUNTIME *rt; static ADMISSION_CONTEXT *ctx;
static ULONGLONG KeQueryInterruptTime(void){ return now; }
static LARGE_INTEGER KeQueryPerformanceCounter(void*){ LARGE_INTEGER q; q.QuadPart=(long long)now+1; return q; }
static KIRQL KeGetCurrentIrql(void){ return PASSIVE_LEVEL; }
static void KeAcquireSpinLock(int*, KIRQL*){} static void KeReleaseSpinLock(int*, KIRQL){}
static int AdmissionRenderPacketState(PACKET *p){ return p->State; }
static void tick(){ now += 100000ULL; if(finish_after && --finish_after==0){ rt->WorkScheduled=0;
  rt->Backend.Phase=AppleAgxBackendRuntimeReady; ctx->RenderPacket.State=AdmissionRenderPacketEmpty;
  ctx->G4PendingCount=0; }
  /* AdmissionPlatformRuntimeRestart's exit: ownership returned, backend back (or not). */
  if(restart_after && --restart_after==0){ rt->FwRestartActive=0; rt->Resetting=0;
    rt->BackendStarted=restart_fails?0:1; } }
static int slot_waits, clears;
static void KeClearEvent(KEVENT*){ ++clears; }
static NTSTATUS KeWaitForSingleObject(KEVENT *e, int, int, BOOLEAN, LARGE_INTEGER*){
  ++waits; if(e==&rt->SlotEvent) ++slot_waits; tick(); return 0; }
static NTSTATUS KeDelayExecutionThread(int, BOOLEAN, LARGE_INTEGER*){ ++delays; tick(); return 0; }
@@FUNCS@@
int main(){
  ADMISSION_PLATFORM_RUNTIME r={1,1,{AppleAgxBackendRuntimeReady},(void*)1,0,0,0,{0}};
  ADMISSION_CONTEXT c={&r,0,{AdmissionRenderPacketEmpty},0,{}}; rt=&r; ctx=&c; ULONG why=99;
  assert(AdmissionPlatformRuntimeAwaitWork(&c,1000,&why) && why==0 && waits+delays==0);
  /* Worker returning after the fence notification. */
  r.WorkScheduled=1; finish_after=3;
  if(!AdmissionPlatformRuntimeAwaitWork(&c,1000,&why)){printf("returning worker refused new work (%lu)\n",why);return 1;}
  assert(why==0 && waits==3);
  /* VidSch submits while the previous job still runs. */
  waits=0; r.WorkScheduled=1; r.Backend.Phase=AppleAgxBackendRuntimeSubmitted; c.RenderPacket.State=AdmissionRenderPacketActive; finish_after=5;
  if(!AdmissionPlatformRuntimeAwaitWork(&c,1000,&why)){printf("running job refused next submission (%lu)\n",why);return 1;}
  /* Render slot busy with an idle worker (a concurrent submitter holds it):
   * wait on the slot event, which the slot's release sets, not a timer. */
  waits=delays=slot_waits=0; c.RenderPacket.State=AdmissionRenderPacketActive; finish_after=2;
  assert(AdmissionPlatformRuntimeAwaitWork(&c,1000,&why) && slot_waits==2 && delays==0);
  /* Phase 5a: an empty slot with queued submissions is still busy; an
   * immediate bind must not overtake them. */
  waits=delays=slot_waits=0; c.G4PendingCount=1; finish_after=2;
  assert(AdmissionPlatformRuntimeAwaitWork(&c,1000,&why) && slot_waits==2 && why==0);
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
  c.PlatformRuntime=&r; r.ProviderReady=1;
  /* A firmware restart owns the runtime: the submission waits it out. */
  waits=0; r.Resetting=1; r.FwRestartActive=1; r.BackendStarted=0; restart_after=4; restart_fails=0;
  if(!AdmissionPlatformRuntimeAwaitWork(&c,1000,&why)){printf("submission during a firmware restart refused (%lu)\n",why);return 1;}
  assert(why==0 && waits==4);
  /* The restart failed: refused with the real predicate as soon as it ends. */
  waits=0; r.Resetting=1; r.FwRestartActive=1; r.BackendStarted=0; restart_after=2; restart_fails=1;
  assert(!AdmissionPlatformRuntimeAwaitWork(&c,1000,&why) && why==3 && waits==2);
  /* A restart outliving the bound is refused, not waited on for ever. */
  r.Resetting=1; r.FwRestartActive=1; r.BackendStarted=1; restart_after=0; now=0;
  assert(!AdmissionPlatformRuntimeAwaitWork(&c,50,&why) && why==7);
  r.Resetting=0; r.FwRestartActive=0;
  /* EXP1135: every wait is counted under its first busy predicate. */
  assert(c.SubmitPath.Waits[1]>=1 && c.SubmitPath.Waits[0]>=1 && c.SubmitPath.Waits[2]>=1);
  assert(c.SubmitPath.WaitQpc[1]>0 && c.SubmitPath.MaxWaitQpc>0);
  /* Every wait was on the slot event, each preceded by a clear. */
  assert(slot_waits>0 && clears>=slot_waits);
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
