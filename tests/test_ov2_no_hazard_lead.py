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
        got = dict(line.split() for line in ran.stdout.split('\n') if line)
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

if __name__ == '__main__':
    unittest.main()
