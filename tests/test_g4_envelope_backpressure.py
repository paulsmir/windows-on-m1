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
- EXP2315 (GravityMark control arm, device removed at 26 s; Wom1G4SubmitFailure
  branch 7 subsite 0x10D kind 4, one failure, no wait near the bound, runtime
  healthy afterwards): the backend phase was read twice, once by the readiness
  test (Submitted, so predicate 4) and again to decide whether predicate 4 is
  transient. A job that completed between the two reads (Submitted -> Ready)
  made a healthy runtime look like a non-transient refusal. A phase that is
  Ready on the second look is retested, never refused;
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
/* Every read of the phase is observable: flip_on_read models the worker
 * completing the job between two reads of the submitter. */
static int phase_reads, flip_on_read;
struct PHASE { int v; PHASE(int x=0):v(x){} PHASE &operator=(int x){ v=x; return *this; }
  operator int(){ int seen=v; if(flip_on_read && ++phase_reads==flip_on_read) v=3 /* Ready */; return seen; } };
struct BACKEND { PHASE Phase; };
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
/* F-A2: the admissibility predicate of a waiting submission. */
typedef void *PVOID;
typedef BOOLEAN (*ADMISSION_AWAIT_ADMISSIBLE)(ADMISSION_CONTEXT *, PVOID);
static int admit_after, admit_calls, admit_clears;
static BOOLEAN admit(ADMISSION_CONTEXT *c, PVOID arg){ assert(c==ctx && arg==(PVOID)&admit_after);
  ++admit_calls; admit_clears=clears; return admit_after && admit_calls>=admit_after; }
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
  /* F-A2: with a predicate the wait also ends when the submission became
   * admissible, although the job still runs and the slot is not empty; it
   * is tested on every pass, after the event was cleared (a signal after the
   * test is not lost), and the wait is still counted. */
  { BOOLEAN admitted=FALSE; LONG counted=c.SubmitPath.Waits[0];
    waits=slot_waits=0; admit_calls=0; admit_after=3; finish_after=0; now=0;
    r.WorkScheduled=1; r.Backend.Phase=AppleAgxBackendRuntimeSubmitted; c.RenderPacket.State=AdmissionRenderPacketActive;
    { int clears_before=clears;
      if(!AdmissionPlatformRuntimeAwaitWorkEx(&c,1000,&why,admit,&admit_after,&admitted)){printf("admissible submission kept waiting (%lu)\n",why);return 1;}
      assert(admitted && why==0 && admit_calls==3 && slot_waits==2 && clears==clears_before+3 && admit_clears==clears); }
    assert(r.Backend.Phase==AppleAgxBackendRuntimeSubmitted && c.SubmitPath.Waits[0]==counted+1);
    /* admissible at once: no wait at all */
    admitted=FALSE; waits=0; admit_calls=0; admit_after=1; counted=c.SubmitPath.Waits[0];
    assert(AdmissionPlatformRuntimeAwaitWorkEx(&c,1000,&why,admit,&admit_after,&admitted) && admitted && waits==0 && c.SubmitPath.Waits[0]==counted);
    /* never admissible: "ready and empty" ends the wait as without a predicate */
    admitted=TRUE; waits=0; admit_calls=0; admit_after=0; finish_after=2;
    assert(AdmissionPlatformRuntimeAwaitWorkEx(&c,1000,&why,admit,&admit_after,&admitted) && !admitted && why==0 && waits==2 && admit_calls==3);
    /* a non-transient refusal is not waited on, predicate or not */
    admitted=TRUE; waits=0; admit_calls=0; r.Stopping=1;
    assert(!AdmissionPlatformRuntimeAwaitWorkEx(&c,1000,&why,admit,&admit_after,&admitted) && !admitted && why==6 && waits==0);
    r.Stopping=0;
    /* the bound still holds */
    admitted=TRUE; r.WorkScheduled=1; r.Backend.Phase=AppleAgxBackendRuntimeSubmitted; finish_after=0; now=0;
    assert(!AdmissionPlatformRuntimeAwaitWorkEx(&c,50,&why,admit,&admit_after,&admitted) && !admitted && why==4);
    r.WorkScheduled=0; r.Backend.Phase=AppleAgxBackendRuntimeReady; c.RenderPacket.State=AdmissionRenderPacketEmpty;
    /* no predicate: the old entry point is the same loop */
    assert(AdmissionPlatformRuntimeAwaitWork(&c,1000,&why) && why==0); }
  /* EXP2315: the job completes between the readiness test's read of the
   * phase (Submitted) and the next read. Admitted, not refused. */
  r.WorkScheduled=0; r.Backend.Phase=AppleAgxBackendRuntimeSubmitted; c.RenderPacket.State=AdmissionRenderPacketEmpty;
  c.G4PendingCount=0; finish_after=0; now=0; phase_reads=0; flip_on_read=1;
  if(!AdmissionPlatformRuntimeAwaitWork(&c,1000,&why)){printf("job completing between two phase reads refused the next submission (%lu)\n",why);return 1;}
  assert(why==0); flip_on_read=0;
  /* a failed backend is still refused at once */
  waits=0; r.Backend.Phase=AppleAgxBackendRuntimeFailed; assert(!AdmissionPlatformRuntimeAwaitWork(&c,1000,&why) && why==4 && waits==0);
  r.Backend.Phase=AppleAgxBackendRuntimeReady;
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
        accepts = function(text, 'AdmissionPlatformRuntimeAwaitWorkEx') + '\n' + \
            function(text, 'AdmissionPlatformRuntimeAwaitWork')
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
        # F-A2: both go through the one helper, which is the old wait
        # unless Wom1SubmitRequeue is set
        for macro in macros:
            self.assertIn('AdmissionG4SubmitAwait(adapter, context,', macro)
        helper = g3[g3.index('static BOOLEAN AdmissionG4SubmitAwait('):]
        helper = helper[:helper.index('\n}\n')]
        self.assertIn('if (adapter->SubmitRequeue == 0u)\n'
                      '    return AdmissionPlatformRuntimeAwaitWork(adapter, 10000u, FailedPredicate);', helper)


if __name__ == '__main__':
    unittest.main()
