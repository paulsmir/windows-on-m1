"""OV5 step 2: the TA waiver (Wom1Ov5TaWaiver, device feature 0x4).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
The waiver removes the only TA-side wait of a render on the compute job
before it. A wrong waiver is a data race (the TA reads what the compute job
still writes) or a lost wait (neither ring waits); a wrong ring layout is a
GPU hang or a stale completion; a hidden feature makes the path dead code:
  W1 parser: without APPLE_AGX_G4_PARSE_RENDER_TA_INDEPENDENT a render
     header (vdm 0, cdm NONE) is refused as before; with it, only a packet
     without a compute command is admitted and marked TaIndependent;
  W2 provider: a waived successor render publishes its TA root alone (no
     lead Barrier), its 3D queue leads with the compute-stamp Barrier, the
     TA/3D done pointers are the plan's waived/Barrier values; a render
     with compute work, a depth-1 job, a waived job without the 3D Barrier
     or with a TA lead are refused (nothing published);
  W3 unflagged renders: the provider publication is byte-identical to the
     pre-OV5 golden trace (test_ov5_frag_wait_drill.GOLDEN_OFF);
  W4 in-order completion: the waived render's TA may end before the
     compute job; the runtime still reports the compute job first and the
     render only after its 3D, so no later job can be kicked on the
     assumption that the compute job ended;
  W5 KMD decision: waive only TA_ON_COMPUTE pairs of a flagged plain
     render behind a compute job of the same WDDM context and process; the
     feature only with the switch, the depth-two kick, TA_ON_COMPUTE and
     Q2c; the receipt counts per reason;
  W6 KMD wiring: default-off switch read once at start, the feature
     published last on the start success path (EXP2196), cleared on stop
     and failure, the parser flag at every render parse site, the waiver
     built before (and instead of) the TA lead Barrier.
"""
import os
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
sys.path.insert(0, str(HERE))
import test_ov5_frag_wait_drill as drill  # noqa: E402

ROOT = HERE.parent
SHARED = ROOT / 'drivers/apple-agx/shared'
RENDER = ROOT / 'drivers/apple-agx/render-admission'
WORKER = RENDER / 'src/backend_platform_windows.c'
G3 = RENDER / 'src/gpuva_g3_windows.c'


def build_run(code, sources, includes, defines=(), args=()):
    with tempfile.TemporaryDirectory(prefix='ov5w-') as tmp:
        src = Path(tmp) / 'probe.c'
        src.write_text(code)
        exe = Path(tmp) / 'probe'
        cmd = [os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
               '-Wconversion', '-Wno-sign-conversion',
               '-ftrivial-auto-var-init=pattern', '-fsanitize=address,undefined',
               *(f'-D{d}' for d in defines), *(f'-I{p}' for p in includes),
               str(src), *map(str, sources), '-o', str(exe)]
        built = subprocess.run(cmd, capture_output=True, text=True)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)
        return subprocess.run([str(exe), *args], capture_output=True, text=True, timeout=60)


PARSER_PROBE = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "apple_agx_g4_submit.h"
typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER header;
  APPLE_AGX_G4_NATIVE_HEADER attachments;
  APPLE_AGX_G4_ATTACHMENT attachment;
  APPLE_AGX_G4_NATIVE_HEADER render;
  APPLE_AGX_G4_NATIVE_RENDER payload;
} PLAIN;
typedef struct {
  APPLE_AGX_G4_PRIVATE_HEADER_V5 Header;
  APPLE_AGX_G4_NATIVE_HEADER ComputeHeader;
  APPLE_AGX_G4_NATIVE_COMPUTE Compute;
  APPLE_AGX_G4_NATIVE_HEADER RenderHeader;
  APPLE_AGX_G4_NATIVE_RENDER Render;
} PRELUDE;
static int any(void *c, unsigned long long va, unsigned int bytes, int write,
               APPLE_AGX_G4_ACCESS_KIND kind, unsigned int ordinal) {
  (void)c; (void)va; (void)bytes; (void)write; (void)kind; (void)ordinal; return 1;
}
static void render(APPLE_AGX_G4_NATIVE_RENDER *r) {
  r->VdmCtrlStreamBase = 0x40000ULL; r->WidthPx = 32u; r->HeightPx = 32u; r->Layers = 1u;
  r->UtileWidthPx = 32u; r->UtileHeightPx = 32u; r->Samples = 1u; r->SampleSizeBytes = 4u;
}
static int plain(unsigned short vdm, unsigned short cdm, unsigned flags, int *ta) {
  PLAIN p; APPLE_AGX_G4_SUBMIT_VIEW v; APPLE_AGX_G4_FAILURE f; int r;
  memset(&p, 0, sizeof(p));
  p.header.Magic = APPLE_AGX_G4_PRIVATE_MAGIC; p.header.Version = 1u;
  p.header.HeaderBytes = sizeof(p.header);
  p.header.CommandBytes = (unsigned)(sizeof(p) - sizeof(p.header));
  p.header.CommandVa = 0x20000ULL;
  p.attachments.Type = APPLE_AGX_G4_FRAGMENT_ATTACHMENTS;
  p.attachments.Size = sizeof(p.attachment);
  p.attachments.VdmBarrier = 0xffffu; p.attachments.CdmBarrier = 0xffffu;
  p.attachment.Pointer = 0x30000ULL; p.attachment.Size = 0x4000ULL;
  p.render.Type = APPLE_AGX_G4_RENDER; p.render.Size = sizeof(p.payload);
  p.render.VdmBarrier = vdm; p.render.CdmBarrier = cdm;
  render(&p.payload);
  r = (int)AppleAgxG4ParseSubmitFlagsEx(&p, sizeof(p), sizeof(p), 0x20000ULL,
      p.header.CommandBytes, flags, any, 0, &v, &f);
  *ta = v.TaIndependent;
  /* the v5 entry point (the KMD's submit and preflight) agrees */
  {
    int r5 = (int)AppleAgxG4ParseSubmitComputeFlagsEx(&p, sizeof(p), sizeof(p),
        0x20000ULL, p.header.CommandBytes, 0x7fa0u, flags, any, 0, &v, &f);
    assert(r5 == r && v.TaIndependent == *ta);
  }
  return r;
}
static int prelude(unsigned short vdm, unsigned short cdm, unsigned flags, int *ta) {
  PRELUDE q; APPLE_AGX_G4_PRIVATE_HEADER_V3 v3; APPLE_AGX_G4_SUBMIT_VIEW v;
  APPLE_AGX_G4_FAILURE f; APPLE_AGX_G4_PROCESS_RANGE pre = {0x3000000ULL, 0x10000u, 0u};
  const unsigned bytes = (unsigned)(sizeof(q) - sizeof(q.Header));
  unsigned i; int r;
  memset(&q, 0, sizeof(q)); memset(&v3, 0, sizeof(v3));
  v3.V2.Base.Magic = APPLE_AGX_G4_PRIVATE_MAGIC;
  v3.V2.Base.Reserved = APPLE_AGX_G4_COLOR_BGRA8;
  for (i = 0; i < APPLE_AGX_G4_PROCESS_RANGE_COUNT; ++i) {
    v3.V2.Process[i].Va = 0x100000ULL + i * 0x400000ULL; v3.V2.Process[i].Bytes = 0x10000u;
  }
  v3.V2.Process[2].Bytes = 0x400000u; v3.V2.Process[3].Bytes = 0x20000u;
  v3.Lease.ManagerId = 1u; v3.Lease.ManagerGeneration = 2u;
  v3.Lease.SceneId = 3u; v3.Lease.SceneGeneration = 4u;
  assert(AppleAgxG4ComposeHeaderV5(&q.Header, &v3, 0x50000ULL, bytes, &pre));
  q.ComputeHeader.Type = APPLE_AGX_G4_COMPUTE; q.ComputeHeader.Size = sizeof(q.Compute);
  q.Compute.CdmCtrlStreamBase = 0x4000000ULL; q.Compute.CdmCtrlStreamEnd = 0x4000040ULL;
  q.RenderHeader.Type = APPLE_AGX_G4_RENDER; q.RenderHeader.Size = sizeof(q.Render);
  q.RenderHeader.VdmBarrier = vdm; q.RenderHeader.CdmBarrier = cdm;
  render(&q.Render);
  r = (int)AppleAgxG4ParseSubmitComputeFlagsEx(&q, sizeof(q), sizeof(q), 0x50000ULL,
      bytes, 0x7fa0u, flags, any, 0, &v, &f);
  *ta = v.TaIndependent;
  return r;
}
int main(void) {
  const unsigned TA = APPLE_AGX_G4_PARSE_RENDER_TA_INDEPENDENT;
  int ta, r;
  r = plain(0u, 0u, 0u, &ta); printf("plain_off %d %d\n", r, ta);
  r = plain(0u, 0xffffu, 0u, &ta); printf("flagged_off %d %d\n", r, ta);
  r = plain(0u, 0xffffu, TA, &ta); printf("flagged_on %d %d\n", r, ta);
  r = plain(0u, 0u, TA, &ta); printf("plain_on %d %d\n", r, ta);
  r = plain(0xffffu, 0xffffu, TA, &ta); printf("vdm_none_on %d %d\n", r, ta);
  r = plain(0u, 1u, TA, &ta); printf("cdm_one_on %d %d\n", r, ta);
  r = plain(0u, 0xffffu, APPLE_AGX_G4_PARSE_INDEPENDENT_SUBQUEUE, &ta);
  printf("compute_flag %d %d\n", r, ta);
  r = plain(0u, 0xffffu, 0x4u, &ta); printf("unknown_flag %d %d\n", r, ta);
  r = prelude(0xffffu, 1u, TA, &ta); printf("prelude_on %d %d\n", r, ta);
  r = prelude(0xffffu, 0xffffu, TA, &ta); printf("prelude_cdm_none_on %d %d\n", r, ta);
  r = prelude(0u, 0xffffu, TA, &ta); printf("prelude_flag_on %d %d\n", r, ta);
  return 0;
}
'''


class Parser(unittest.TestCase):
    def test_flag_admitted_only_with_the_feature_and_without_compute(self):
        ran = build_run(PARSER_PROBE, [SHARED / 'src/apple_agx_g4_submit.c'], [SHARED / 'include'])
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        ok, invalid = 0, 1  # AppleAgxG4ParseOk, AppleAgxG4ParseInvalid
        got = dict((l.split()[0], (int(l.split()[1]), int(l.split()[2])))
                   for l in ran.stdout.split('\n') if l)
        self.assertEqual(got, {
            'plain_off': (ok, 0),
            # feature off: today's refusal of an unexpected barrier value
            'flagged_off': (invalid, 0),
            # feature on: admitted and marked
            'flagged_on': (ok, 1),
            'plain_on': (ok, 0),
            'vdm_none_on': (invalid, 0),
            'cdm_one_on': (invalid, 0),
            # the OV2 compute flag does not admit it; unknown flags are invalid
            'compute_flag': (invalid, 0),
            'unknown_flag': (invalid, 0),
            # a render with a compute command is never TA independent
            'prelude_on': (ok, 0),
            'prelude_cdm_none_on': (invalid, 0),
            'prelude_flag_on': (invalid, 0)})

    def test_result_values(self):
        header = (SHARED / 'include/apple_agx_g4_submit.h').read_text()
        self.assertRegex(header, r'AppleAgxG4ParseOk\s*=\s*0')
        self.assertIn('AppleAgxG4ParseInvalid', header)


# The drill's provider probe with the waiver modes: the job carries no TA
# lead Barrier, TaLeadWaived, the 3D lead Barrier (set B slot) and the
# plan's waived TA and Barrier 3D done pointers. "waiver" also drives the
# completions: R's TA first, then N's compute, then R's 3D.
WAIVER_MODES = r'''
  if (!strncmp(mode, "waiver", 6)) {
    printf("plan ta=%u ta_waived=%u\n", plan.TaExpectedDonePointer, plan.TaWaivedExpectedDonePointer);
    r.LeadBarrierAddress = 0; r.TaLeadWaived = 1u;
    r.D3LeadBarrierAddress = SIDE_VA + 0xa840u;
    r.D3ExpectedDonePointer = plan.D3BarrierExpectedDonePointer;
    r.TaExpectedDonePointer = plan.TaWaivedExpectedDonePointer;
    if (!strcmp(mode, "waiver_compute")) {
      r.ComputeWorkAddresses[0] = SIDE_VA + 0xa000u; r.ComputeWorkAddressCount = 1;
      r.ComputeEvent = 9; r.ComputeExpectedStamp = 0x400;
      r.ComputeExpectedDonePointer = plan.ComputeExpectedDonePointer;
      r.TaBarrierAddress = SIDE_VA + 0xa800u;
    }
    if (!strcmp(mode, "waiver_no_d3")) {
      r.D3LeadBarrierAddress = 0; r.D3ExpectedDonePointer = plan.D3ExpectedDonePointer;
    }
    if (!strcmp(mode, "waiver_ta_lead")) {
      r.LeadBarrierAddress = SIDE_VA + 0xa800u; r.TaExpectedDonePointer = plan.TaExpectedDonePointer;
    }
  }
'''

WAIVER_EVENTS = r'''
  if (!strcmp(mode, "waiver") && ok) {
    unsigned char ev[APPLE_AGX_G13_EVENT_MESSAGE_SIZE];
    APPLE_AGX_G13_QUEUE_RUNTIME_COMPLETION done;
#define FIRE(bit) do { memset(ev, 0, sizeof(ev)); ev[0] = 1; ev[4 + (bit) / 8] = (unsigned char)(1u << ((bit) % 8)); \
    printf("event%u %u\n", (unsigned)(bit), (unsigned)AppleAgxG13QueueRuntimeHandleEvent(&p.Runtime, ev, sizeof(ev))); } while (0)
    /* R's TA ends while N's compute still runs (the waiver's overlap) */
    dp[0] = wp[0]; st[0] = 0x4100;
    FIRE(7);
    printf("after_ta phase=%u completion=%u head=%u succ_ta=%u succ_d3=%u\n", (unsigned)p.Runtime.Phase,
           (unsigned)p.Runtime.CompletionAvailable, p.Runtime.PendingFence,
           (unsigned)p.Runtime.Successor.TaPending.Complete, (unsigned)p.Runtime.Successor.D3Pending.Complete);
    printf("plan_successor %u\n", (unsigned)AppleAgxG13QueueProviderPlanSuccessor(&p, &plan));
    /* N's compute ends: N completes first, R becomes the running job */
    dp[2] = wp[2]; st[2] = 0x300;
    FIRE(9);
    printf("after_compute completion=%u fence=%u\n", (unsigned)p.Runtime.CompletionAvailable,
           p.Runtime.Completion.Fence);
    printf("take %u fence=%u status=%u\n", (unsigned)AppleAgxG13QueueRuntimeTakeCompletion(&p.Runtime, &done),
           done.Fence, (unsigned)done.Status);
    printf("promoted head=%u phase=%u completion=%u\n", p.Runtime.PendingFence,
           (unsigned)p.Runtime.Phase, (unsigned)p.Runtime.CompletionAvailable);
    /* R's 3D (behind the compute-stamp Barrier) ends: R completes */
    dp[1] = wp[1]; st[1] = 0x4200;
    FIRE(8);
    printf("after_d3 completion=%u fence=%u\n", (unsigned)p.Runtime.CompletionAvailable,
           p.Runtime.Completion.Fence);
  }
'''


def waiver_probe():
    code = drill.PROVIDER_PROBE
    anchor = '  ok = q.Queues.RunSuccessor(&p, &r, 77);\n'
    assert code.count(anchor) == 1
    code = code.replace(anchor, WAIVER_MODES + anchor)
    tail = '  fputs(trace, stdout);\n  return 0;\n}\n'
    assert code.count(tail) == 1
    code = code.replace(tail, '  printf("waived_placed=%u\\n", p.TaLeadWaivedPlaced);\n'
                        '  fputs(trace, stdout);\n' + WAIVER_EVENTS + '  return 0;\n}\n')
    return code


def provider(mode):
    return build_run(waiver_probe(), [SHARED / 'src' / s for s in drill.PROVIDER_SOURCES],
                     [SHARED / 'include'], defines=('OV5_NEW',), args=(mode,))


class ProviderWaiver(unittest.TestCase):
    def test_unflagged_render_is_byte_identical(self):
        for mode in ('off', 'drill'):
            ran = provider(mode)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('waived_placed=0\n', ran.stdout, mode)
            # the drill's own outputs, unchanged by the waiver's fields
            reference = drill.provider(mode)
            self.assertEqual(reference.returncode, 0, reference.stdout + reference.stderr)
            self.assertEqual(ran.stdout.replace('waived_placed=0\n', ''), reference.stdout, mode)
        self.assertEqual(drill.old_lines(provider('off').stdout.replace('waived_placed=0\n', '')),
                         drill.GOLDEN_OFF)

    def test_waived_render_leads_only_its_3d_queue(self):
        ran = provider('waiver')
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        out = ran.stdout
        self.assertIn('plan ta=7 ta_waived=6\n', out)
        self.assertIn('plan d3=8 d3_barrier=9\n', out)
        self.assertIn('ok=1 guard=0/0 successor=77\n', out)
        # TA: the root alone (no lead Barrier); 3D: the compute-stamp Barrier first
        self.assertIn('wp=6,9,8\n', out)
        self.assertIn('ring0 0 0 0 0 0 2000000100 0 0 0 0 0 0\n', out)
        self.assertIn('ring1 0 0 0 0 0 0 150000a840 3000000000 3000000100 0 0 0\n', out)
        self.assertIn('ring2 0 0 0 0 0 0 0 1500008000 0 0 0 0\n', out)
        self.assertIn('pending ta=6 d3=9\n', out)
        self.assertIn('placed=0\nwaived_placed=1\n', out)
        flushes = [l for l in out.split('\n') if l.startswith(('flush', 'publish'))]
        self.assertEqual(flushes, [
            'flush side+0 10000',  # the 3D Barrier's prepared range first
            'flush d30 40', 'flush d31 40', 'flush ta1 40',
            'flush ring1[6] 8', 'flush ring1[7] 8', 'flush ring1[8] 8',
            'flush ring0[5] 8', 'publish wp1=9', 'publish wp0=6'])
        runs = [l for l in out.split('\n') if l.startswith('run ')]
        golden = [l for l in drill.GOLDEN_OFF.split('\n') if l.startswith('run ')]
        # the run messages differ from the golden ones only in the heads
        # (3D write pointer 8 -> 9, TA write pointer 7 -> 6)
        self.assertEqual(runs[0], golden[0].replace('0000000800000008', '0000000900000008', 1))
        self.assertEqual(runs[1], golden[1].replace('0000000700000007', '0000000600000007', 1))

    def test_ta_end_before_the_compute_job_keeps_completion_in_order(self):
        """W4: the waived render's TA ends first. The compute job N must
        still be the job that completes first, the window must stay full
        (no successor plan, so nothing else is kicked), and R completes only
        after its 3D, which waits on N's compute stamp."""
        out = provider('waiver').stdout
        lines = out[out.index('event7'):].split('\n')
        self.assertEqual(lines[:9], [
            'event7 0',
            # phase 1 = ComputeSubmitted: N runs, no completion, R's TA done
            'after_ta phase=1 completion=0 head=76 succ_ta=1 succ_d3=0',
            # the window is full: no third job can be planned
            'plan_successor 0',
            'event9 0',
            'after_compute completion=1 fence=76',
            'take 1 fence=76 status=0',
            # R is the running job now, not complete (its 3D is pending)
            'promoted head=77 phase=3 completion=0',
            'event8 0',
            'after_d3 completion=1 fence=77'])

    def test_misplaced_waivers_are_refused(self):
        for mode in ('waiver_compute', 'waiver_no_d3', 'waiver_ta_lead'):
            ran = provider(mode)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('ok=0', ran.stdout, mode)
            self.assertIn('wp=5,6,8\n', ran.stdout, mode)
            self.assertNotIn('publish', ran.stdout, mode)
            self.assertIn('waived_placed=0\n', ran.stdout, mode)

    def test_depth1_and_compute_submissions_refuse_the_flag(self):
        source = (SHARED / 'src/apple_agx_g13_queue_provider.c').read_text()
        run_ta = drill.section(source, 'static APPLE_AGX_BACKEND_BOOL AppleAgxG13ProviderRunTa(')
        self.assertIn('Job->TaLeadWaived != 0u', run_ta)
        runtime = (SHARED / 'src/apple_agx_g13_queue_runtime.c').read_text()
        compute = drill.section(runtime, 'APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeSubmitCompute(')
        self.assertIn('Submission->TaLeadWaived', compute)
        with_compute = drill.section(
            runtime, 'APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeSubmitRenderWithCompute(')
        self.assertIn('Submission->TaLeadWaived', with_compute)
        submit = drill.section(runtime, 'APPLE_AGX_G13_QUEUE_RUNTIME_RESULT AppleAgxG13QueueRuntimeSubmit(')
        self.assertIn('(Submission->TaLeadWaived &&\n       (!successor || !Submission->D3LeadingBarrier))',
                      submit)
        self.assertIn('(successor && !Submission->TaLeadWaived ? 1u : 0u)', submit)


KMD_DECISION = r'''
#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "render_job_window.h"
static ADMISSION_JOB_WINDOW_JOB job(unsigned kind, unsigned long long ctx, unsigned ta) {
  ADMISSION_JOB_WINDOW_JOB j; memset(&j, 0, sizeof(j));
  j.Fence = 1; j.Kind = kind; j.Process = 0x10; j.Context = ctx; j.TaIndependent = ta; return j;
}
int main(void) {
  const unsigned TC = ADMISSION_JOB_WINDOW_BARRIER_TA_ON_COMPUTE;
  ADMISSION_JOB_WINDOW_JOB c = job(ADMISSION_JOB_WINDOW_KIND_COMPUTE, 0x20, 0);
  ADMISSION_JOB_WINDOW_JOB r = job(ADMISSION_JOB_WINDOW_KIND_RENDER, 0x20, 1);
  ADMISSION_JOB_WINDOW_JOB x;
  ADMISSION_JOB_WINDOW_OV5W_RECEIPT rc;
  printf("waive %u\n", AdmissionJobWindowOv5WaiverRefusal(1, TC, &c, &r));
  printf("off %u\n", AdmissionJobWindowOv5WaiverRefusal(0, TC, &c, &r));
  printf("ta_on_3d %u\n", AdmissionJobWindowOv5WaiverRefusal(1, ADMISSION_JOB_WINDOW_BARRIER_TA_ON_3D, &c, &r));
  printf("compute_on_compute %u\n", AdmissionJobWindowOv5WaiverRefusal(1, ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_COMPUTE, &c, &r));
  x = r; x.TaIndependent = 0;
  printf("unflagged %u\n", AdmissionJobWindowOv5WaiverRefusal(1, TC, &c, &x));
  x = r; x.Kind = ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE;
  printf("prelude %u\n", AdmissionJobWindowOv5WaiverRefusal(1, TC, &c, &x));
  x = c; x.Kind = ADMISSION_JOB_WINDOW_KIND_PRESENT;
  printf("running_present %u\n", AdmissionJobWindowOv5WaiverRefusal(1, TC, &x, &r));
  x = c; x.Context = 0x30;
  printf("other_context %u\n", AdmissionJobWindowOv5WaiverRefusal(1, TC, &x, &r));
  x = c; x.Process = 0x11;
  printf("other_process %u\n", AdmissionJobWindowOv5WaiverRefusal(1, TC, &x, &r));
  x = c; x.Context = 0; r.Context = 0;
  printf("no_context %u\n", AdmissionJobWindowOv5WaiverRefusal(1, TC, &x, &r));
  r.Context = 0x20;
  printf("null %u\n", AdmissionJobWindowOv5WaiverRefusal(1, TC, 0, &r));
  printf("build %u %u %u %u %u %u %u\n",
         AdmissionJobWindowOv5WaiverBuildRefusal(1, 0, 2, 3, 9, 64, 6, 64),
         AdmissionJobWindowOv5WaiverBuildRefusal(0, 0, 2, 3, 9, 64, 6, 64),
         AdmissionJobWindowOv5WaiverBuildRefusal(1, 1, 2, 3, 9, 64, 6, 64),
         AdmissionJobWindowOv5WaiverBuildRefusal(1, 0, 3, 3, 9, 64, 6, 64),
         AdmissionJobWindowOv5WaiverBuildRefusal(1, 0, 0, 3, 9, 64, 6, 64),
         AdmissionJobWindowOv5WaiverBuildRefusal(1, 0, 2, 3, 64, 64, 6, 64),
         AdmissionJobWindowOv5WaiverBuildRefusal(1, 0, 2, 3, 9, 64, 64, 64));
  {
    const unsigned all = 0u, tc = 1u << TC, other = 1u << ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D;
    printf("available %u %u %u %u %u %u %u\n",
           AdmissionJobWindowOv5WaiverAvailable(1, 1, all, 1, 1),
           AdmissionJobWindowOv5WaiverAvailable(1, 1, tc | other, 1, 1),
           AdmissionJobWindowOv5WaiverAvailable(0, 1, all, 1, 1),
           AdmissionJobWindowOv5WaiverAvailable(1, 0, all, 1, 1),
           AdmissionJobWindowOv5WaiverAvailable(1, 1, other, 1, 1),
           AdmissionJobWindowOv5WaiverAvailable(1, 1, all, 0, 1),
           AdmissionJobWindowOv5WaiverAvailable(1, 1, all, 1, 0));
  }
  AdmissionJobWindowOv5WaiverReceiptInitialize(&rc);
  assert(rc.Version == ADMISSION_JOB_WINDOW_OV5W_RECEIPT_VERSION && rc.Bytes == sizeof(rc));
  /* EXP2292: held back only while a compute with a timestamp block has not started */
  assert(rc.Deferred == 0u);
  assert(AdmissionJobWindowOv5WaiverDefer(ADMISSION_JOB_WINDOW_OV5W_WAIVE, 1u, 0ull) == 1u);
  assert(AdmissionJobWindowOv5WaiverDefer(ADMISSION_JOB_WINDOW_OV5W_WAIVE, 1u, 5ull) == 0u);
  assert(AdmissionJobWindowOv5WaiverDefer(ADMISSION_JOB_WINDOW_OV5W_WAIVE, 0u, 0ull) == 0u);
  assert(AdmissionJobWindowOv5WaiverDefer(ADMISSION_JOB_WINDOW_OV5W_CLASS, 1u, 0ull) == 0u);
  assert(AdmissionJobWindowOv5WaiverNote(&rc, ADMISSION_JOB_WINDOW_OV5W_WAIVE, 77, 0x300, 0x4200, 0xa840) == 1u);
  assert(AdmissionJobWindowOv5WaiverNote(&rc, ADMISSION_JOB_WINDOW_OV5W_CLASS, 78, 0, 0, 0) == 1u);
  assert(AdmissionJobWindowOv5WaiverNote(&rc, ADMISSION_JOB_WINDOW_OV5W_COMPUTE_WORK, 79, 0, 0, 0) == 1u);
  assert(AdmissionJobWindowOv5WaiverNote(&rc, ADMISSION_JOB_WINDOW_OV5W_BUILD, 80, 0, 0, 0) == 1u);
  assert(AdmissionJobWindowOv5WaiverNote(&rc, 99u, 81, 0, 0, 0) == 1u);
  printf("receipt %u %u %u %u %u %u %u %x %x %x\n", rc.Considered, rc.Waived, rc.Placed,
         rc.Refused[ADMISSION_JOB_WINDOW_OV5W_CLASS], rc.Refused[ADMISSION_JOB_WINDOW_OV5W_COMPUTE_WORK],
         rc.Refused[ADMISSION_JOB_WINDOW_OV5W_BUILD], rc.LastFence, rc.LastWaitValue,
         rc.LastStampSelf, rc.LastOffset);
  printf("bytes %u\n", (unsigned)sizeof(rc));
  return 0;
}
'''


class KmdDecision(unittest.TestCase):
    def test_decision_availability_and_receipt(self):
        ran = build_run(KMD_DECISION, [RENDER / 'src/render_job_window.c'],
                        [RENDER / 'include', SHARED / 'include'])
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertEqual(ran.stdout.split('\n')[:-1], [
            'waive 0', 'off 1', 'ta_on_3d 2', 'compute_on_compute 2', 'unflagged 3',
            'prelude 3', 'running_present 4', 'other_context 5', 'other_process 5',
            'no_context 5', 'null 3',
            # successor, compute work, ring limit (2x), 3D done, TA done
            'build 0 8 6 7 7 7 7',
            # only with switch, kick, TA_ON_COMPUTE (0 = every class), Q2c, compute
            'available 1 1 0 0 0 0 0',
            'receipt 5 1 0 1 1 1 77 300 4200 a840',
            # Version, Bytes, Considered, Waived, Placed, Refused[9], Last x4, Deferred
            'bytes %d' % (4 * (5 + 9 + 4 + 1))])


def section(text, start):
    return drill.section(text, start)


class KmdWiring(unittest.TestCase):
    def test_switch_default_off_read_once_at_start(self):
        text = WORKER.read_text()
        self.assertEqual(text.count('L"Wom1Ov5TaWaiver"'), 1)
        self.assertIn('runtime->Ov5WaiverSwitch =\n'
                      '      AdmissionPlatformSwitchEnabled(Context, L"Wom1Ov5TaWaiver") ? TRUE : FALSE;',
                      text)
        start = section(text, 'NTSTATUS AdmissionPlatformRuntimeStart(')
        self.assertIn('L"Wom1Ov5TaWaiver"', start)

    def test_start_publishes_feature_after_reset(self):
        """EXP2196 analogue: the device feature is the last write of
        GpuRenderTaIndependent on the start success path (after the soft-
        fault/OV2 reset and after the backend start), it publishes
        Ov5Waiver, and Ov5Waiver is computed right there from the switch,
        the depth-two kick, its classes and Q2c."""
        src = WORKER.read_text()
        start = src.index('NTSTATUS AdmissionPlatformRuntimeStart(')
        body = src[start:src.index('\nFail:', start)]
        last = body.rindex('GpuRenderTaIndependent')
        line = body[body.rindex('\n', 0, last):body.index('\n', last)]
        self.assertIn('runtime->Ov5Waiver ? 1 : 0', line)
        for later_than in ('InterlockedExchange(&Context->GpuSoftFaults, 0);',
                           'GpuIndependentSubqueue, runtime->Ov2Enabled ? 1 : 0',
                           'AppleAgxBackendRuntimeStart(', 'AdmissionJobThreadStart(Context, runtime);'):
            self.assertLess(body.rindex(later_than), last, later_than)
        tail = body[last:]
        self.assertNotIn('GpuRenderTaIndependent', tail[tail.index('\n'):])
        self.assertLess(body.rindex('runtime->Ov5Waiver = AdmissionJobWindowOv5WaiverAvailable('), last)
        avail = body[body.rindex('runtime->Ov5Waiver = AdmissionJobWindowOv5WaiverAvailable('):last]
        for term in ('runtime->Ov5WaiverSwitch', 'runtime->JobDepth2Kick', 'runtime->Depth2KickClasses',
                     'RenderSharedMemory.ComputeOwnQueue', 'QueueProvider.Config.HasCompute'):
            self.assertIn(term, avail)
        # reset early in start, cleared on failure and stop
        self.assertLess(body.index('InterlockedExchange(&Context->GpuRenderTaIndependent, 0);'), last)
        fail = src[src.index('\nFail:', start):]
        self.assertIn('InterlockedExchange(&Context->GpuRenderTaIndependent, 0);',
                      fail[:fail.index('\n}\n')])
        stop = section(src, 'NTSTATUS AdmissionPlatformRuntimeStop(')
        self.assertLess(stop.index('InterlockedExchange(&Context->GpuRenderTaIndependent, 0);'),
                        stop.index('AdmissionPlatformDestroy(runtime)'))

    def test_feature_reported_and_parser_flag_at_every_render_parse(self):
        lifecycle = (RENDER / 'src/lifecycle.c').read_text()
        self.assertIn('if (InterlockedCompareExchange(&context->GpuRenderTaIndependent, 0, 0) != 0)\n'
                      '      info->Features |= AGX_WIN32_DEVICE_FEATURE_RENDER_TA_INDEPENDENT;', lifecycle)
        g3 = G3.read_text()
        self.assertTrue(g3.startswith('#include "render_ov2.h"\n#include "render_ov5.h"\n'))
        flags = section(g3, 'static unsigned int AdmissionG4RenderParseFlags(')
        self.assertIn('adapter->GpuRenderTaIndependent', flags)
        self.assertIn('APPLE_AGX_G4_PARSE_RENDER_TA_INDEPENDENT', flags)
        self.assertLess(g3.index('static unsigned int AdmissionG4RenderParseFlags('),
                        g3.index('ADMISSION_G4_RENDER_PARSE_FLAGS(adapter)'))
        # submit, preflight and both begin re-proofs pass the flag; no render
        # parse without flags is left
        self.assertEqual(g3.count('ADMISSION_G4_RENDER_PARSE_FLAGS(adapter)'), 4)
        self.assertNotIn('AppleAgxG4ParseSubmitComputeEx(', g3)
        self.assertNotIn('AppleAgxG4ParseSubmitEx(', g3)
        ov5 = (RENDER / 'include/render_ov5.h').read_text()
        self.assertIn('#define ADMISSION_G4_RENDER_PARSE_FLAGS(adapter) AdmissionG4RenderParseFlags(adapter)', ov5)
        image = (RENDER / 'src/render_backend_image.c').read_text()
        self.assertIn('Image->G4TaIndependent = View->TaIndependent && !View->HasCompute', image)
        describe = section(WORKER.read_text(), 'static VOID AdmissionJobWindowDescribeLocked(')
        self.assertIn('job->TaIndependent = image->BoundFence == fence && image->G4Native &&\n'
                      '      !image->G4WithCompute && image->G4TaIndependent ? 1u : 0u;', describe)

    def test_waiver_replaces_the_ta_lead_only_in_the_no_compute_successor_branch(self):
        text = WORKER.read_text()
        build = section(text, 'static APPLE_AGX_BACKEND_BOOL AdmissionExternalBuildJob(')
        no_compute = build[build.index('} else if (Plan->Successor) {'):]
        no_compute = no_compute[:no_compute.index('runtime->Depth2BuildStep = ADMISSION_KICK_BUILD_RENDER_LEAD;')]
        self.assertLess(no_compute.index('waived = AdmissionBuildOv5TaWaiver(runtime, Plan, set,'),
                        no_compute.index('if (!waived &&\n        !AdmissionBuildLeadBarrier(runtime, '
                                         'ADMISSION_JOB_WINDOW_QUEUE_TA, set,'))
        self.assertIn('if (!waived)\n      AdmissionBuildOv5FragBarrier(runtime, Plan, set,', no_compute)
        # a prelude render is only counted (refusal 6), never waived
        prelude = build[build.index('ADMISSION_JOB_WINDOW_QUEUE_COMPUTE,\n'):build.index('} else if (Plan->Successor) {')]
        self.assertIn('if (Plan->Successor)\n      (void)AdmissionBuildOv5TaWaiver(', prelude)
        waiver = section(text, 'static BOOLEAN AdmissionBuildOv5TaWaiver(')
        self.assertIn('if (!runtime->Ov5Waive) return FALSE;\n  runtime->Ov5Waive = FALSE;', waiver)
        self.assertIn('AdmissionBuildOv5D3Barrier(runtime, Set, Job, &barrier, &address)', waiver)
        for line in ('Job->D3ExpectedDonePointer = Plan->D3BarrierExpectedDonePointer;',
                     'Job->TaExpectedDonePointer = Plan->TaWaivedExpectedDonePointer;',
                     'Job->TaLeadWaived = 1u;',
                     'runtime->Depth2KickBarrier != ADMISSION_JOB_WINDOW_BARRIER_TA_ON_COMPUTE',
                     'runtime->Backend.Phase != AppleAgxBackendRuntimeSubmitted'):
            self.assertIn(line, waiver)
        self.assertNotRegex(waiver, r'[^3]LeadBarrierAddress\s*=[^=]')
        # declared before use
        self.assertLess(text.index('static BOOLEAN AdmissionBuildOv5TaWaiver('),
                        text.index('AdmissionBuildOv5TaWaiver(runtime'))
        self.assertLess(text.index('static APPLE_AGX_BACKEND_BOOL AdmissionBuildOv5D3Barrier('),
                        text.index('AdmissionBuildOv5D3Barrier(runtime'))

    def test_kick_decision_uses_the_pure_rule(self):
        text = WORKER.read_text()
        self.assertEqual(text.count('AdmissionJobWindowOv5WaiverRefusal(1u,'), 2)  # the kick decision and the EXP2292 deferral
        self.assertEqual(text.count('runtime->Ov5Waive = TRUE;'), 1)
        kick = text[text.index('unsigned int ov5w = AdmissionJobWindowOv5WaiverRefusal(1u,'):]
        self.assertLess(kick.index('runtime->Ov5Waive = TRUE;'),
                        kick.index('runtime->Depth2KickBarrier = barrier;'))
        self.assertIn('(unsigned int)barrier, &s.Running, &s.Next);', kick[:200])

    def test_receipt_export_is_throttled_and_unflushed(self):
        export = section(WORKER.read_text(), 'static VOID AdmissionJobTimingExportWindows(')
        self.assertEqual(export.count('L"Wom1Ov5WaiverReceipt"'), 2)
        alone = export[export.index('if (!runtime->JobDiagnostics && runtime->Ov5Waiver) {'):]
        alone = alone[:alone.index('#endif')]
        self.assertIn('2ULL * (ULONGLONG)frequency.QuadPart', alone)
        self.assertIn('ov5w.Placed = runtime->Provider.QueueProvider.TaLeadWaivedPlaced;', alone)
        self.assertNotIn('ZwFlushKey', export)
        self.assertIn('!runtime->Ov5Waiver && !runtime->Ov5DrillEnabled) ||', export)

    def test_msvc_w4_traps(self):
        """C4201 nameless unions/structs in the new receipt; BOOLEAN from a
        comparison (no C4242 narrowing). The host probes above compile the
        shared sources with -Wconversion."""
        header = (RENDER / 'include/render_job_window.h').read_text()
        receipt = header[header.index('typedef struct _ADMISSION_JOB_WINDOW_OV5W_RECEIPT {'):]
        receipt = receipt[:receipt.index('}')]
        self.assertNotIn('union', receipt)
        self.assertNotIn('struct {', receipt)
        # BOOLEAN results are written from comparisons, never from wider ints
        waiver = section(WORKER.read_text(), 'static BOOLEAN AdmissionBuildOv5TaWaiver(')
        self.assertIn('return refusal == ADMISSION_JOB_WINDOW_OV5W_WAIVE ? TRUE : FALSE;', waiver)
        self.assertIsNone(re.search(r'\(BOOLEAN\)', waiver))


if __name__ == '__main__':
    unittest.main()
