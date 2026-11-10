"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
N4c-v4 (EXP2327 s0): the cross-execution access model named a buffer no draw
reads and refused every frame. The tracker recorded all 16 IA slots of every
draw, but a D3D12 draw reads vertex data only through its input layout's
InputSlot (Asahi fetches only the attributes' buffers): a stale binding in a
slot the layout does not name made 8/2 fire. And a barrier on a range only
the compute uses (its transition to UNORDERED_ACCESS) refused 8/3 although it
orders nothing between the kept render and the compute. The fixes must stay
conservative where the layout is unknown (no pipeline, too many elements, a
slot or register the tracker cannot represent, a register hole that reads
slot 0), must leave every slot in place with the cross-lead switch off, must
still refuse a barrier on a range both sides access and a global UAV
barrier, and the old 8/3 rule must stay visible as receipt bit 23 without
ever entering the verdict ((mask & ~bit 23) == 0 iff the verdict is 0).
measure-batch-deps-exact must tell a Bloom collision from a real shared BO.
"""
import unittest
from pathlib import Path

from test_ov2_no_hazard_lead import build_run

ROOT = Path(__file__).resolve().parents[1]
D3D12 = ROOT / 'drivers/apple-agx/d3d12'
WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys'
OWNER = WINSYS / 'agx_d3d12_windows.cpp'
DDI = D3D12 / 'agx_d3d12_ddi.cpp'
BATCH = WINSYS / 'agx_win32_gpuva_batch.c'

COMMON = r"""
#include <stdio.h>
#include "agx_d3d12_job_barriers.h"
enum { DRAW = 5, DISPATCH = 2, TRANSITION = 0, UAV = 2 };
#define VIEW AGX_D3D12_JOB_SRC_VIEW
#define TARGET AGX_D3D12_JOB_SRC_TARGET
#define VB (AGX_D3D12_JOB_VERTEX | AGX_D3D12_JOB_SRC_VB)
#define SHARED __attribute__((unused))
static AGX_D3D12_JOB_BARRIERS j;
static unsigned values[AGX_D3D12_JOB_BARRIER_VALUES];
static SHARED int A, H, RT, V;
static SHARED void batch(unsigned kind, unsigned clean) {
  AgxD3d12JobCrossBatch(&j, kind, clean);
  AgxD3d12JobBarrierBatch(&j, kind, values);
}
static SHARED void barrier(unsigned type, unsigned id, const void *heap, unsigned long long lo, unsigned long long hi) {
  AgxD3d12JobBarrierAdd(&j, type, 0, 0, 0, id);
  AgxD3d12JobBarrierRange(&j, type, id, heap, lo, hi);
}
/* the replay's vertex-buffer loop (agx_d3d12_ddi.cpp JobDrawRanges): slot 0
 * binds V, slot 1 binds H; the layout is elements {slot, reg} */
static SHARED unsigned draw_vb(unsigned cross, unsigned layout, unsigned elements, const unsigned *slot,
                               const unsigned *reg) {
  const void *bound[2] = {&V, &H};
  const unsigned mask = AgxD3d12JobVbSlots(cross, layout, elements, 16u, slot, reg);
  unsigned v, skipped = 0u, first = 0u;
  const void *heap = 0;
  for (v = 0; v < 2u; ++v) {
    if (mask & (1u << v)) AgxD3d12JobBarrierDrawRange(&j, bound[v], 0, 256, VB);
    else if (!skipped++) {
      heap = bound[v];
      first = v;
    }
  }
  if (cross) AgxD3d12JobCrossVbDraw(&j, skipped, heap, first, elements, first < AgxD3d12JobVbCount(mask) ? 1u : 0u);
  return mask;
}
static SHARED unsigned has(const AGX_D3D12_JOB_RANGE_SET *s, const void *heap) {
  unsigned i;
  for (i = 0; i < s->Count; ++i)
    if (s->R[i].Heap == heap) return 1u;
  return 0u;
}
"""

SLOTS = COMMON + r"""
int main(void) {
  const unsigned s0[1] = {0}, r0[1] = {0}, s1[1] = {1}, r1[1] = {1}, s03[2] = {0, 3}, r01[2] = {0, 1};
  const unsigned s16[1] = {16}, r32[1] = {32};
  unsigned big[17] = {0}, cross, mask;
  printf("layout %x\n", AgxD3d12JobVbSlots(1, 1, 1, 16, s0, r0));
  printf("off %x\n", AgxD3d12JobVbSlots(0, 1, 1, 16, s0, r0));
  printf("nopso %x\n", AgxD3d12JobVbSlots(1, 0, 0, 16, s0, r0));
  printf("toomany %x\n", AgxD3d12JobVbSlots(1, 1, 17, 16, big, big));
  printf("slot16 %x reg32 %x\n", AgxD3d12JobVbSlots(1, 1, 1, 16, s16, r0), AgxD3d12JobVbSlots(1, 1, 1, 16, s0, r32));
  printf("hole %x\n", AgxD3d12JobVbSlots(1, 1, 1, 16, s1, r1));
  mask = AgxD3d12JobVbSlots(1, 1, 2, 16, s03, r01);
  printf("two %x count %u\n", mask, AgxD3d12JobVbCount(mask));
  printf("none %x count %u\n", AgxD3d12JobVbSlots(1, 1, 0, 16, s0, r0), AgxD3d12JobVbCount(0));
  /* the render set of a 1-element layout on slot 0 with buffers in slots 0 and 1 */
  for (cross = 0; cross < 3u; ++cross) {
    AgxD3d12JobBarriersInit(&j);
    AgxD3d12JobBarrierExecution(&j, 1);
    AgxD3d12JobCrossBegin(&j, 0);
    AgxD3d12JobBarrierBegin(&j, DRAW);
    draw_vb(cross != 0u, cross != 2u, 1, s0, r0); /* cross 2: no pipeline */
    AgxD3d12JobBarrierEnd(&j);
    printf("render cross %u: V %u H %u same V %u H %u\n", cross, has(&j.Cross.Render, &V), has(&j.Cross.Render, &H),
           has(&j.RangeRender, &V), has(&j.RangeRender, &H));
  }
  return 0;
}
"""

VERDICT = COMMON + r"""
/* execution N: R draws with V in slot 0 and H in slot 1 (layout: mode 0
 * slot 0 only, mode 1 slots 0 and 1, mode 2 slot 0 with the switch off);
 * N+1: C0 writes H, submitted by the next render's first draw */
static AGX_D3D12_JOB_CROSS_MASK m;
static void run(int mode) {
  const unsigned s0[1] = {0}, r0[1] = {0}, s01[2] = {0, 1}, r01[2] = {0, 1};
  unsigned detail = 0, shadow, mask;
  AgxD3d12JobBarriersInit(&j);
  AgxD3d12JobBarrierExecution(&j, 1);
  AgxD3d12JobCrossBegin(&j, 0);
  AgxD3d12JobBarrierBegin(&j, DRAW);
  AgxD3d12JobBarrierDrawRange(&j, &RT, 0, 4096, TARGET);
  if (mode == 1) draw_vb(1, 1, 2, s01, r01);
  else draw_vb(mode != 2, 1, 1, s0, r0);
  AgxD3d12JobBarrierEnd(&j);
  AgxD3d12JobBarrierBegin(&j, DRAW); /* a second draw, nothing skipped */
  AgxD3d12JobBarrierDrawRange(&j, &RT, 0, 4096, TARGET);
  if (mode != 2) AgxD3d12JobCrossVbDraw(&j, 0, 0, 0, 3, 0);
  AgxD3d12JobBarrierEnd(&j);
  batch(1u, 1);
  AgxD3d12JobCrossEnd(&j, 1);
  AgxD3d12JobBarrierExecution(&j, 1);
  AgxD3d12JobCrossBegin(&j, 0);
  AgxD3d12JobBarrierBegin(&j, DISPATCH);
  AgxD3d12JobBarrierDispatchRange(&j, &H, 0, 64, 1u | AGX_D3D12_JOB_SRC_ROOT);
  AgxD3d12JobBarrierEnd(&j);
  AgxD3d12JobBarrierBegin(&j, DRAW);
  shadow = AgxD3d12JobCrossRefusal(&j, &detail);
  mask = AgxD3d12JobCrossMask(&j, &m);
  printf("%d %u.%u %x vb %u %u %s %u %u %u\n", mode, shadow, detail, mask & ~AGX_D3D12_JOB_MASK_NAMED,
         j.Cross.VbShadow.Draws, j.Cross.VbShadow.Skipped, j.Cross.VbShadow.Heap == &H ? "H" : "-",
         j.Cross.VbShadow.Slot, j.Cross.VbShadow.Elements, j.Cross.VbShadow.Below);
  batch(2u, 0);
  AgxD3d12JobBarrierEnd(&j);
}
int main(void) {
  run(0);
  run(1);
  run(2);
  return 0;
}
"""

BARRIERS = COMMON + r"""
/* execution N: R draws RT (target) and reads V; then barriers; N+1: C0
 * writes A (mode 3: reads V instead), submitted by the next render's draw.
 * mode 0: transition on A after R; 1: on A before C0's dispatch; 2: on V
 * after R, C0 reads V; 3: on V before C0, C0 reads V; 4: global UAV barrier,
 * C0 writes A through a UAV; 5: transition on A, then on V, C0 reads V */
static AGX_D3D12_JOB_CROSS_MASK m;
static unsigned contract_broken;
static void run(int mode) {
  unsigned detail = 0, shadow, mask;
  const int reads_v = mode == 2 || mode == 3 || mode == 5;
  AgxD3d12JobBarriersInit(&j);
  AgxD3d12JobBarrierExecution(&j, 1);
  AgxD3d12JobCrossBegin(&j, 0);
  AgxD3d12JobBarrierBegin(&j, DRAW);
  AgxD3d12JobBarrierDrawRange(&j, &RT, 0, 4096, TARGET);
  AgxD3d12JobBarrierDrawRange(&j, &V, 0, 256, VIEW);
  AgxD3d12JobBarrierEnd(&j);
  if (mode == 0 || mode == 5) barrier(TRANSITION, 9, &A, 0, 64);
  if (mode == 2 || mode == 5) barrier(TRANSITION, 8, &V, 0, 256);
  if (mode == 4) barrier(UAV, 0, 0, 0, 0);
  batch(1u, 1);
  AgxD3d12JobCrossEnd(&j, 1);
  AgxD3d12JobBarrierExecution(&j, 1);
  AgxD3d12JobCrossBegin(&j, 0);
  if (mode == 1) barrier(TRANSITION, 9, &A, 0, 64);
  if (mode == 3) barrier(UAV, 8, &V, 0, 256);
  AgxD3d12JobBarrierBegin(&j, DISPATCH);
  if (reads_v) AgxD3d12JobBarrierDispatchRange(&j, &V, 0, 16, VIEW);
  else AgxD3d12JobBarrierDispatchRange(&j, &A, 0, 64, 1u | VIEW);
  AgxD3d12JobBarrierEnd(&j);
  AgxD3d12JobBarrierBegin(&j, DRAW);
  shadow = AgxD3d12JobCrossRefusal(&j, &detail);
  mask = AgxD3d12JobCrossMask(&j, &m);
  if (!j.Cross.Kept && ((mask & ~AGX_D3D12_JOB_MASK_NAMED) == 0u) != (shadow == 0u)) contract_broken = 1u;
  printf("%d %u.%u %x", mode, shadow, detail, mask);
  if (mask & AGX_D3D12_JOB_MASK_NAMED)
    printf(" named %u where %u %s %llu-%llu kind %u kept %u", m.BarrierNamed, m.BarrierWhere,
           m.Barrier.Heap == &A ? "A" : m.Barrier.Heap == &V ? "V" : "?", m.Barrier.Lo, m.Barrier.Hi, m.Barrier.Uav,
           m.BarrierKept);
  printf("\n");
  batch(2u, 0);
  AgxD3d12JobBarrierEnd(&j);
}
int main(void) {
  int mode;
  for (mode = 0; mode <= 5; ++mode) run(mode);
  printf("%s\n", contract_broken ? "contract broken" : "contract");
  return 0;
}
"""

EXACT = r"""
#include <stdio.h>
#include "agx_win32_batch_deps.h"
static AGX_WIN32_BATCH_DEPS d;
static AGX_WIN32_BATCH_EXACT e;
static uint32_t v[AGX_WIN32_BATCH_EXACT_VALUES];
static unsigned batch(unsigned kind, const unsigned *r, unsigned nr, const unsigned *w, unsigned nw) {
  unsigned i;
  AgxWin32BatchDepsBegin(&d, kind);
  AgxWin32BatchExactBegin(&e);
  for (i = 0; i < nr; ++i) {
    AgxWin32BatchDepsAdd(&d, r[i], 0);
    AgxWin32BatchExactAdd(&e, r[i], 0);
  }
  for (i = 0; i < nw; ++i) {
    AgxWin32BatchDepsAdd(&d, w[i], 1);
    AgxWin32BatchExactAdd(&e, w[i], 1);
  }
  AgxWin32BatchExactCommit(&e, kind, v);
  return AgxWin32BatchDepsCommit(&d);
}
static void show(const char *name, unsigned bloom) {
  unsigned i;
  printf("%s bloom %u exact", name, bloom & 1u);
  for (i = 0; i < AGX_WIN32_BATCH_EXACT_VALUES; ++i) printf(" %u", (unsigned)v[i]);
  printf("\n");
}
int main(void) {
  unsigned a = 7u, b = 8u, i;
  unsigned many[300];
  const unsigned shared = 40u, other = 41u;
  while (AgxWin32BatchDepsBit(b) != AgxWin32BatchDepsBit(a)) ++b; /* a Bloom collision */
  show("first", batch(1, 0, 0, &a, 1));
  show("collision", batch(2, &b, 1, 0, 0));
  show("raw", batch(1, &other, 1, &shared, 1));       /* writes 40 */
  show("read", batch(2, &shared, 1, 0, 0));           /* reads 40: 2 */
  show("war", batch(1, 0, 0, &shared, 1));            /* writes what was read: 1 */
  show("waw", batch(2, 0, 0, &shared, 1));            /* both wrote: 3 */
  for (i = 0; i < 300u; ++i) many[i] = 1000u + i;
  show("overflow", batch(1, many, 300, many, 70));
  show("after", batch(2, &a, 1, 0, 0));
  printf("collision %u %u\n", a, b);
  return 0;
}
"""


class N4cV4VbLayout(unittest.TestCase):
    def run_c(self, code, include=D3D12):
        ran = build_run(code, [], [include])
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        return ran.stdout.split('\n')[:-1]

    def test_vb_slots_follow_the_input_layout(self):
        self.assertEqual(self.run_c(SLOTS), [
            'layout 1',          # a 1-element layout on slot 0
            'off ffff',          # the cross-lead switch off: every slot, as before
            'nopso ffff',        # no pipeline: every slot
            'toomany ffff',      # more elements than the pipeline tracks
            'slot16 ffff reg32 ffff',
            'hole 3',            # register 0 is a hole: the owner fills it from slot 0
            'two 9 count 4',     # slots 0 and 3; Gallium gets 4 buffers
            'none 0 count 0',    # no input layout: no vertex buffer is read
            'render cross 0: V 1 H 1 same V 1 H 1',
            'render cross 1: V 1 H 0 same V 1 H 0',
            'render cross 2: V 1 H 1 same V 1 H 1'])

    def test_unreferenced_slot_does_not_order_the_compute(self):
        self.assertEqual(self.run_c(VERDICT), [
            '0 0.0 0 vb 2 1 H 1 1 0',          # H only in a slot the layout does not name
            '1 8.2 40000 vb 2 0 - 0 0 0',      # the layout names slot 1: a true dependency, bit 18
            '2 8.2 40000 vb 0 0 - 0 0 0'])     # switch off: as before

    def test_barrier_refuses_only_on_a_shared_range(self):
        lines = self.run_c(BARRIERS)
        self.assertEqual(lines, [
            '0 0.0 800000 named 1 where 0 A 0-64 kind 0 kept 0',     # only C0 uses A: receipt bit 23 only
            '1 0.0 800000 named 1 where 1 A 0-64 kind 0 kept 0',     # the same before C0's dispatch
            '2 8.3 a00000 named 1 where 0 V 0-256 kind 0 kept 1',    # both access V: 8/3, bits 21 and 23
            '3 8.3 a00000 named 1 where 1 V 0-256 kind 1 kept 1',    # a UAV barrier on V before C0
            '4 8.4 400000',                                          # a global UAV barrier: 8/4 unchanged
            '5 8.3 a00000 named 1 where 0 V 0-256 kind 0 kept 1',    # only the V barrier is named: C0 reads V
            'contract'])

    def test_exact_batch_deps(self):
        lines = self.run_c(EXACT, WINSYS)
        a, b = lines[-1].split()[1:]
        self.assertNotEqual(a, b)
        self.assertEqual(lines[:-1], [
            'first bloom 0 exact 1 0 0 0 0 0 0 0 0 0 16',
            'collision bloom 1 exact 2 0 0 0 0 0 0 0 0 0 0',   # the Bloom bit, no exact conflict
            'raw bloom 0 exact 1 0 0 0 0 0 0 0 0 0 0',
            'read bloom 1 exact 2 1 40 0 0 0 2 0 0 0 0',
            'war bloom 1 exact 1 1 40 0 0 0 1 0 0 0 0',
            'waw bloom 1 exact 2 1 40 0 0 0 3 0 0 0 0',
            'overflow bloom 0 exact 1 0 0 0 0 0 0 0 0 0 3',
            'after bloom 0 exact 2 0 0 0 0 0 0 0 0 0 12'])

    def test_wiring(self):
        ddi = DDI.read_text()
        ranges = ddi[ddi.index('static void JobDrawRanges('):ddi.index('static __declspec(noinline) HRESULT ReplayDraw(')]
        self.assertIn('const unsigned cross = AgxD3d12OwnerJobTablePlacement() ? 1u : 0u;', ranges)
        self.assertIn('AgxD3d12JobVbSlots(cross, d.pso ? 1u : 0u, elements, kPipelineElements, slot, reg);', ranges)
        self.assertIn('if (vb_slots & (1u << v)) JobHeapRange(sink, d.vb[v].heap, d.vb[v].offset, d.vb[v].size, '
                      'vertex | AGX_D3D12_JOB_SRC_VB);', ranges)
        self.assertIn('    if (cross)\n        AgxD3d12OwnerJobVbDraw(', ranges)
        self.assertEqual(ranges.count('JobHeapRange(sink, d.vb[v]'), 1)
        self.assertIn('AgxD3d12OwnerJobHeapResource(JobHeapResource); /* N4c-v4 receipt */', ddi)
        owner = OWNER.read_text()
        receipt = owner[owner.index('static void owner_ov2_receipt_locked(void) {'):]
        receipt = receipt[:receipt.index('\n}\n')]
        self.assertIn('AdmissionUmdDiagnostic("measure-d3d12-ov2-vb-skip", S_OK, owner_cross_vb_values, 6u);', receipt)
        self.assertIn('if (owner_cross_mask_values[0] & AGX_D3D12_JOB_MASK_NAMED)\n'
                      '        AdmissionUmdDiagnostic("measure-d3d12-ov2-mask-barrier", S_OK, owner_cross_barrier_values, 9u);',
                      receipt)
        summary = owner[owner.index('static void owner_cross_mask_summary_locked(void) {'):]
        summary = summary[:summary.index('\n}\n')]
        self.assertIn('values, 2u + AGX_D3D12_JOB_MASK_BITS);', summary)
        heap_bo = owner[owner.index('static void owner_heap_bo_locked(const void *heap) {'):]
        heap_bo = heap_bo[:heap_bo.index('\n}\n')]
        self.assertIn('if (!heap || !owner_job_heap_resource || !AgxWin32Ov2CrossFeature) return;', heap_bo)
        self.assertIn('AdmissionUmdDiagnostic("measure-d3d12-ov2-heap-bo", S_OK, values, 3u);', heap_bo)
        self.assertEqual(owner.count('owner_heap_bo_locked(heap); /* N4c-v4 receipt */'), 4)
        hook = owner[owner.index('static unsigned owner_batch_kind(unsigned kind) {'):]
        hook = hook[:hook.index('static void owner_batch_deps(')]
        cross = hook[hook.index('      if (AgxWin32Ov2CrossFeature) {'):]
        self.assertIn('owner_cross_exact_next = 1u;', cross[:cross.index('\n      }\n')])
        self.assertIn('AgxWin32BatchDepsExactHook = owner_batch_deps_exact; /* N4c-v4 */', owner)
        batch = BATCH.read_text()
        self.assertIn('const int exact=AgxWin32BatchDepsExactHook && AgxWin32Ov2CrossFeature;', batch)
        self.assertIn('AgxWin32BatchDepsHook(values,4u);', batch)   # measure-batch-deps keeps 4 values


if __name__ == '__main__':
    unittest.main()
