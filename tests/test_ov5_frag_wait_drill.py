"""OV5 step 1: the 3D-queue compute-stamp Barrier drill (Wom1Ov5FragWaitDrill).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The drill adds one firmware-visible ring entry. A wrong entry is a GPU hang
or a stale completion, and the switch must leave every other job alone:
  D1 switch off: the provider publishes exactly what the pre-OV5 provider
     published (ring words, write pointers, flushed ranges, run messages,
     expected done pointers): a golden trace taken with this probe on the
     base commit 92dfda88f (integration/ad04-windows-compiler);
  D2 switch on, successor render without compute behind a running compute
     job: the 3D publication leads with the drill Barrier (its prepared
     range flushed first), the 3D done pointer advances by one more entry,
     the TA keeps its lead Barrier first, nothing else moves;
  D3 the provider refuses (publishes nothing) a 3D lead Barrier on a depth-1
     job, on a render with compute work, or without a TA lead Barrier;
  D4 the KMD decision places it only for a successor, TA_ON_COMPUTE, no
     compute work, below the ring-entry limit and with an in-ring done
     pointer; the receipt counts built / refused per reason;
  D5 the sidecar slot (0x8840 + set delta) overlaps no other sidecar word
     of either set and the encoder writes the G13 Barrier there;
  D6 KMD wiring: default-off switch read once at start, built only in the
     successor branches after the TA lead Barrier, receipt exported without
     a flush at most every two seconds.
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
SHARED = ROOT / 'drivers/apple-agx/shared'
RENDER = ROOT / 'drivers/apple-agx/render-admission'
WORKER = RENDER / 'src/backend_platform_windows.c'


def build_run(code, sources, includes, defines=(), args=()):
    with tempfile.TemporaryDirectory(prefix='ov5-') as tmp:
        src = Path(tmp) / 'probe.c'
        src.write_text(code)
        exe = Path(tmp) / 'probe'
        cmd = [os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
               '-ftrivial-auto-var-init=pattern', '-fsanitize=address,undefined',
               *(f'-D{d}' for d in defines), *(f'-I{p}' for p in includes),
               str(src), *map(str, sources), '-o', str(exe)]
        built = subprocess.run(cmd, capture_output=True, text=True)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)
        return subprocess.run([str(exe), *args], capture_output=True, text=True, timeout=60)


# The real provider and runtime: a compute job N runs, then a successor
# render N+1 (no compute work) is kicked behind it (TA_ON_COMPUTE). The
# submission builder stands in for the platform's (sidecar range for the
# lead Barriers). OV5_NEW is defined only for the current tree; without it
# the probe compiles against the pre-OV5 headers (golden capture).
PROVIDER_PROBE = r'''
#include <stdio.h>
#include <string.h>
#include "apple_agx_g13_queue_provider.h"
#define SIDE_VA 0x1500000000ULL
static APPLE_AGX_BACKEND_U64 ring[3][64];
static volatile APPLE_AGX_BACKEND_U32 wp[3], dp[3], st[3];
static unsigned char sidecar[0x10000], ta_work[2][0x40], d3_work[2][0x40];
static char trace[8192]; static size_t used;
#define LOG(...) (used += (size_t)snprintf(trace + used, sizeof(trace) - used, __VA_ARGS__))
static const char *tag(const void *a) {
  static char b[32];
  if ((const unsigned char *)a >= sidecar && (const unsigned char *)a < sidecar + sizeof(sidecar))
    snprintf(b, sizeof(b), "side+%lx", (unsigned long)((const unsigned char *)a - sidecar));
  else if (a == ta_work[0] || a == ta_work[1]) snprintf(b, sizeof(b), "ta%d", a == ta_work[1]);
  else if (a == d3_work[0] || a == d3_work[1]) snprintf(b, sizeof(b), "d3%d", a == d3_work[1]);
  else {
    unsigned q, i;
    for (q = 0; q < 3; ++q) for (i = 0; i < 64; ++i)
      if (a == &ring[q][i]) { snprintf(b, sizeof(b), "ring%u[%u]", q, i); return b; }
    snprintf(b, sizeof(b), "other");
  }
  return b;
}
static APPLE_AGX_BACKEND_BOOL fl(void *c, const void *a, APPLE_AGX_BACKEND_U32 b) {
  (void)c; LOG("flush %s %x\n", tag(a), b); return 1; }
static void ba(void *c) { (void)c; }
static APPLE_AGX_BACKEND_BOOL pu(void *c, volatile APPLE_AGX_BACKEND_U32 *a, APPLE_AGX_BACKEND_U32 v) {
  (void)c; *a = v; LOG("publish wp%d=%u\n", (int)(a - wp), v); return 1; }
static APPLE_AGX_BACKEND_BOOL rd(void *c, const volatile APPLE_AGX_BACKEND_U32 *a, APPLE_AGX_BACKEND_U32 *v) {
  (void)c; *v = *a; return 1; }
static APPLE_AGX_BACKEND_BOOL se(void *c, APPLE_AGX_BACKEND_U32 q, const unsigned char m[APPLE_AGX_G13_RUN_MESSAGE_SIZE]) {
  unsigned i; (void)c; LOG("run q%u", q);
  for (i = 0; i < APPLE_AGX_G13_RUN_MESSAGE_SIZE; ++i) LOG("%02x", m[i]);
  LOG("\n"); return 1; }
static APPLE_AGX_BACKEND_BOOL qu(void *c, APPLE_AGX_BACKEND_U32 f) { (void)c; (void)f; return 1; }
static APPLE_AGX_BACKEND_BOOL bu(void *c, const APPLE_AGX_BACKEND_JOB_IMAGE *j, APPLE_AGX_BACKEND_U32 f,
                                 APPLE_AGX_G13_QUEUE_RUNTIME_SUBMISSION *s) {
  unsigned i; (void)c; (void)f;
  s->NowTicks = 1000ULL;
  if (j->ComputeWorkAddressCount) {
    s->Compute.GpuAddresses[0] = j->ComputeWorkAddresses[0]; s->Compute.GpuAddressCount = 1;
    s->Compute.PreparedRanges[0].Address = sidecar; s->Compute.PreparedRanges[0].Bytes = 0x10000;
    s->Compute.PreparedRangeCount = 1; s->Compute.ExpectedStamp = j->ComputeExpectedStamp;
  }
  if (j->Kind != AppleAgxBackendJobCompute) {
    for (i = 0; i < 2; ++i) {
      s->Ta.GpuAddresses[i] = j->TaWorkAddresses[i];
      s->Ta.PreparedRanges[i].Address = ta_work[i]; s->Ta.PreparedRanges[i].Bytes = 0x40;
      s->D3.GpuAddresses[i] = j->D3WorkAddresses[i];
      s->D3.PreparedRanges[i].Address = d3_work[i]; s->D3.PreparedRanges[i].Bytes = 0x40;
    }
    s->Ta.GpuAddressCount = s->Ta.PreparedRangeCount = 2; s->Ta.ExpectedStamp = j->TaExpectedStamp;
    s->D3.GpuAddressCount = s->D3.PreparedRangeCount = 2; s->D3.ExpectedStamp = j->D3ExpectedStamp;
  }
  if (j->LeadBarrierAddress) { s->LeadBarrierRange.Address = sidecar; s->LeadBarrierRange.Bytes = 0x10000; }
#ifdef OV5_NEW
  if (j->D3LeadBarrierAddress) { s->D3LeadBarrierRange.Address = sidecar; s->D3LeadBarrierRange.Bytes = 0x10000; }
#endif
  return 1;
}
static APPLE_AGX_G13_QUEUE_PROVIDER p;
static APPLE_AGX_BACKEND_IO q;
static int setup(void) {
  static APPLE_AGX_G13_QUEUE_RUNTIME_CONFIG c;
  static APPLE_AGX_G13_QUEUE_RUNTIME_IO io;
  static APPLE_AGX_G13_QUEUE_PROVIDER_IO pio;
  unsigned i;
  memset(&p, 0, sizeof(p)); memset(&c, 0, sizeof(c)); memset(&io, 0, sizeof(io));
  memset(&pio, 0, sizeof(pio)); memset(&q, 0, sizeof(q)); memset(ring, 0, sizeof(ring));
  for (i = 0; i < 3; ++i) {
    APPLE_AGX_G13_QUEUE_BINDING *b = i == 0 ? &c.Ta : i == 1 ? &c.D3 : &c.Compute;
    b->QueueType = i; b->QueueInfoGpuAddress = 0x1000ULL * (i + 1); b->RingCpuAddress = ring[i];
    b->RingCapacity = 64u; b->CpuWritePointer = &wp[i]; b->GpuDonePointer = &dp[i];
    b->Stamp = &st[i]; b->EventNumber = 7u + i;
    wp[i] = dp[i] = 5u + i; st[i] = 0;
  }
  c.HasCompute = 1; c.TimeoutTicks = 1000ULL;
  io.FlushForDevice = fl; io.MemoryBarrier = ba; io.PublishU32 = pu; io.ReadU32 = rd;
  io.SendRunMessage = se; io.Quiesce = qu;
  pio.Context = &p; pio.BuildSubmission = bu;
  if (!AppleAgxG13QueueProviderInitialize(&p, &c, &io, &pio)) return 0;
  q.Context = &p;
  if (!AppleAgxG13QueueProviderInstall(&q) || !q.Queues.Create(&p)) return 0;
  /* the queues already ran a render (InitBM and NewQueue done) */
  p.Runtime.BufferManagerInitialized = 1; p.Runtime.TaFirstRun = 0; p.Runtime.D3FirstRun = 0;
  return 1;
}
static APPLE_AGX_BACKEND_JOB_IMAGE render(const APPLE_AGX_G13_QUEUE_JOB_PLAN *plan) {
  APPLE_AGX_BACKEND_JOB_IMAGE j;
  memset(&j, 0, sizeof(j));
  j.Kind = AppleAgxBackendJobRender;
  j.TaWorkAddresses[0] = 0x2000000000ULL; j.TaWorkAddresses[1] = 0x2000000100ULL;
  j.D3WorkAddresses[0] = 0x3000000000ULL; j.D3WorkAddresses[1] = 0x3000000100ULL;
  j.TaWorkAddressCount = j.D3WorkAddressCount = 2;
  j.TaEvent = 7; j.D3Event = 8; j.TaExpectedStamp = 0x4100; j.D3ExpectedStamp = 0x4200;
  j.TaExpectedDonePointer = plan->TaExpectedDonePointer;
  j.D3ExpectedDonePointer = plan->D3ExpectedDonePointer;
  j.LeadBarrierAddress = SIDE_VA + 0xa800u; /* set B TA lead Barrier */
  return j;
}
/* mode: off | drill | compute | nolead | depth1 */
int main(int argc, char **argv) {
  const char *mode = argc > 1 ? argv[1] : "off";
  APPLE_AGX_G13_QUEUE_JOB_PLAN plan;
  APPLE_AGX_BACKEND_JOB_IMAGE n, r;
  unsigned i, ok;
  if (!setup()) return 2;
  if (!strcmp(mode, "depth1")) {
#ifdef OV5_NEW
    memset(&plan, 0, sizeof(plan));
    if (!AppleAgxG13QueueProviderPlanJob(&p, &plan)) return 3;
    r = render(&plan); r.LeadBarrierAddress = 0; r.D3LeadBarrierAddress = SIDE_VA + 0x8840u;
    printf("depth1_plan_d3_barrier_done %u\n", plan.D3BarrierExpectedDonePointer);
    ok = q.Queues.RunTa(&p, &r, 77);
    printf("depth1 ok=%u guard=%u wp=%u,%u\n", ok, p.LastSubmitGuard, wp[0], wp[1]);
#endif
    return 0;
  }
  memset(&plan, 0, sizeof(plan));
  if (!AppleAgxG13QueueProviderPlanJob(&p, &plan)) return 3;
  memset(&n, 0, sizeof(n));
  n.Kind = AppleAgxBackendJobCompute; n.ComputeWorkAddresses[0] = SIDE_VA + 0x8000u;
  n.ComputeWorkAddressCount = 1; n.ComputeEvent = 9; n.ComputeExpectedStamp = 0x300;
  n.ComputeExpectedDonePointer = plan.ComputeExpectedDonePointer;
  if (!q.Queues.RunCompute(&p, &n, 76)) { printf("compute guard=%u rr=%u\n", p.LastSubmitGuard, p.LastSubmitRuntimeResult); return 4; }
  used = 0; trace[0] = 0;
  memset(&plan, 0, sizeof(plan));
  if (!AppleAgxG13QueueProviderPlanSuccessor(&p, &plan)) return 5;
  r = render(&plan);
#ifdef OV5_NEW
  printf("plan d3=%u d3_barrier=%u\n", plan.D3ExpectedDonePointer, plan.D3BarrierExpectedDonePointer);
  if (strcmp(mode, "off")) {
    r.D3LeadBarrierAddress = SIDE_VA + 0xa840u;
    r.D3ExpectedDonePointer = plan.D3BarrierExpectedDonePointer;
  }
  if (!strcmp(mode, "compute_control")) { /* the same render without the drill: accepted */
    mode = "compute";
    r.D3LeadBarrierAddress = 0; r.D3ExpectedDonePointer = plan.D3ExpectedDonePointer;
  }
  if (!strcmp(mode, "compute")) {
    r.ComputeWorkAddresses[0] = SIDE_VA + 0xa000u; r.ComputeWorkAddressCount = 1;
    r.ComputeEvent = 9; r.ComputeExpectedStamp = 0x400;
    r.ComputeExpectedDonePointer = plan.ComputeExpectedDonePointer;
    r.TaBarrierAddress = SIDE_VA + 0xa800u; r.LeadBarrierAddress = SIDE_VA + 0xa820u;
    r.TaExpectedDonePointer = plan.TaBarrierExpectedDonePointer;
  }
  if (!strcmp(mode, "nolead")) r.LeadBarrierAddress = 0;
#endif
  ok = q.Queues.RunSuccessor(&p, &r, 77);
  printf("ok=%u guard=%u/%u successor=%u\n", ok, p.LastSubmitGuard, p.LastSubmitRuntimeResult, p.SuccessorFence);
  printf("wp=%u,%u,%u\n", wp[0], wp[1], wp[2]);
  for (i = 0; i < 3; ++i) {
    unsigned k;
    printf("ring%u", i);
    for (k = 0; k < 12; ++k) printf(" %llx", (unsigned long long)ring[i][k]);
    printf("\n");
  }
  printf("pending ta=%u d3=%u\n", p.Runtime.Successor.TaPending.ExpectedDonePointer,
         p.Runtime.Successor.D3Pending.ExpectedDonePointer);
#ifdef OV5_NEW
  printf("placed=%u\n", p.D3LeadPlaced);
#endif
  fputs(trace, stdout);
  return 0;
}
'''

PROVIDER_SOURCES = ['apple_agx_g13_codec.c', 'apple_agx_g13_queue_runtime.c',
                    'apple_agx_g13_queue_provider.c']

# D1: `probe off` on the base commit 92dfda88f (pre-OV5 provider and
# runtime, compiled without OV5_NEW): the successor render's publication,
# its flushed ranges and its two run messages (3D, TA).
GOLDEN_OFF = '''ok=1 guard=0/0 successor=77
wp=7,8,8
ring0 0 0 0 0 0 150000a800 2000000100 0 0 0 0 0
ring1 0 0 0 0 0 0 3000000000 3000000100 0 0 0 0
ring2 0 0 0 0 0 0 0 1500008000 0 0 0 0
pending ta=7 d3=8
flush d30 40
flush d31 40
flush side+0 10000
flush ta1 40
flush ring1[6] 8
flush ring1[7] 8
flush ring0[5] 8
flush ring0[6] 8
publish wp1=8
publish wp0=7
run q1010000000020000000000000080000000800000000000000000000000000000000000000000000000000000000000000
run q0000000000010000000000000070000000700000000000000000000000000000000000000000000000000000000000000
'''


def provider(mode, shared=SHARED, new=True):
    return build_run(PROVIDER_PROBE, [shared / 'src' / s for s in PROVIDER_SOURCES],
                     [shared / 'include'], defines=('OV5_NEW',) if new else (), args=(mode,))


def old_lines(text):
    # the lines the pre-OV5 probe prints (the new tree adds plan/placed)
    return ''.join(line + '\n' for line in text.split('\n')
                   if line and not line.startswith(('plan ', 'placed=')))


class ProviderDrill(unittest.TestCase):
    def test_switch_off_is_byte_identical(self):
        ran = provider('off')
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertEqual(old_lines(ran.stdout), GOLDEN_OFF)
        self.assertIn('placed=0\n', ran.stdout)

    def test_drill_leads_the_3d_publication(self):
        ran = provider('drill')
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        out = ran.stdout
        self.assertIn('plan d3=8 d3_barrier=9\n', out)
        self.assertIn('ok=1 guard=0/0 successor=77\n', out)
        self.assertIn('wp=7,9,8\n', out)
        # TA: its lead Barrier first, then the root (as with the switch off)
        self.assertIn('ring0 0 0 0 0 0 150000a800 2000000100 0 0 0 0 0\n', out)
        # 3D: the drill Barrier (set B slot), then the template's two items
        self.assertIn('ring1 0 0 0 0 0 0 150000a840 3000000000 3000000100 0 0 0\n', out)
        self.assertIn('ring2 0 0 0 0 0 0 0 1500008000 0 0 0 0\n', out)
        self.assertIn('pending ta=7 d3=9\n', out)
        self.assertIn('placed=1\n', out)
        flushes = [l for l in out.split('\n') if l.startswith(('flush', 'publish'))]
        self.assertEqual(flushes, [
            'flush side+0 10000',  # the drill Barrier's prepared range first
            'flush d30 40', 'flush d31 40', 'flush side+0 10000', 'flush ta1 40',
            'flush ring1[6] 8', 'flush ring1[7] 8', 'flush ring1[8] 8',
            'flush ring0[5] 8', 'flush ring0[6] 8', 'publish wp1=9', 'publish wp0=7'])
        runs = [l for l in out.split('\n') if l.startswith('run ')]
        golden_runs = [l for l in GOLDEN_OFF.split('\n') if l.startswith('run ')]
        self.assertEqual(runs[1], golden_runs[1])  # the TA run message is unchanged
        # the 3D run message differs only in its head (write pointer 8 -> 9)
        self.assertEqual(runs[0], golden_runs[0].replace('0000000800000008', '0000000900000008', 1))
        self.assertNotEqual(runs[0], golden_runs[0])

    def test_provider_refuses_misplaced_drill_barriers(self):
        # the control: the same render with compute work, no drill, is accepted
        ran = provider('compute_control')
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn('ok=1 guard=0/0 successor=77\n', ran.stdout)
        for mode in ('compute', 'nolead'):
            ran = provider(mode)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            # AppleAgxG13SubmitGuardBuild, nothing of the render published
            self.assertIn('ok=0 guard=7/0 successor=0\n' if mode == 'compute' else 'ok=0', ran.stdout, mode)
            self.assertIn('wp=5,6,8\n', ran.stdout, mode)
            self.assertNotIn('publish', ran.stdout, mode)
            self.assertIn('placed=0\n', ran.stdout, mode)
        ran = provider('depth1')
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertIn('depth1_plan_d3_barrier_done 0\n', ran.stdout)
        # AppleAgxG13SubmitGuardArguments, nothing published
        self.assertIn('depth1 ok=0 guard=1 wp=5,6\n', ran.stdout)


KMD_DECISION = r'''
#include <assert.h>
#include <stdio.h>
#include "render_job_window.h"
int main(void) {
  const unsigned TC = ADMISSION_JOB_WINDOW_BARRIER_TA_ON_COMPUTE;
  ADMISSION_JOB_WINDOW_OV5_RECEIPT r;
  printf("place %u\n", AdmissionJobWindowOv5DrillRefusal(1, 1, TC, 0, 2, 3, 9, 64));
  printf("off %u\n", AdmissionJobWindowOv5DrillRefusal(0, 1, TC, 0, 2, 3, 9, 64));
  printf("depth1 %u\n", AdmissionJobWindowOv5DrillRefusal(1, 0, TC, 0, 2, 3, 9, 64));
  printf("ta_on_3d %u\n", AdmissionJobWindowOv5DrillRefusal(1, 1, ADMISSION_JOB_WINDOW_BARRIER_TA_ON_3D, 0, 2, 3, 9, 64));
  printf("compute_on_3d %u\n", AdmissionJobWindowOv5DrillRefusal(1, 1, ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D, 0, 2, 3, 9, 64));
  printf("compute_on_compute %u\n", AdmissionJobWindowOv5DrillRefusal(1, 1, ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_COMPUTE, 1, 2, 3, 9, 64));
  printf("none %u\n", AdmissionJobWindowOv5DrillRefusal(1, 1, ADMISSION_JOB_WINDOW_BARRIER_NONE, 0, 2, 3, 9, 64));
  printf("prelude %u\n", AdmissionJobWindowOv5DrillRefusal(1, 1, TC, 1, 2, 3, 9, 64));
  printf("limit %u %u\n", AdmissionJobWindowOv5DrillRefusal(1, 1, TC, 0, 3, 3, 9, 64),
         AdmissionJobWindowOv5DrillRefusal(1, 1, TC, 0, 0, 3, 9, 64));
  printf("done %u %u\n", AdmissionJobWindowOv5DrillRefusal(1, 1, TC, 0, 2, 3, 64, 64),
         AdmissionJobWindowOv5DrillRefusal(1, 1, TC, 0, 2, 3, 0x500, 64));
  AdmissionJobWindowOv5ReceiptInitialize(&r);
  assert(r.Version == ADMISSION_JOB_WINDOW_OV5_RECEIPT_VERSION && r.Bytes == sizeof(r));
  assert(AdmissionJobWindowOv5Note(&r, ADMISSION_JOB_WINDOW_OV5_PLACE, 77, 0x300, 0x4200, 0xa840) == 1u);
  assert(AdmissionJobWindowOv5Note(&r, ADMISSION_JOB_WINDOW_OV5_COMPUTE_WORK, 78, 0, 0, 0) == 1u);
  assert(AdmissionJobWindowOv5Note(&r, ADMISSION_JOB_WINDOW_OV5_BUILD, 79, 0, 0, 0) == 1u);
  assert(AdmissionJobWindowOv5Note(&r, 99u, 80, 0, 0, 0) == 1u);
  printf("receipt %u %u %u %u %u %u %x %x %x\n", r.Considered, r.Built, r.Placed,
         r.Refused[ADMISSION_JOB_WINDOW_OV5_COMPUTE_WORK], r.Refused[ADMISSION_JOB_WINDOW_OV5_BUILD],
         r.LastFence, r.LastWaitValue, r.LastStampSelf, r.LastOffset);
  printf("bytes %u\n", (unsigned)sizeof(r));
  return 0;
}
'''


class KmdDecision(unittest.TestCase):
    def test_decision_and_receipt(self):
        ran = build_run(KMD_DECISION, [RENDER / 'src/render_job_window.c'],
                        [RENDER / 'include', SHARED / 'include'])
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertEqual(ran.stdout.split('\n')[:-1], [
            'place 0', 'off 1', 'depth1 2', 'ta_on_3d 3', 'compute_on_3d 3',
            'compute_on_compute 3', 'none 3', 'prelude 4', 'limit 5 5', 'done 6 6',
            'receipt 4 1 0 1 1 77 300 4200 a840',
            # Version, Bytes, Considered, Built, Placed, Refused[8], Last x4
            'bytes %d' % (4 * (5 + 8 + 4))])


SIDECAR_PROBE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_render_shared_memory.h"
#include "apple_agx_g13_compute_work.h"
struct span { unsigned lo, hi; const char *name; };
int main(void) {
  /* every sidecar word with its extent, both sets for the job words */
  struct span s[32]; unsigned n = 0, set, i, j;
  const unsigned d3 = APPLE_AGX_COMPUTE_3D_BARRIER_OFFSET;
#define ADD(at, bytes, nm) (s[n].lo = (at), s[n].hi = (at) + (bytes), s[n].name = (nm), ++n)
  ADD(APPLE_AGX_COMPUTE_PREEMPT_OFFSET, APPLE_AGX_G13_COMPUTE_PREEMPT_BYTES, "preempt");
  ADD(APPLE_AGX_COMPUTE_FIRMWARE_STAMP_OFFSET, 8u, "stamp");
  ADD(APPLE_AGX_COMPUTE_GPU_BUFFER_OFFSET, APPLE_AGX_COMPUTE_GPU_BUFFER_BYTES, "gpu_buf");
  ADD(APPLE_AGX_COMPUTE_OWN_FIRST_OFFSET, APPLE_AGX_COMPUTE_OWN_END_OFFSET - APPLE_AGX_COMPUTE_OWN_FIRST_OFFSET, "own");
  for (set = 0; set < 2; ++set) {
#define W(off) APPLE_AGX_COMPUTE_JOB_WORD_OFFSET(set, off)
    ADD(W(APPLE_AGX_COMPUTE_WORK_OFFSET), APPLE_AGX_G13_COMPUTE_WORK_BYTES, "work");
    ADD(W(APPLE_AGX_COMPUTE_MICROSEQUENCE_OFFSET), APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_BYTES, "micro");
    ADD(W(APPLE_AGX_COMPUTE_TA_BARRIER_OFFSET), APPLE_AGX_G13_BARRIER_BYTES, "ta_barrier");
    ADD(W(APPLE_AGX_COMPUTE_QUEUE_BARRIER_OFFSET), APPLE_AGX_G13_BARRIER_BYTES, "queue_barrier");
    ADD(W(APPLE_AGX_COMPUTE_TIMESTAMP_MICROSEQUENCE_OFFSET), APPLE_AGX_G13_COMPUTE_MICROSEQUENCE_TIMESTAMP_BYTES, "ts_micro");
    ADD(W(APPLE_AGX_COMPUTE_TIMESTAMP_OFFSET), APPLE_AGX_G13_TIMESTAMP_BLOCK_BYTES, "ts_block");
    ADD(W(d3), APPLE_AGX_G13_BARRIER_BYTES, "d3_barrier");
  }
  for (i = 0; i < n; ++i) for (j = i + 1; j < n; ++j)
    if (s[i].lo < s[j].hi && s[j].lo < s[i].hi) { printf("overlap %s %s %x\n", s[i].name, s[j].name, s[j].lo); return 1; }
  /* the shared statistics run from 0x8600 to the firmware stamp */
  assert(d3 >= APPLE_AGX_COMPUTE_FIRMWARE_STAMP_OFFSET + 8u && (d3 & 0x1fu) == 0u);
  /* set B's slot is in the free range the rc98 sets use */
  assert(d3 + APPLE_AGX_COMPUTE_JOB_WORD_SET_B_DELTA >= APPLE_AGX_COMPUTE_JOB_WORD_SET_FREE_START &&
         d3 + APPLE_AGX_COMPUTE_JOB_WORD_SET_B_DELTA + APPLE_AGX_G13_BARRIER_BYTES <= APPLE_AGX_COMPUTE_GPU_BUFFER_OFFSET);
  printf("slots %x %x\n", d3, d3 + APPLE_AGX_COMPUTE_JOB_WORD_SET_B_DELTA);
  return 0;
}
'''


class SidecarSlot(unittest.TestCase):
    def test_slot_overlaps_nothing(self):
        ran = build_run(SIDECAR_PROBE, [], [SHARED / 'include'])
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertEqual(ran.stdout, 'slots 8840 a840\n')

    def test_no_other_writer_names_the_slot(self):
        """Only the leading-Barrier builder writes the slot."""
        users = []
        for path in (ROOT / 'drivers/apple-agx').rglob('*.[ch]'):
            text = path.read_text(errors='replace')
            if 'APPLE_AGX_COMPUTE_3D_BARRIER_OFFSET' in text or '0x8840u' in text or '0xa840u' in text:
                users.append(path.relative_to(ROOT).as_posix())
        self.assertEqual(sorted(users), [
            'drivers/apple-agx/shared/include/apple_agx_render_shared_memory.h',
            'drivers/apple-agx/shared/src/apple_agx_render_shared_memory.c'])
        source = (SHARED / 'src/apple_agx_render_shared_memory.c').read_text()
        self.assertEqual(source.count('APPLE_AGX_COMPUTE_3D_BARRIER_OFFSET'), 1)
        lead = source[source.index('APPLE_AGX_BOOL AppleAgxRenderSharedMemoryBuildLeadingBarrier('):]
        lead = lead[:lead.index('\n}\n')]
        self.assertIn('APPLE_AGX_COMPUTE_3D_BARRIER_OFFSET', lead)


def section(text, start):
    body = text[text.index(start):]
    return body[:body.index('\n}\n')]


class KmdWiring(unittest.TestCase):
    def test_switch_default_off_read_once_at_start(self):
        text = WORKER.read_text()
        self.assertEqual(text.count('L"Wom1Ov5FragWaitDrill"'), 1)
        self.assertIn('runtime->Ov5DrillEnabled =\n'
                      '      AdmissionPlatformSwitchEnabled(Context, L"Wom1Ov5FragWaitDrill") ? TRUE : FALSE;',
                      text)
        start = section(text, 'NTSTATUS AdmissionPlatformRuntimeStart(')
        self.assertIn('L"Wom1Ov5FragWaitDrill"', start)
        self.assertIn('#include "render_ov5.h"', text)
        self.assertIn('#define ADMISSION_OV5_ON 1', (RENDER / 'include/render_ov5.h').read_text())

    def test_built_only_in_successor_branches_after_the_ta_lead(self):
        text = WORKER.read_text()
        self.assertEqual(text.count('AdmissionBuildOv5FragBarrier(runtime'), 2)
        # declared before use
        self.assertLess(text.index('static VOID AdmissionBuildOv5FragBarrier('),
                        text.index('AdmissionBuildOv5FragBarrier(runtime'))
        build = section(text, 'static APPLE_AGX_BACKEND_BOOL AdmissionExternalBuildJob(')
        no_compute = build[build.index('} else if (Plan->Successor) {'):]
        self.assertLess(no_compute.index('ADMISSION_JOB_WINDOW_QUEUE_TA, set,'),
                        no_compute.index('AdmissionBuildOv5FragBarrier(runtime, Plan, set,'))
        prelude = build[build.index('ADMISSION_JOB_WINDOW_QUEUE_COMPUTE,\n'):build.index('} else if (Plan->Successor) {')]
        self.assertIn('if (Plan->Successor)\n      AdmissionBuildOv5FragBarrier(', prelude)
        drill = section(text, 'static VOID AdmissionBuildOv5FragBarrier(')
        self.assertIn('if (!runtime->Ov5DrillEnabled) return;', drill)
        # OV5 step 2: the Barrier itself is built by the core the drill and
        # the waiver share (the same wait and stamp_self rules)
        self.assertIn('Job->LeadBarrierAddress == 0ULL ||\n'
                      '       !AdmissionBuildOv5D3Barrier(runtime, Set, Job, &barrier, &address)', drill)
        core = section(text, 'static APPLE_AGX_BACKEND_BOOL AdmissionBuildOv5D3Barrier(')
        self.assertIn('Barrier->StampSelf = Job->D3ExpectedStamp;', core)
        self.assertIn('Barrier->WaitValue = running->ComputeExpectedStamp;', core)
        self.assertIn('Barrier->WaitSlot = running->ComputeEvent;', core)
        self.assertIn('APPLE_AGX_RENDER_STAMP_QUEUE_COMPUTE', core)
        self.assertIn('APPLE_AGX_RENDER_BARRIER_QUEUE_3D', core)
        self.assertIn('running->Kind == AppleAgxBackendJobCompute', core)
        self.assertNotRegex(core, r'LeadBarrierAddress\s*=[^=]')
        self.assertIn('Job->D3ExpectedDonePointer = Plan->D3BarrierExpectedDonePointer;', drill)
        # the TA lead Barrier is untouched by the drill
        self.assertNotRegex(drill, r'[^3]LeadBarrierAddress\s*=[^=]')
        self.assertNotIn('TaBarrierAddress =', drill)

    def test_receipt_export_is_throttled_and_unflushed(self):
        export = section(WORKER.read_text(), 'static VOID AdmissionJobTimingExportWindows(')
        self.assertEqual(export.count('L"Wom1Ov5DrillReceipt"'), 2)
        alone = export[export.index('if (!runtime->JobDiagnostics && runtime->Ov5DrillEnabled) {'):]
        alone = alone[:alone.index('#endif')]
        self.assertIn('2ULL * (ULONGLONG)frequency.QuadPart', alone)
        self.assertNotIn('ZwFlushKey', export)
        self.assertIn('ov5.Placed = runtime->Provider.QueueProvider.D3LeadPlaced;', alone)
        self.assertIn('!runtime->Ov5DrillEnabled) ||', export)
        self.assertIn('KeGetCurrentIrql() != PASSIVE_LEVEL) return;', export)

    def test_platform_builder_flushes_the_3d_barrier_range(self):
        source = (SHARED / 'src/apple_agx_platform_provider.c').read_text()
        lead = section(source, 'static APPLE_AGX_BACKEND_BOOL AppleAgxPlatformLeadBarrierRange(')
        self.assertIn('Submission->D3LeadBarrierRange.Address = sidecar->CpuAddress;', lead)
        self.assertIn('return Job->D3LeadBarrierAddress == 0ULL ? APPLE_AGX_BACKEND_TRUE', lead)


if __name__ == '__main__':
    unittest.main()
