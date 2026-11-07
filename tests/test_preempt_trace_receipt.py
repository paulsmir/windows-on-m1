"""P1 Wom1PreemptTrace: the KMD hooks' attribution and the decoder's layout.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH? A receipt that misattributes
a stamp (a stale DMA_PREEMPTED, a later submit's AwaitWork, a post-commit
retry) or a decoder whose layout drifts from render_preempt_trace.h would
turn the H1/H2/H3 discriminator into a wrong verdict. Version 2: a dispatch
decision or job-thread wake attributed to another fence, thread or event
would misplace the EXP2173 lost pickup between dispatch, KeSetEvent and the
wait return. The helpers are sliced
from backend_platform_windows.c and run against host stubs; the bytes they
produce are decoded by tools/agx_preempt_trace.py.
"""
from pathlib import Path
import importlib.util
import os
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
RENDER = ROOT / 'drivers/apple-agx/render-admission'


def decoder():
    spec = importlib.util.spec_from_file_location(
        'agx_preempt_trace', ROOT / 'tools/agx_preempt_trace.py')
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def helpers():
    source = (RENDER / 'src/backend_platform_windows.c').read_text()
    begin = source.index('/* P1 receipt-only (render_preempt_trace.h). Every hook')
    end = source.index('#endif', source.index('static VOID AdmissionPreemptTracePickup'))
    return source[begin:end]


class PreemptTraceReceiptTests(unittest.TestCase):
    def test_hooks_attribute_stamps_and_decoder_reads_them(self):
        with tempfile.TemporaryDirectory() as directory:
            p = Path(directory)
            (p / 'trace.c').write_text(SHIM + helpers() + CASES)
            subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall',
                '-Wextra', '-Wconversion', '-Wsign-conversion', '-Werror',
                '-Wno-unused-function', '-fsanitize=address,undefined',
                '-I', str(RENDER / 'include'), str(p / 'trace.c'),
                '-o', str(p / 'trace')], check=True)
            subprocess.run([str(p / 'trace'), str(p / 'receipts.txt')],
                           check=True, timeout=10, capture_output=True)
            module = decoder()
            raw = module.registry_value((p / 'receipts.txt').read_bytes())
        self.assertEqual(len(raw), module.BYTES)
        trace = module.parse(raw)
        self.assertEqual(trace['count'], 34)
        self.assertEqual(len(trace['events']), 32)
        # Oldest first: the ring wrapped twice, so event 2 (fence 1020) leads.
        self.assertEqual(trace['events'][0]['PreemptionFence'], 1020)
        self.assertEqual(trace['events'][-1]['PreemptionFence'], 1330)
        last = trace['events'][-1]
        self.assertEqual(last['SubmitFence'], 1335)
        self.assertEqual(last['SubmitPid'], 4242)
        self.assertEqual(last['DroppedFence'], 1326)
        self.assertEqual(last['AwaitFirstReason'], 16)
        s = module.spans(last, trace['hz'])
        self.assertGreater(s['ntfy>cmt'], 0)
        self.assertGreater(s['sub>pick'], 0)
        # v2: dispatch -> KeSetEvent -> wait return -> pickup, in order.
        for column in ('sub>disp', 'disp>set', 'set>wake', 'wake>pick'):
            self.assertGreater(s[column], 0, column)
        self.assertEqual(module.dispatch(last), 'render+subx2')
        self.assertEqual(module.cpus(last), '3>5')
        self.assertEqual(module.dispatch(trace['events'][0]), 'reserved=1019@submitter')
        self.assertEqual(module.cpus(trace['events'][0]), '-')
        self.assertIn('render+subx2 / 3>5 /', self.report(module, trace))

    @staticmethod
    def report(module, trace):
        import io
        out = io.StringIO()
        module.report(trace, out)
        return out.getvalue()

    def test_hooks_are_guarded_and_exported(self):
        header = (RENDER / 'include/render_preempt_trace.h').read_text()
        backend = (RENDER / 'src/backend_platform_windows.c').read_text()
        self.assertIn('#define ADMISSION_PREEMPT_TRACE_ON 1', header)
        self.assertIn('L"Wom1PreemptTrace"', backend)
        # Off unless Wom1JobDiagnostics: every entry point checks the switch.
        self.assertEqual(helpers().count('!runtime->JobDiagnostics'), 2)


SHIM = r'''
/* Host stubs for the sliced AdmissionPreemptTrace* helpers. */
#include <stdint.h>
#include <string.h>
#include <stdio.h>
#include <assert.h>
typedef void VOID;
typedef unsigned char BOOLEAN;
typedef uint32_t ULONG, UINT;
typedef int32_t LONG;
typedef int64_t LONG64, LONGLONG;
typedef uint64_t ULONGLONG;
typedef void *PVOID;
typedef unsigned char KIRQL;
typedef union { LONGLONG QuadPart; } LARGE_INTEGER;
#define TRUE 1
#define FALSE 0
#define PASSIVE_LEVEL 0
#define C_ASSERT(e) _Static_assert(e, #e)
#define RtlZeroMemory(p, n) memset((p), 0, (n))
#define APPLE_AGX_GPUVA_G3_QUALIFICATION 1
static KIRQL g_irql;
static int g_threads[2], g_cur;
static ULONG g_cpu;
static LONGLONG g_qpc = 100;
static KIRQL KeGetCurrentIrql(void) { return g_irql; }
static PVOID KeGetCurrentThread(void) { return &g_threads[g_cur]; }
static ULONG KeGetCurrentProcessorNumberEx(PVOID n) { (void)n; return g_cpu; }
static LARGE_INTEGER KeQueryPerformanceCounter(LARGE_INTEGER *f) { LARGE_INTEGER r; if (f) f->QuadPart = 24000000; r.QuadPart = g_qpc++; return r; }
static LONG InterlockedCompareExchange(volatile LONG *p, LONG v, LONG c) { LONG o = *p; if (o == c) *p = v; return o; }
static LONG64 InterlockedCompareExchange64(volatile LONG64 *p, LONG64 v, LONG64 c) { LONG64 o = *p; if (o == c) *p = v; return o; }
static LONG InterlockedExchange(volatile LONG *p, LONG v) { LONG o = *p; *p = v; return o; }
static LONG InterlockedIncrement(volatile LONG *p) { return ++*p; }
static LONG InterlockedOr(volatile LONG *p, LONG v) { LONG o = *p; *p |= v; return o; }
static LONG InterlockedExchangeAdd(volatile LONG *p, LONG v) { LONG o = *p; *p += v; return o; }
typedef enum { AppleAgxPreemptionIdle = 0, AppleAgxPreemptionWaitCurrentBoundary, AppleAgxPreemptionReadyToNotify, AppleAgxPreemptionNotificationClaimed } APPLE_AGX_PREEMPTION_PHASE;
typedef struct { APPLE_AGX_PREEMPTION_PHASE PreemptionPhase; } APPLE_AGX_SCHEDULER;
static APPLE_AGX_PREEMPTION_PHASE AppleAgxSchedulerPreemptionPhase(const APPLE_AGX_SCHEDULER *s) { return s->PreemptionPhase; }
#define ADMISSION_G4_PENDING_CAPACITY 8u
typedef struct { ULONG Fence; } ADMISSION_G4_PENDING;
typedef struct _ADMISSION_CONTEXT {
  PVOID PlatformRuntime;
  volatile LONG PagingPending, PagingDpcPending, PagingDpcsActive;
  ULONG DispatchedFence, CpuQueueCount, G4PendingHead, G4PendingCount;
  ADMISSION_G4_PENDING G4Pending[ADMISSION_G4_PENDING_CAPACITY];
  APPLE_AGX_SCHEDULER Scheduler;
} ADMISSION_CONTEXT;
static int g_worker;
static BOOLEAN AdmissionPlatformRenderWorkerScheduled(ADMISSION_CONTEXT *c) { (void)c; return (BOOLEAN)g_worker; }
#include "render_preempt_trace.h"
typedef struct {
  ADMISSION_CONTEXT *Adapter;
  BOOLEAN JobDiagnostics;
  ADMISSION_PREEMPT_TRACE PreemptTrace;
  ADMISSION_PREEMPT_TRACE PreemptTraceSnapshot;
  volatile LONG PreemptTraceCurrent, PreemptTraceSequence;
  PVOID PreemptTraceCommandThread[ADMISSION_PREEMPT_TRACE_CAPACITY];
  PVOID PreemptTraceSubmitThread[ADMISSION_PREEMPT_TRACE_CAPACITY];
  volatile LONG PreemptTraceWakePending;
} ADMISSION_PLATFORM_RUNTIME;
static ULONGLONG AdmissionJobQpc(VOID) { return (ULONGLONG)KeQueryPerformanceCounter(NULL).QuadPart; }
'''

CASES = r'''
int main(int argc, char **argv) {
  static ADMISSION_PLATFORM_RUNTIME rt;
  static ADMISSION_CONTEXT ctx;
  ADMISSION_PREEMPT_EVENT *e;
  int i;
  ctx.PlatformRuntime = &rt; rt.Adapter = &ctx;
  rt.PreemptTrace.Version = ADMISSION_PREEMPT_TRACE_VERSION; rt.PreemptTrace.Bytes = sizeof(rt.PreemptTrace); rt.PreemptTrace.QpcFrequency = 24000000;
  /* off: nothing */
  AdmissionPreemptTraceEnter(&ctx, 5);
  assert(rt.PreemptTrace.Count == 0);
  rt.JobDiagnostics = TRUE;
  for (i = 0; i < 34; ++i) {
    ULONG f = 1000u + (ULONG)i * 10u;
    g_irql = 2;
    AdmissionPreemptTraceEnter(&ctx, f);
    ctx.G4PendingCount = 1; ctx.G4Pending[0].Fence = f - 1u;
    AdmissionPreemptTraceBeginLocked(&ctx, f, f - 2u, f - 3u, f - 4u);
    ctx.Scheduler.PreemptionPhase = AppleAgxPreemptionWaitCurrentBoundary;
    AdmissionPreemptTraceDecideLocked(&ctx, FALSE);
    AdmissionPreemptTraceExit(&ctx, f);
    g_worker = 1;
    g_irql = 0;
    AdmissionPreemptTraceRetry(&ctx, ADMISSION_PREEMPT_RETRY_WORKER);
    AdmissionPreemptTraceDefer(&ctx, 0u, ADMISSION_PREEMPT_WHY_BUSY);
    g_irql = 2;
    AdmissionPreemptTraceRetry(&ctx, ADMISSION_PREEMPT_RETRY_DPC);
    AdmissionPreemptTraceDefer(&ctx, 0u, ADMISSION_PREEMPT_WHY_PHASE(AppleAgxPreemptionNotificationClaimed));
    AdmissionPreemptTraceClaim(&ctx, 0u, f);
    AdmissionPreemptTraceNotified(&ctx, 0u, f, f - 3u);
    AdmissionPreemptTraceNotified(&ctx, 0u, f + 1u, 0u); /* stale: ignored */
    g_irql = 0;
    AdmissionPreemptTraceSubmit(&ctx, 4242u, f + 5u, FALSE);
    AdmissionPreemptTraceSubmitPath(&ctx, f + 5u, FALSE);
    AdmissionPreemptTraceAwaitEnter(&ctx);
    AdmissionPreemptTraceAwaitExit(&ctx, 7, 16u, 0u);
    AdmissionPreemptTraceCommitted(&ctx, 0u, f);
    AdmissionPreemptTraceDefer(&ctx, 0u, ADMISSION_PREEMPT_DEFER_FAULTED); /* after commit: ignored */
    AdmissionPreemptTraceSubmit(&ctx, 1u, f - 4u, TRUE);
    AdmissionPreemptTraceAwaitEnter(&ctx); /* another submit's: ignored */
    /* v2: the job thread (thread 1) consumes no armed wake yet. */
    g_cur = 1;
    AdmissionPreemptTraceWoke(&rt, AdmissionJobQpc());
    if (i == 2) {
      /* Event 2 (oldest kept): only the submit thread's refused decision. */
      g_cur = 0;
      AdmissionPreemptTraceDispatch(&ctx, AdmissionPreemptTraceNow(&ctx), f - 1u, f - 1u,
          ADMISSION_PREEMPT_DISPATCH_RESERVED);
      g_cur = 1;
    } else {
      /* Another head on another thread: not this submit's decision. */
      AdmissionPreemptTraceDispatch(&ctx, AdmissionPreemptTraceNow(&ctx), f - 1u, 0u,
          ADMISSION_PREEMPT_DISPATCH_RENDER);
      AdmissionPreemptTraceDispatch(&ctx, AdmissionPreemptTraceNow(&ctx), f + 5u, 0u,
          ADMISSION_PREEMPT_DISPATCH_RENDER | ADMISSION_PREEMPT_DISPATCH_SUBMITTED);
      AdmissionPreemptTraceDispatch(&ctx, AdmissionPreemptTraceNow(&ctx), f + 5u, f + 5u,
          ADMISSION_PREEMPT_DISPATCH_RESERVED); /* counted, not recorded */
      g_cpu = 3;
      AdmissionPreemptTraceWakeSet(&ctx, f + 4u); /* another fence: ignored */
      AdmissionPreemptTraceWakeSet(&ctx, f + 5u);
      AdmissionPreemptTraceWakeSet(&ctx, f + 5u); /* first wins */
      g_cpu = 5;
      AdmissionPreemptTraceWoke(&rt, AdmissionJobQpc());
      g_cpu = 6;
      AdmissionPreemptTraceWoke(&rt, AdmissionJobQpc()); /* consumed: ignored */
    }
    g_cur = 0;
    AdmissionPreemptTracePickup(&rt, f + 5u);
    /* After the pickup nothing more is attributed to SubmitFence. */
    AdmissionPreemptTraceDispatch(&ctx, AdmissionPreemptTraceNow(&ctx), f + 5u, 0u,
        ADMISSION_PREEMPT_DISPATCH_EMPTY);
  }
  /* A wake armed for an older event is not stamped on the newest. */
  rt.PreemptTraceWakePending = 1;
  AdmissionPreemptTraceWoke(&rt, AdmissionJobQpc());
  assert(rt.PreemptTrace.Events[1].WakeCpu == 5u);
  rt.JobDiagnostics = FALSE;
  assert(AdmissionPreemptTraceNow(&ctx) == 0u);
  rt.JobDiagnostics = TRUE;
  assert(rt.PreemptTrace.Count == 34 && rt.PreemptTrace.Next == 2);
  e = &rt.PreemptTrace.Events[1];
  assert(e->PreemptionFence == 1330u && e->DroppedFence == 1326u && e->PendingDropped == 1u);
  assert(e->DeferMask == (ADMISSION_PREEMPT_DEFER_CMD_PHASE | ADMISSION_PREEMPT_DEFER_WORKER | ADMISSION_PREEMPT_DEFER_CLAIMED));
  assert(e->DeferCount == 3u && e->RetryCount == 0x101u);
  assert((e->ClaimSource & 0xffu) == ADMISSION_PREEMPT_CLAIM_DISPATCH);
  assert(e->NotifyLastCompletedFence == 1327u);
  assert(e->SubmitFence == 1335u && e->SubmitPid == 4242u && e->SubmitFlags == ADMISSION_PREEMPT_SUBMIT_IMMEDIATE);
  assert(e->SubmitCount == 2u && e->SubmitOrdinal == 1u && e->ResubmitFence == 1326u);
  assert(e->AwaitEnterQpc < e->AwaitExitQpc && e->AwaitWaitQpc == 7u && e->AwaitFirstReason == 16u);
  assert(e->EnterQpc < e->ExitQpc && e->ExitQpc < e->ClaimQpc && e->ClaimQpc < e->NotifyQpc);
  assert(e->NotifyQpc < e->CommitQpc && e->CommitQpc < e->PickupQpc && e->FirstPickupFence == 1335u);
  assert(e->DispatchOutcome == (ADMISSION_PREEMPT_DISPATCH_RENDER | ADMISSION_PREEMPT_DISPATCH_SUBMITTED));
  assert(e->DispatchHeadFence == 1335u && e->DispatchReservedFence == 0u && e->DispatchCount == 2u);
  assert(e->SubmitQpc < e->DispatchQpc && e->DispatchQpc < e->WakeSetQpc);
  assert(e->WakeSetQpc < e->WakeQpc && e->WakeQpc < e->PickupQpc);
  assert(e->WakeSetCpu == 3u && e->WakeCpu == 5u && rt.PreemptTraceWakePending == 0);
  e = &rt.PreemptTrace.Events[2];
  assert(e->PreemptionFence == 1020u && e->DispatchCount == 1u && e->WakeSetQpc == 0u && e->WakeQpc == 0u);
  assert(e->DispatchOutcome == (ADMISSION_PREEMPT_DISPATCH_RESERVED | ADMISSION_PREEMPT_DISPATCH_SUBMITTER));
  if (argc > 1) {
    FILE *f = fopen(argv[1], "w");
    const unsigned char *b = (const unsigned char *)&rt.PreemptTrace;
    size_t k;
    fprintf(f, "Wom1JobDiagnostics = 1\nWom1PreemptTrace ");
    for (k = 0; k < sizeof(rt.PreemptTrace); ++k) fprintf(f, "%02x", b[k]);
    fprintf(f, "\n");
    fclose(f);
  }
  printf("ok %zu\n", sizeof(ADMISSION_PREEMPT_TRACE));
  return 0;
}
'''


if __name__ == '__main__':
    unittest.main()
