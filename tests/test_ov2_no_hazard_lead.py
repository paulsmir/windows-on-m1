"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
OV2 lets a compute job start while the render before it still runs. That is
only legal when D3D12 declares no hazard between them; a wrong 'independent'
verdict is a real data race (the compute reads a render target or UAV the
render still writes). The UMD rule (agx_d3d12_job_barriers.h) must say
independent only for: previous batch a render of the same execution, no
barrier in the gap, no barrier but UAV barriers inside the compute batch, a
render that bound no UAV, fully logged barriers, no other owner work
(e.g. ResolveQueryData: Asahi copies the query result in the next compute
batch, which then depends on the render) and no dispatch from before the render. The KMD decision
(render_job_window.c) must waive only COMPUTE_ON_3D pairs of a flagged
compute behind a render of the same WDDM context and process, and the wait
value must otherwise stay the running job's end.
"""
import os
import subprocess
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
D3D12 = ROOT / 'drivers/apple-agx/d3d12'
RENDER = ROOT / 'drivers/apple-agx/render-admission'
SHARED = ROOT / 'drivers/apple-agx/shared'


def build_run(code, sources, includes):
    with tempfile.TemporaryDirectory(prefix='ov2-') as tmp:
        src = Path(tmp) / 'test.c'
        src.write_text(code)
        exe = Path(tmp) / 'test'
        cmd = [os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wextra', '-Werror',
               '-fsanitize=address,undefined', *(f'-I{p}' for p in includes),
               str(src), *map(str, sources), '-o', str(exe)]
        built = subprocess.run(cmd, capture_output=True, text=True)
        if built.returncode:
            raise AssertionError(built.stdout + built.stderr)
        return subprocess.run([str(exe)], capture_output=True, text=True, timeout=30)


UMD_RULE = r'''
#include <assert.h>
#include <stdio.h>
#include "agx_d3d12_job_barriers.h"
enum { DRAW = 5, DISPATCH = 2, TRANSITION = 0, UAV = 2 };
static AGX_D3D12_JOB_BARRIERS j;
static unsigned values[AGX_D3D12_JOB_BARRIER_VALUES];
/* A render batch R (2 draws, optionally one binding a UAV), then a compute
 * batch C whose first dispatch lazily submits R; gap barriers before C, inside
 * barriers between C's dispatches, then the next render's first draw submits
 * C after a C->R gap of transitions (which must not count). */
enum { QUERY = 9, COPY = 1 };
static unsigned last_reason;
static unsigned scenario_ex(unsigned known, unsigned uav_draw, unsigned gap, unsigned inside_type,
                            unsigned prev_compute, unsigned resolve_trigger, unsigned copy_inside,
                            unsigned early_dispatch);
static unsigned scenario(unsigned known, unsigned uav_draw, unsigned gap, unsigned inside_type,
                         unsigned prev_compute) {
  return scenario_ex(known, uav_draw, gap, inside_type, prev_compute, 0, 0, 0);
}
static unsigned scenario_ex(unsigned known, unsigned uav_draw, unsigned gap, unsigned inside_type,
                            unsigned prev_compute, unsigned resolve_trigger, unsigned copy_inside,
                            unsigned early_dispatch) {
  AgxD3d12JobBarriersInit(&j);
  AgxD3d12JobBarrierExecution(&j, known);
  if (prev_compute) { /* a compute batch right before R's own submission */
    AgxD3d12JobBarrierBegin(&j, DISPATCH); AgxD3d12JobBarrierEnd(&j);
  }
  if (early_dispatch) { /* a dispatch kept in an open compute batch across R */
    AgxD3d12JobBarrierBegin(&j, DISPATCH); AgxD3d12JobBarrierEnd(&j);
  }
  AgxD3d12JobBarrierBegin(&j, DRAW);
  if (prev_compute) AgxD3d12JobBarrierBatch(&j, 2u, values); /* lazily at R's first draw */
  if (uav_draw) AgxD3d12JobBarrierDrawUav(&j);
  AgxD3d12JobBarrierEnd(&j);
  AgxD3d12JobBarrierBegin(&j, DRAW); AgxD3d12JobBarrierEnd(&j);
  if (gap) AgxD3d12JobBarrierAdd(&j, gap == 1 ? TRANSITION : UAV, 0, 4, 8, 77);
  if (resolve_trigger) { /* ResolveQueryData flushes R and copies into C;
                           * 2/3: OV3 defers the copy (3: then drains it
                           * the native way into C) */
    AgxD3d12JobBarrierBegin(&j, QUERY);
    if (resolve_trigger >= 2u) AgxD3d12JobBarrierLocal(&j);
    AgxD3d12JobBarrierBatch(&j, 1u, values);
    AgxD3d12JobBarrierEnd(&j);
    AgxD3d12JobBarrierBegin(&j, DISPATCH);
    if (resolve_trigger == 3u) AgxD3d12JobBarrierForeign(&j);
  } else {
    AgxD3d12JobBarrierBegin(&j, DISPATCH);        /* C's first dispatch */
    AgxD3d12JobBarrierBatch(&j, 1u, values);      /* R submitted */
  }
  AgxD3d12JobBarrierEnd(&j);
  if (copy_inside) { AgxD3d12JobBarrierBegin(&j, COPY); AgxD3d12JobBarrierEnd(&j); }
  if (inside_type != 9u) AgxD3d12JobBarrierAdd(&j, inside_type, 0, 8, 4, 78);
  AgxD3d12JobBarrierBegin(&j, DISPATCH); AgxD3d12JobBarrierEnd(&j);
  AgxD3d12JobBarrierAdd(&j, TRANSITION, 0, 8, 0x80, 79); /* C -> next R gap */
  AgxD3d12JobBarrierAdd(&j, TRANSITION, 0, 8, 0x80, 80);
  AgxD3d12JobBarrierBegin(&j, DRAW);              /* next R's first draw */
  {
    unsigned r = AgxD3d12JobBarrierIndependent(&j);
    last_reason = AgxD3d12JobBarrierRefusal(&j);
    if ((last_reason == 0u) != (r == 1u)) return 99u;
    AgxD3d12JobBarrierBatch(&j, 2u, values);      /* C submitted */
    return r;
  }
}
int main(void) {
  /* known, uav draw, gap (0 none / 1 transition / 2 UAV), inside (9 none), prev compute */
  printf("base %u\n", scenario(1, 0, 0, 9, 0));
  printf("inside_uav %u\n", scenario(1, 0, 0, UAV, 0));
  printf("inside_transition %u\n", scenario(1, 0, 0, TRANSITION, 0));
  printf("inside_aliasing %u\n", scenario(1, 0, 0, 1, 0));
  printf("gap_transition %u\n", scenario(1, 0, 1, 9, 0));
  printf("gap_uav %u\n", scenario(1, 0, 2, 9, 0));
  printf("render_uav %u\n", scenario(1, 1, 0, 9, 0));
  printf("unknown %u\n", scenario(0, 0, 0, 9, 0));
  printf("after_compute_render %u\n", scenario(1, 0, 0, 9, 1));
  printf("resolve_trigger %u\n", scenario_ex(1, 0, 0, 9, 0, 1, 0, 0));
  printf("resolve_deferred %u\n", scenario_ex(1, 0, 0, 9, 0, 2, 0, 0));
  printf("resolve_drained %u\n", scenario_ex(1, 0, 0, 9, 0, 3, 0, 0));
  printf("resolve_deferred_unknown %u\n", scenario_ex(0, 0, 0, 9, 0, 2, 0, 0));
  printf("copy_inside %u\n", scenario_ex(1, 0, 0, 9, 0, 0, 1, 0));
  printf("early_dispatch %u\n", scenario_ex(1, 0, 0, 9, 0, 0, 0, 1));
  /* EXP2198 receipt measure-d3d12-ov2-refusal: the reason per scenario */
  printf("reasons");
  scenario(1, 0, 0, 9, 0); printf(" %u", last_reason);
  scenario(0, 0, 0, 9, 0); printf(" %u", last_reason);
  scenario(1, 0, 1, 9, 0); printf(" %u", last_reason);
  scenario(1, 1, 0, 9, 0); printf(" %u", last_reason);
  scenario_ex(1, 0, 0, 9, 0, 0, 0, 1); printf(" %u", last_reason);
  scenario_ex(1, 0, 0, 9, 0, 1, 0, 0); printf(" %u", last_reason);
  scenario(1, 0, 0, TRANSITION, 0); printf(" %u", last_reason);
  scenario_ex(1, 0, 0, 9, 0, 2, 0, 0); printf(" %u\n", last_reason);
  /* compute after compute is never independent of a render */
  AgxD3d12JobBarriersInit(&j); AgxD3d12JobBarrierExecution(&j, 1);
  AgxD3d12JobBarrierBegin(&j, DISPATCH); AgxD3d12JobBarrierBatch(&j, 2u, values);
  printf("compute_after_compute %u\n", AgxD3d12JobBarrierIndependent(&j));
  /* a fresh accumulator (no execution yet) is unknown */
  AgxD3d12JobBarriersInit(&j);
  printf("fresh %u\n", AgxD3d12JobBarrierIndependent(&j));
  return 0;
}
'''

KMD_RULE = r'''
#include <assert.h>
#include <stdio.h>
#include "render_job_window.h"
static ADMISSION_JOB_WINDOW_JOB job(unsigned kind, unsigned long long ctx, unsigned long long proc,
                                   unsigned independent) {
  ADMISSION_JOB_WINDOW_JOB j = {0};
  j.Fence = 9; j.Kind = kind; j.Context = ctx; j.Process = proc; j.Independent = independent;
  return j;
}
int main(void) {
  const unsigned C3D = ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_3D;
  ADMISSION_JOB_WINDOW_JOB r = job(ADMISSION_JOB_WINDOW_KIND_RENDER, 5, 7, 0);
  ADMISSION_JOB_WINDOW_JOB rp = job(ADMISSION_JOB_WINDOW_KIND_RENDER_PRELUDE, 5, 7, 0);
  ADMISSION_JOB_WINDOW_JOB c = job(ADMISSION_JOB_WINDOW_KIND_COMPUTE, 5, 7, 1);
  ADMISSION_JOB_WINDOW_JOB cu = job(ADMISSION_JOB_WINDOW_KIND_COMPUTE, 5, 7, 0);
  ADMISSION_JOB_WINDOW_JOB cx = job(ADMISSION_JOB_WINDOW_KIND_COMPUTE, 6, 7, 1);
  ADMISSION_JOB_WINDOW_JOB cp = job(ADMISSION_JOB_WINDOW_KIND_COMPUTE, 5, 8, 1);
  ADMISSION_JOB_WINDOW_JOB r0 = job(ADMISSION_JOB_WINDOW_KIND_RENDER, 0, 7, 0);
  ADMISSION_JOB_WINDOW_JOB c0 = job(ADMISSION_JOB_WINDOW_KIND_COMPUTE, 0, 7, 1);
  ADMISSION_JOB_WINDOW_JOB pc = job(ADMISSION_JOB_WINDOW_KIND_PRESENT, 5, 7, 0);
  ADMISSION_JOB_WINDOW_JOB cc = job(ADMISSION_JOB_WINDOW_KIND_COMPUTE, 5, 7, 0);
  ADMISSION_JOB_WINDOW_OV2_RECEIPT receipt;
  printf("waive %u\n", AdmissionJobWindowOv2Refusal(1, C3D, &r, &c));
  printf("waive_prelude %u\n", AdmissionJobWindowOv2Refusal(1, C3D, &rp, &c));
  printf("off %u\n", AdmissionJobWindowOv2Refusal(0, C3D, &r, &c));
  printf("class_cc %u\n", AdmissionJobWindowOv2Refusal(1, ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_COMPUTE, &cc, &c));
  printf("class_ta %u\n", AdmissionJobWindowOv2Refusal(1, ADMISSION_JOB_WINDOW_BARRIER_TA_ON_COMPUTE, &c, &r));
  printf("unflagged %u\n", AdmissionJobWindowOv2Refusal(1, C3D, &r, &cu));
  printf("next_render %u\n", AdmissionJobWindowOv2Refusal(1, C3D, &r, &r));
  printf("running_present %u\n", AdmissionJobWindowOv2Refusal(1, C3D, &pc, &c));
  printf("other_context %u\n", AdmissionJobWindowOv2Refusal(1, C3D, &r, &cx));
  printf("other_process %u\n", AdmissionJobWindowOv2Refusal(1, C3D, &r, &cp));
  printf("no_context %u %u\n", AdmissionJobWindowOv2Refusal(1, C3D, &r0, &c0),
         AdmissionJobWindowOv2Refusal(1, C3D, &r, &c0));
  printf("null %u\n", AdmissionJobWindowOv2Refusal(1, C3D, 0, &c));
  printf("wait %u %u %u %u\n", AdmissionJobWindowOv2WaitValue(C3D, 0x500, 0x400),
         AdmissionJobWindowOv2WaitValue(C3D, 0x500, 0),
         AdmissionJobWindowOv2WaitValue(ADMISSION_JOB_WINDOW_BARRIER_COMPUTE_ON_COMPUTE, 0x500, 0x400),
         AdmissionJobWindowOv2WaitValue(ADMISSION_JOB_WINDOW_BARRIER_TA_ON_3D, 0x500, 0x400));
  AdmissionJobWindowOv2ReceiptInitialize(&receipt);
  assert(receipt.Version == ADMISSION_JOB_WINDOW_OV2_RECEIPT_VERSION && receipt.Bytes == sizeof(receipt));
  assert(AdmissionJobWindowOv2Note(&receipt, ADMISSION_JOB_WINDOW_OV2_WAIVE, 41, 0x500, 0x400) == 1u);
  assert(AdmissionJobWindowOv2Note(&receipt, ADMISSION_JOB_WINDOW_OV2_CONTEXT, 42, 0, 0) == 1u);
  printf("receipt %u %u %u %u %u\n", receipt.Considered, receipt.Waived,
         receipt.Refused[ADMISSION_JOB_WINDOW_OV2_CONTEXT], receipt.LastFence, receipt.LastWaitValue);
  return 0;
}
'''


class Ov2NoHazardLead(unittest.TestCase):
    def test_umd_independence_rule(self):
        ran = build_run(UMD_RULE, [], [D3D12])
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        lines = [line for line in ran.stdout.split('\n') if line]
        reasons = [line for line in lines if line.startswith('reasons')]
        self.assertEqual(reasons, ['reasons 0 1 3 4 5 6 7 0'])
        got = dict(line.split() for line in lines if not line.startswith('reasons'))
        self.assertEqual(got, {
            'base': '1', 'inside_uav': '1', 'inside_transition': '0', 'inside_aliasing': '0',
            'gap_transition': '0', 'gap_uav': '0', 'render_uav': '0', 'unknown': '0',
            'after_compute_render': '1', 'compute_after_compute': '0', 'fresh': '0',
            'resolve_trigger': '0', 'copy_inside': '0', 'early_dispatch': '0',
            'resolve_deferred': '1', 'resolve_drained': '0', 'resolve_deferred_unknown': '0'})

    def test_kmd_waiver_decision(self):
        ran = build_run(KMD_RULE, [RENDER / 'src/render_job_window.c'],
                        [RENDER / 'include', SHARED / 'include'])
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        lines = [line for line in ran.stdout.split('\n') if line]
        self.assertEqual(lines, [
            'waive 0', 'waive_prelude 0', 'off 1', 'class_cc 2', 'class_ta 2', 'unflagged 3',
            'next_render 3', 'running_present 4', 'other_context 5', 'other_process 5',
            'no_context 5 5', 'null 3', 'wait 1024 1280 1280 1280', 'receipt 2 1 1 41 1024'])

    def test_wiring(self):
        """The KMD waives only through the decision, behind the switch on Q2c;
        the UMD flags only compute envelopes and only with the feature."""
        backend = (RENDER / 'src/backend_platform_windows.c').read_text()
        self.assertIn('runtime->Ov2Enabled = runtime->Initdata.RenderSharedMemory.ComputeOwnQueue &&\n'
                      '      AdmissionPlatformSwitchEnabled(Context, L"Wom1Ov2NoHazardLead")', backend)
        self.assertIn('AdmissionJobWindowOv2Refusal(1u, (unsigned int)barrier,', backend)
        self.assertEqual(backend.count('runtime->Ov2Waive = TRUE;'), 1)
        batch = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c').read_text()
        self.assertEqual(batch.count('DRM_ASAHI_BARRIER_NONE : 0,0);'), 1)
        self.assertIn('AgxWin32BatchKindHook(2u) &&\n      AgxWin32Ov2Feature', batch)

    def test_start_publishes_feature_after_reset(self):
        """EXP2196: RuntimeStart set GpuIndependentSubqueue from Ov2Enabled and
        then zeroed it again with the soft-fault reset, so the UMD never saw
        the OV2 device feature. The last write on the success path before
        'return STATUS_SUCCESS' must publish Ov2Enabled, not 0."""
        src = (ROOT / 'drivers/apple-agx/render-admission/src/backend_platform_windows.c').read_text()
        start = src.index('NTSTATUS AdmissionPlatformRuntimeStart(')
        body = src[start:src.index('\nFail:', start)]
        reset = body.rindex('InterlockedExchange(&Context->GpuSoftFaults, 0);')
        after = body.index('GpuIndependentSubqueue', reset)
        line = body[body.rindex('\n', 0, after):body.index('\n', after)]
        self.assertIn('runtime->Ov2Enabled ? 1 : 0', line)

    def test_ov3_resolve_deferral_wiring(self):
        """OV3: a deferred pipeline-statistics copy must still land before
        anything can observe the destination or reset the query: a barrier on
        its heap, a Begin/End of a pending query, the replay's end, present
        and destroy copy it the native way (foreign to OV2), and only
        executions whose barriers are all replayed defer at all."""
        owner = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        self.assertIn('owner->ResolveDefer = begin && known && AgxWin32Ov2Feature && '
                      'AgxD3d12OwnerJobBarriersEnabled();', owner)
        self.assertIn('bool defer = heap->Type == 2 && heap->Slots == values && owner->ResolveDefer', owner)
        for reason in ('kResolveDrainEnd', 'kResolveDrainNative', 'kResolveDrainBarrier',
                       'kResolveDrainQuery', 'kResolveDrainRefused', 'kResolveDrainPresent'):
            self.assertIn('owner_resolve_drain(owner, %s)' % reason, owner)
        self.assertEqual(owner.count('owner_resolve_drain(owner, kResolveDrainUnflushed)'), 2)
        drain = owner[owner.index('static void owner_resolve_drain('):]
        drain = drain[:drain.index('\n}\n')]
        self.assertIn('owner_job_foreign();', drain)
        self.assertLess(owner.index('owner_job_local();'),
                        owner.index('AgxWin32AsahiQueryResolveFlushWriters(pipe, q)'))
        ddi = (D3D12 / 'agx_d3d12_ddi.cpp').read_text()
        self.assertEqual(ddi.count('ReplayResolveBarrier(static_cast<AGX_D3D12_OWNER *>(list->device->owner), r);'), 2)
        self.assertIn('AgxD3d12OwnerResolveExecution(static_cast<AGX_D3D12_OWNER *>(queue->device->owner), 1, known);', ddi)
        self.assertLess(ddi.index('AgxD3d12OwnerResolveExecution(static_cast<AGX_D3D12_OWNER *>(queue->device->owner), 0, 0);'),
                        ddi.index('AgxD3d12OwnerUploadBoundary(static_cast<AGX_D3D12_OWNER *>(queue->device->owner), 0);'))
        self.assertIn('command.kind == kCommandDraw || command.kind == kCommandIndirectExpand))\n'
                      '                AgxD3d12OwnerResolveAfterDraw(owner);', ddi)
        group = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_query_group.h').read_text()
        into = group[group.index('AgxWin32AsahiQueryResolveIntoRender(struct pipe_context *pctx'):]
        self.assertIn('agx_batch_is_compute(batch) || !batch->draws', into)
        self.assertIn('query->writer_generation[idx] == ctx->batches.generation[idx]', into)
        self.assertLess(into.index('agx_add_query_to_batch(batch, query);'), into.index('libagx_copy_query_gl('))
        self.assertIn('AGX_BARRIER_ALL | AGX_PREGFX', into)


    def test_ov3b_never_creates_a_prelude(self):
        """EXP2197: OV3 put the resolve copies into renders without a compute
        prelude, adding ~1.3 prelude renders per frame (3.1 -> 4.4), each
        starting behind the overlapping compute, and lost ~9 % at HD. OV3b
        (ExistingPrelude) must copy only into a render batch whose CDM exists
        already: it never allocates one, never touches a compute batch or a
        batch without draws, and without OV3b the old placement stays."""
        group = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_query_group.h').read_text()
        start = group.index('int\nAgxWin32AsahiQueryResolveIntoRender(')
        body = group[start:group.index('\n}\n', start) + 3]
        code = r"""
#include <stdio.h>
#include <stdint.h>
#include <stddef.h>
struct pipe_context { int unused; };
struct pipe_query { int unused; };
struct pipe_resource { unsigned width0; };
enum { PIPE_QUERY_PIPELINE_STATISTICS_SINGLE = 9, PIPE_QUERY_TYPE_U64 = 4,
       AGX_BARRIER_ALL = 1, AGX_PREGFX = 2 };
struct agx_encoder { void *bo; };
struct agx_batch { int compute, draws; struct agx_encoder cdm; };
struct agx_query { int type; void *bo; struct { uint64_t gpu; } ptr; unsigned writer_generation[4]; };
struct agx_context { int any_faults; struct agx_batch *batch; struct { unsigned generation[4]; } batches;
                     struct pipe_context base_unused; struct { void *screen; } base; };
struct agx_resource { struct pipe_resource base; };
static struct agx_context C; static int allocs, copies, adds; static char bo;
static struct agx_context *agx_context(struct pipe_context *p) { (void)p; return &C; }
static int agx_batch_is_compute(struct agx_batch *b) { return b->compute; }
static unsigned agx_batch_idx(struct agx_batch *b) { (void)b; return 0; }
static void *agx_device(void *s) { return s; }
static struct agx_encoder agx_encoder_allocate(struct agx_batch *b, void *d) {
  (void)b; (void)d; ++allocs; struct agx_encoder e = {&bo}; return e; }
static struct agx_resource *agx_resource(struct pipe_resource *r) { return (struct agx_resource *)r; }
static void agx_add_query_to_batch(struct agx_batch *b, struct agx_query *q) { (void)b; (void)q; ++adds; }
static void agx_batch_writes_range(struct agx_batch *b, struct agx_resource *r, unsigned o, unsigned n) {
  (void)b; (void)r; (void)o; (void)n; }
static uint64_t agx_map_gpu(struct agx_resource *r) { (void)r; return 0x1000; }
static int agx_1d(int n) { return n; }
static void libagx_copy_query_gl(struct agx_batch *b, int g, int bar, uint64_t s, uint64_t d, int t, int x) {
  (void)b; (void)g; (void)bar; (void)s; (void)d; (void)t; (void)x; ++copies; }
""" + body + r"""
static int run(int compute, int draws, int has_cdm, int existing) {
  static struct agx_batch b; struct agx_batch z = {0}; b = z;
  b.compute = compute; b.draws = draws; b.cdm.bo = has_cdm ? &bo : NULL; C.batch = &b;
  struct agx_query q = {PIPE_QUERY_PIPELINE_STATISTICS_SINGLE, &bo, {0x2000}, {0}};
  C.batches.generation[0] = 1;
  struct agx_resource r = {{64}}; allocs = copies = adds = 0;
  int st = AgxWin32AsahiQueryResolveIntoRender((struct pipe_context *)&C, (struct pipe_query *)&q,
                                               &r.base, 8, existing);
  printf("%d,%d,%d ", st, allocs, copies);
  return st;
}
int main(void) {
  run(0, 2, 0, 1); run(0, 2, 1, 1); run(1, 0, 1, 1); run(0, 0, 1, 1);
  run(0, 2, 0, 0); run(0, 2, 1, 0); printf("\n"); return 0;
}
"""
        ran = build_run(code, [], [])
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertEqual(ran.stdout.split(), [
            '0,0,0',   # OV3b, render without prelude: keep pending, no CDM
            '1,0,1',   # OV3b, render with a prelude: copy into it
            '0,0,0',   # compute batch: never
            '0,0,0',   # render batch without draws: never
            '1,1,1',   # OV3 (EXP2197): allocates a new prelude
            '1,0,1'])
        owner = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        self.assertIn('owner_resolve_existing_prelude())) == 1)', owner)
        self.assertIn('GetEnvironmentVariableA("APPLE_AGX_D3D12_OV3B", option, sizeof(option)) == 1 && option[0] == \'1\'',
                      owner)

    def test_ov2_ranges_rule(self):
        """EXP2199: C3/C5/C7 were refused only by DrawUav (their render bound
        a UAV). D3D12 orders a render's accesses before later work only
        through barriers naming them: the opt-in ranges rule must refuse
        exactly when a barrier between the render's last draw and the
        compute batch's last dispatch names a range the render accessed (UAV
        barriers: a range it accessed through a UAV; global UAV barriers: any
        UAV access), or something it cannot place; barriers after the
        compute's last dispatch order later work and must not count."""
        code = r"""
#include <stdio.h>
#include "agx_d3d12_job_barriers.h"
enum { DRAW = 5, DISPATCH = 2, QUERY = 9, TRANSITION = 0, UAV = 2 };
static AGX_D3D12_JOB_BARRIERS j;
static unsigned values[AGX_D3D12_JOB_BARRIER_VALUES];
static int A, B, C, D;
static void barrier(unsigned type, unsigned id, const void *heap, unsigned long long lo, unsigned long long hi) {
  AgxD3d12JobBarrierAdd(&j, type, 0, 0, 0, id);
  AgxD3d12JobBarrierRange(&j, type, id, heap, lo, hi);
}
/* mode: which barrier (or access) the case adds */
static void run(int mode) {
  AgxD3d12JobBarriersInit(&j);
  AgxD3d12JobBarrierExecution(&j, 1);
  if (mode == 10) AgxD3d12JobBarrierResolveRange(&j, &D, 0, 64);
  AgxD3d12JobBarrierBegin(&j, DRAW);                 /* R: writes UAV A, reads C */
  AgxD3d12JobBarrierDrawRange(&j, &A, 0, 100, 1);
  AgxD3d12JobBarrierDrawRange(&j, &C, 0, 100, 0);
  if (mode == 9) AgxD3d12JobBarrierDrawRange(&j, 0, 0, 0, 0);
  AgxD3d12JobBarrierDrawUav(&j);
  AgxD3d12JobBarrierEnd(&j);
  if (mode == 8) barrier(TRANSITION, 5, &A, 0, 10);  /* R -> C gap */
  AgxD3d12JobBarrierBegin(&j, QUERY);                /* deferred resolve submits R */
  AgxD3d12JobBarrierLocal(&j);
  AgxD3d12JobBarrierBatch(&j, 1u, values);
  AgxD3d12JobBarrierEnd(&j);
  barrier(TRANSITION, 1, &B, 0, 50);                 /* C's own resources */
  AgxD3d12JobBarrierBegin(&j, DISPATCH); AgxD3d12JobBarrierEnd(&j);
  barrier(UAV, 2, &B, 0, 50);
  if (mode == 1) barrier(TRANSITION, 3, &A, 50, 60);
  if (mode == 2) barrier(UAV, 3, &A, 0, 100);
  if (mode == 3) barrier(UAV, 4, &C, 0, 100);
  if (mode == 4) barrier(UAV, 0, 0, 0, 0);
  if (mode == 5) barrier(TRANSITION, 3, &A, 200, 300);
  if (mode == 6) barrier(TRANSITION, 3, 0, 0, 0);
  if (mode == 10) barrier(TRANSITION, 6, &D, 0, 64);
  AgxD3d12JobBarrierBegin(&j, DISPATCH); AgxD3d12JobBarrierEnd(&j);
  if (mode == 7) barrier(TRANSITION, 3, &A, 0, 100); /* after C's last dispatch */
  barrier(TRANSITION, 7, &A, 0, 100);                /* C -> next R gap (always) */
  AgxD3d12JobBarrierBegin(&j, DRAW);                 /* next R submits C */
  printf("%d %u %u\n", mode, AgxD3d12JobBarrierRefusal(&j), AgxD3d12JobBarrierRefusalRanges(&j));
  AgxD3d12JobBarrierBatch(&j, 2u, values);
}
int main(void) {
  for (int m = 0; m <= 10; ++m) run(m);
  /* a render without UAV access: a global UAV barrier does not order it */
  AgxD3d12JobBarriersInit(&j); AgxD3d12JobBarrierExecution(&j, 1);
  AgxD3d12JobBarrierBegin(&j, DRAW); AgxD3d12JobBarrierDrawRange(&j, &C, 0, 100, 0); AgxD3d12JobBarrierEnd(&j);
  AgxD3d12JobBarrierBegin(&j, DISPATCH); AgxD3d12JobBarrierBatch(&j, 1u, values); AgxD3d12JobBarrierEnd(&j);
  barrier(UAV, 0, 0, 0, 0);
  AgxD3d12JobBarrierBegin(&j, DISPATCH); AgxD3d12JobBarrierEnd(&j);
  AgxD3d12JobBarrierBegin(&j, DRAW);
  printf("global_no_uav %u\n", AgxD3d12JobBarrierRefusalRanges(&j));
  return 0;
}
"""
        ran = build_run(code, [], [D3D12])
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertEqual(ran.stdout.split('\n')[:-1], [
            '0 4 0',    # GravityMark shape: only DrawUav refused it
            '1 4 8',    # transition overlapping R's UAV range inside C
            '2 4 8',    # UAV barrier on R's UAV range
            '3 4 0',    # UAV barrier on a range R only read
            '4 4 8',    # global UAV barrier after a UAV-accessing render
            '5 4 0',    # transition on A outside R's range
            '6 4 8',    # barrier on an unknown resource
            '7 4 0',    # after C's last dispatch: orders later work only
            '8 3 8',    # R -> C gap barrier on R's range
            '9 4 8',    # an access the tracker could not place
            '10 4 8',   # a resolve destination OV3 may copy in R's prelude
            'global_no_uav 0'])
        ddi = (D3D12 / 'agx_d3d12_ddi.cpp').read_text()
        self.assertEqual(ddi.count('AgxD3d12OwnerJobBarrierRange(r.type, r.id, r.heap, r.lo, r.hi);'), 2)
        self.assertIn('JobDrawRanges(d, optional, clears, clear_count);', ddi)
        owner = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        self.assertIn('independent = (owner_ov2_ranges() ? ranged : reason) == 0u ? 1u : 0u;', owner)

    def test_drained_copies_stay_foreign_across_a_render_submit(self):
        """EXP2200: the resolve drained at the replay's end copies into the
        open compute batch (C9) while R8 is still open; R8's submission then
        reset ForeignSince, so C9 -- which reads R6's statistics -- was
        flagged independent of R8 (reason 0, Waived ~1/frame). Driver work
        put into the open compute batch must keep that batch foreign until
        the compute batch itself is submitted."""
        code = r"""
#include <stdio.h>
#include "agx_d3d12_job_barriers.h"
static AGX_D3D12_JOB_BARRIERS j;
static unsigned values[AGX_D3D12_JOB_BARRIER_VALUES];
static int A;
int main(void) {
  AgxD3d12JobBarriersInit(&j);
  AgxD3d12JobBarrierExecution(&j, 1);
  AgxD3d12JobBarrierBegin(&j, 5); AgxD3d12JobBarrierDrawRange(&j, &A, 0, 64, 0); AgxD3d12JobBarrierEnd(&j);
  AgxD3d12JobBarrierForeign(&j);          /* end-of-replay drain into the open compute batch */
  AgxD3d12JobBarrierBatch(&j, 1u, values); /* R8 submitted by the final flush */
  printf("%u %u\n", AgxD3d12JobBarrierRefusal(&j), AgxD3d12JobBarrierRefusalRanges(&j));
  AgxD3d12JobBarrierBatch(&j, 2u, values); /* C9 */
  /* the next compute batch is clean again */
  AgxD3d12JobBarrierBegin(&j, 5); AgxD3d12JobBarrierEnd(&j);
  AgxD3d12JobBarrierBegin(&j, 2); AgxD3d12JobBarrierBatch(&j, 1u, values); AgxD3d12JobBarrierEnd(&j);
  AgxD3d12JobBarrierBegin(&j, 5);
  printf("%u\n", AgxD3d12JobBarrierRefusalRanges(&j));
  return 0;
}
"""
        ran = build_run(code, [], [D3D12])
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertEqual(ran.stdout.split('\n')[:-1], ['6 6', '0'])

    def test_ov4_split_at_the_first_ordering_barrier(self):
        """OV4: a compute batch may be split only at the first barrier that
        orders the preceding render's accesses, only after at least one of
        its dispatches, and only when that prefix is independent under the
        ranges rule. The prefix is judged without the splitting barrier
        (independent), the rest follows a compute batch (reason 2: never
        flagged, so it keeps its lead). EXP2200 receipt values 12-15 then
        report the prefix length of each compute-after-render batch."""
        code = r"""
#include <stdio.h>
#include "agx_d3d12_job_barriers.h"
static AGX_D3D12_JOB_BARRIERS j;
static unsigned values[AGX_D3D12_JOB_BARRIER_VALUES];
static int A, B;
static void barrier(unsigned type, unsigned id, const void *heap, unsigned long long lo, unsigned long long hi) {
  AgxD3d12JobBarrierAdd(&j, type, 0, 0, 0, id);
  AgxD3d12JobBarrierRange(&j, type, id, heap, lo, hi);
}
static void dispatch(void) { AgxD3d12JobBarrierBegin(&j, 2); AgxD3d12JobBarrierEnd(&j); }
/* R (reads A), its submission by C's first dispatch */
static void prologue(int foreign) {
  AgxD3d12JobBarriersInit(&j); AgxD3d12JobBarrierExecution(&j, 1);
  AgxD3d12JobBarrierBegin(&j, 5); AgxD3d12JobBarrierDrawRange(&j, &A, 0, 64, 0); AgxD3d12JobBarrierEnd(&j);
  if (foreign) AgxD3d12JobBarrierForeign(&j); /* driver work (a drained resolve) in the open compute batch */
  AgxD3d12JobBarrierBegin(&j, 2); AgxD3d12JobBarrierBatch(&j, 1u, values); AgxD3d12JobBarrierEnd(&j);
}
int main(void) {
  /* three dispatches (one in the prologue), an unrelated barrier between them, then a barrier on A */
  prologue(0);
  dispatch();
  barrier(0, 1, &B, 0, 16);
  printf("unrelated %u\n", AgxD3d12JobSplitHere(&j));
  dispatch();
  barrier(0, 2, &A, 0, 64);
  printf("ordering %u prefix %u\n", AgxD3d12JobSplitHere(&j), AgxD3d12JobRangePrefix(&j));
  printf("prefix_reason %u\n", AgxD3d12JobBarrierRefusalRanges(&j));
  AgxD3d12JobBarrierBatch(&j, 2u, values);       /* the split submits the prefix */
  dispatch();                                     /* the rest */
  AgxD3d12JobBarrierBegin(&j, 5);
  printf("suffix_reason %u split %u\n", AgxD3d12JobBarrierRefusalRanges(&j), AgxD3d12JobSplitHere(&j));
  AgxD3d12JobBarrierBatch(&j, 2u, values);
  AgxD3d12JobBarrierEnd(&j);
  /* a barrier on A before any dispatch of C: nothing to split off */
  AgxD3d12JobBarriersInit(&j); AgxD3d12JobBarrierExecution(&j, 1);
  AgxD3d12JobBarrierBegin(&j, 5); AgxD3d12JobBarrierDrawRange(&j, &A, 0, 64, 0); AgxD3d12JobBarrierEnd(&j);
  barrier(0, 2, &A, 0, 64);
  printf("gap %u\n", AgxD3d12JobSplitHere(&j));
  /* driver work in the compute batch since the render: never */
  prologue(1);
  barrier(0, 2, &A, 0, 64);
  printf("foreign %u\n", AgxD3d12JobSplitHere(&j));
  /* the receipt's prefix of a batch the ordering barrier ends after 3 of 4
   * dispatches (no split: OV4 off) */
  prologue(0);
  dispatch(); dispatch();
  barrier(2, 3, &A, 0, 64);                       /* UAV barrier on a read range: not ordering */
  barrier(0, 2, &A, 0, 64);
  dispatch();
  AgxD3d12JobBarrierBegin(&j, 5);
  printf("receipt %u of %u\n", AgxD3d12JobRangePrefix(&j), j.DispatchesSince);
  return 0;
}
"""
        ran = build_run(code, [], [D3D12])
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertEqual(ran.stdout.split('\n')[:-1], [
            'unrelated 0', 'ordering 1 prefix 3', 'prefix_reason 0', 'suffix_reason 2 split 0',
            'gap 0', 'foreign 0', 'receipt 3 of 4'])
        ddi = (D3D12 / 'agx_d3d12_ddi.cpp').read_text()
        self.assertEqual(ddi.count('AgxD3d12OwnerJobSplit(static_cast<AGX_D3D12_OWNER *>(list->device->owner)); /* OV4 */'), 2)
        owner = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        self.assertIn('if (!owner || !AgxWin32Ov2Feature || !owner_ov4()) return;', owner)

    def test_ov5_render_ta_verdict(self):
        """EXP2201: Metal starts R16's vertex/TA work while c15 runs, though
        c15 writes R16's color attachment, and orders only the 3D stage.
        The OV5 verdict (receipt) must free a render's TA exactly when no
        vertex-stage access of it conflicts with the compute batch before it
        (the compute's UAV writes vs any vertex access, any compute access vs
        a vertex-stage write), never for a render with a compute prelude
        (it queues behind the compute), unplaceable accesses, driver work, a
        global UAV barrier with UAVs on both sides, or a render after a
        render."""
        code = r"""
#include <stdio.h>
#include "agx_d3d12_job_barriers.h"
static AGX_D3D12_JOB_BARRIERS j;
static unsigned values[AGX_D3D12_JOB_BARRIER_VALUES];
static int U, S, V;
enum { VX = AGX_D3D12_JOB_VERTEX };
static unsigned run(int mode) {
  AgxD3d12JobBarriersInit(&j); AgxD3d12JobBarrierExecution(&j, 1);
  AgxD3d12JobBarrierBegin(&j, 2);                    /* C: writes U, reads S */
  AgxD3d12JobBarrierDispatchRange(&j, &U, 0, 64, 1);
  AgxD3d12JobBarrierDispatchRange(&j, &S, 0, 64, 0);
  AgxD3d12JobBarrierEnd(&j);
  if (mode == 6) AgxD3d12JobBarrierForeign(&j);
  if (mode == 7) { AgxD3d12JobBarrierAdd(&j, 2, 0, 0, 0, 0); AgxD3d12JobBarrierRange(&j, 2, 0, 0, 0, 0); }
  AgxD3d12JobBarrierBegin(&j, 5);                    /* R: reads V at vertex, renders into U */
  if (mode != 2) AgxD3d12JobBarrierBatch(&j, 2u, values); /* C submitted by R's first draw */
  AgxD3d12JobBarrierDrawRange(&j, &V, 0, 64, VX);
  AgxD3d12JobBarrierDrawRange(&j, &U, 0, 64, 0);     /* the c15 -> R16 attachment */
  if (mode == 1) AgxD3d12JobBarrierDrawRange(&j, &U, 32, 48, VX);       /* vertex read of C's write */
  if (mode == 3) AgxD3d12JobBarrierDrawRange(&j, &S, 0, 16, 1u | VX);   /* stream-out into what C read */
  if (mode == 4) AgxD3d12JobBarrierDrawRange(&j, 0, 0, 0, VX);
  if (mode == 5) AgxD3d12JobBarrierDrawRange(&j, &S, 0, 16, VX);        /* vertex read of what C read */
  if (mode == 7) AgxD3d12JobBarrierDrawRange(&j, &V, 128, 192, 1u);     /* a PS UAV */
  AgxD3d12JobBarrierEnd(&j);
  AgxD3d12JobBarrierBegin(&j, 2);                    /* the next C submits R */
  {
    unsigned r = AgxD3d12JobRenderTaRefusal(&j, mode == 9);
    AgxD3d12JobBarrierBatch(&j, 1u, values);
    AgxD3d12JobBarrierEnd(&j);
    return r;
  }
}
int main(void) {
  const int modes[] = {0, 1, 2, 3, 4, 5, 6, 7, 9};
  for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) printf("%d %u\n", modes[i], run(modes[i]));
  return 0;
}
"""
        ran = build_run(code, [], [D3D12])
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        self.assertEqual(ran.stdout.split('\n')[:-1], [
            '0 0',    # c15 -> R16: writes only a fragment-stage target
            '1 10',   # vertex-stage read of the compute's UAV write
            '2 2',    # the previous batch was not a compute batch
            '3 10',   # vertex-stage write (stream-out) over what the compute read
            '4 11',   # an unplaceable vertex access
            '5 0',    # vertex read of what the compute only read
            '6 11',   # driver work in the compute batch: its writes are unplaced
            '7 12',   # global UAV barrier, UAVs on both sides
            '9 9'])   # a compute prelude
        owner = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()
        self.assertIn('const unsigned ta = AgxD3d12JobRenderTaRefusal(&owner_job_barriers, prelude);', owner)
        batch = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c').read_text()
        self.assertIn('AgxWin32BatchKindHook(1u | (batch->cdm.bo ? 0x100u : 0u));', batch)

if __name__ == '__main__':
    unittest.main()
