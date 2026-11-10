"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
N4c-v3 (EXP2323 x0): the cross-execution shadow verdict refused every
execution with 6/2 because one ResolveQueryData, whose own writer flush
submitted R5, counted as other owner work for the kept render R7. Asahi puts
that resolve's copy into the compute batch (or writes on the CPU), never into
a render batch, so it is placed (AgxD3d12JobCrossPlace) instead. Descriptor
-table accesses (C47 heap tables) were in no range set and refused 11/2 and
11/4; they are now placed in cross-only sets (TableDrawSet, TableDispatchSet,
Cross.Compute). A wrong placement lets the next execution's first compute run
beside a render it depends on: a placed command after the kept render's
submission, an unplaced command before it, a resolve destination or a table
access overlapping the compute must all still refuse; table ranges must reach
the kept render even when the draw's own work submits a render, and must never
enter the same-execution sets (their verdicts would change). The refusal mask
(AgxD3d12JobCrossMask) is receipt only: with the kept verdict 0 it is 0
exactly when the verdict is 0, every refusal shows in it and it never changes
a verdict.
"""
import unittest
from pathlib import Path

from test_ov2_no_hazard_lead import build_run

ROOT = Path(__file__).resolve().parents[1]
D3D12 = ROOT / 'drivers/apple-agx/d3d12'
OWNER = ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp'
DDI = D3D12 / 'agx_d3d12_ddi.cpp'

COMMON = r"""
#include <stdio.h>
#include <string.h>
#include "agx_d3d12_job_barriers.h"
enum { DRAW = 5, DISPATCH = 2, COPY = 1, CLEAR = 3, QUERY = 9, TRANSITION = 0, UAV = 2 };
#define VIEW AGX_D3D12_JOB_SRC_VIEW
#define TARGET AGX_D3D12_JOB_SRC_TARGET
#define VERTEX AGX_D3D12_JOB_VERTEX
static AGX_D3D12_JOB_BARRIERS j;
static unsigned values[AGX_D3D12_JOB_BARRIER_VALUES];
#define SHARED __attribute__((unused))
static SHARED int A, B, Q, RT, T, U, V;
static unsigned broken_invariant;
static void invariant(void) {
  if (j.Cross.LastPlaced > j.Cross.LastForeign || j.Cross.Placed > j.Cross.Foreign) broken_invariant = 1u;
}
static void batch(unsigned kind, unsigned clean) {
  AgxD3d12JobCrossBatch(&j, kind, clean);
  AgxD3d12JobBarrierBatch(&j, kind, values);
  invariant();
}
static void begin(unsigned kind) { AgxD3d12JobBarrierBegin(&j, kind); invariant(); }
static void end(void) { AgxD3d12JobBarrierEnd(&j); invariant(); }
static SHARED void barrier(unsigned type, unsigned id, const void *heap, unsigned long long lo, unsigned long long hi) {
  AgxD3d12JobBarrierAdd(&j, type, 0, 0, 0, id);
  AgxD3d12JobBarrierRange(&j, type, id, heap, lo, hi);
}
/* the execution-first compute of the next execution: C0 writes A, reads B
 * (plus extra), submitted lazily by the next render's first draw */
static unsigned shadow_detail;
static SHARED unsigned next_c0(const void *extra, unsigned long long lo, unsigned long long hi, unsigned flags) {
  unsigned shadow;
  AgxD3d12JobBarrierExecution(&j, 1);
  AgxD3d12JobCrossBegin(&j, 0);
  begin(DISPATCH);
  AgxD3d12JobBarrierDispatchRange(&j, &A, 0, 64, 1u | VIEW);
  AgxD3d12JobBarrierDispatchRange(&j, &B, 0, 64, VIEW);
  if (extra) AgxD3d12JobBarrierDispatchRange(&j, extra, lo, hi, flags);
  end();
  begin(DRAW);
  if (!AgxD3d12JobCrossFirst(&j)) printf("not first\n");
  shadow = AgxD3d12JobCrossRefusal(&j, &shadow_detail);
  batch(2u, 0);
  end();
  return shadow;
}
"""

GM_SHAPE = COMMON + r"""
/* GravityMark (EXP2323 x0, measure-batch-deps C0 R1 C2 R3 C4 R5 C6 R7): R5
 * open, the third ResolveQueryData flushes its writer R5, then R7's draws,
 * a dispatch (C6), and the end-of-execution flush submits C6, then R7.
 * mode 0: the resolve placed; 1: not placed (as today); 2: a clear after
 * R5's submission (unplaced foreign before the kept render); 3: R7 is
 * submitted by a second placed resolve (a command after the kept render);
 * 4: placed, and C0 reads the resolve destination; 5: placed, C0 reads a
 * neighbouring range */
static void run(int mode) {
  unsigned shadow;
  AgxD3d12JobBarriersInit(&j);
  AgxD3d12JobBarrierExecution(&j, 1);
  AgxD3d12JobCrossBegin(&j, 0);
  begin(DRAW); /* R5 */
  AgxD3d12JobBarrierDrawRange(&j, &RT, 0, 4096, TARGET);
  end();
  begin(QUERY);
  if (mode != 1) AgxD3d12JobCrossPlace(&j, &Q, 0, 88);
  batch(1u, 1); /* its writer flush submits R5 */
  end();
  if (mode == 2) { begin(CLEAR); end(); }
  begin(DRAW); /* R7 */
  AgxD3d12JobBarrierDrawRange(&j, &T, 0, 4096, TARGET);
  AgxD3d12JobBarrierDrawRange(&j, &V, 0, 256, VIEW | VERTEX);
  end();
  begin(DISPATCH); /* C6 */
  AgxD3d12JobBarrierDispatchRange(&j, &B, 0, 64, 1u | VIEW);
  end();
  if (mode == 3) {
    begin(QUERY);
    AgxD3d12JobCrossPlace(&j, &Q, 88, 176);
    batch(2u, 0);
    batch(1u, 1);
    end();
  } else {
    batch(2u, 0); /* the end-of-execution flush: C6, then R7 */
    batch(1u, 1);
  }
  AgxD3d12JobCrossEnd(&j, 1);
  printf("%d kept %u.%u last %u/%u ", mode, j.Cross.Kept, j.Cross.KeptDetail, j.Cross.KeptLastForeign,
         j.Cross.KeptLastPlaced);
  shadow = next_c0(mode == 4 ? &Q : mode == 5 ? &Q : 0, mode == 4 ? 80 : 88, mode == 4 ? 96 : 96, VIEW);
  printf("shadow %u.%u\n", shadow, shadow_detail);
}
int main(void) {
  for (int m = 0; m <= 5; ++m) run(m);
  if (broken_invariant) printf("invariant broken\n");
  return 0;
}
"""

LOCAL = COMMON + r"""
int main(void) {
  AgxD3d12JobBarriersInit(&j);
  AgxD3d12JobBarrierExecution(&j, 1);
  AgxD3d12JobCrossBegin(&j, 0);
  begin(QUERY);
  AgxD3d12JobCrossPlace(&j, &Q, 0, 88);
  AgxD3d12JobCrossPlace(&j, &Q, 0, 88); /* once per command */
  printf("placed %u/%u active %u dst %u\n", j.Cross.Placed, j.Cross.Foreign, j.Cross.PlacedActive,
         j.Cross.PlacedDst.Count);
  AgxD3d12JobBarrierLocal(&j); /* OV3: deferred after all */
  printf("local %u/%u active %u dst %u\n", j.Cross.Placed, j.Cross.Foreign, j.Cross.PlacedActive,
         j.Cross.PlacedDst.Count);
  AgxD3d12JobBarrierLocal(&j);
  AgxD3d12JobCrossPlace(&j, &Q, 0, 88); /* a local command is never placed */
  end();
  printf("again %u/%u render %u\n", j.Cross.Placed, j.Cross.Foreign, j.Cross.Render.Count);
  begin(DRAW); /* neither a draw nor no command at all */
  AgxD3d12JobCrossPlace(&j, &Q, 0, 88);
  end();
  AgxD3d12JobCrossPlace(&j, &Q, 0, 88);
  printf("draw %u/%u render %u\n", j.Cross.Placed, j.Cross.Foreign, j.Cross.Render.Count);
  if (broken_invariant) printf("invariant broken\n");
  return 0;
}
"""

TABLES = COMMON + r"""
/* execution N: draw 1 (Ra: target RT), draw 2 reads T and writes U through
 * a descriptor table; its own work submits Ra after the heap walk; the
 * final flush submits Rb (kept). mode 0..3: C0 also writes T (8/2), reads U
 * (8/1), reads a disjoint range (0) / nothing; mode 4: C0 reads RT through a
 * dispatch table (kept: Rb has no RT -> 0); mode 5: C0 writes T through a
 * dispatch table (8/2); mode 6: a table set overflow (11/1) */
static void run(int mode) {
  unsigned shadow;
  AgxD3d12JobBarriersInit(&j);
  AgxD3d12JobBarrierExecution(&j, 1);
  AgxD3d12JobCrossBegin(&j, 0);
  begin(DRAW);
  AgxD3d12JobBarrierDrawRange(&j, &RT, 0, 4096, TARGET);
  end();
  begin(DRAW);
  AgxD3d12JobBarrierDrawRange(&j, &RT, 4096, 8192, TARGET);
  AgxD3d12JobCrossTableRange(&j, 0, &T, 0, 256, VIEW);
  AgxD3d12JobCrossTableRange(&j, 0, &U, 0, 64, 1u | VIEW);
  if (mode == 6)
    for (int i = 0; i < 130; ++i) AgxD3d12JobCrossTableRange(&j, 0, &T, 1024u + 64u * i, 1056u + 64u * i, VIEW);
  batch(1u, 1); /* Ra, submitted by this draw's own work */
  end();
  batch(1u, 1); /* Rb */
  AgxD3d12JobCrossEnd(&j, 1);
  printf("%d kept %u.%u render %u ", mode, j.Cross.Kept, j.Cross.KeptDetail, j.Cross.KeptRender.Count);
  AgxD3d12JobBarrierExecution(&j, 1);
  AgxD3d12JobCrossBegin(&j, 0);
  begin(DISPATCH);
  AgxD3d12JobBarrierDispatchRange(&j, &A, 0, 64, 1u | VIEW);
  if (mode == 0) AgxD3d12JobBarrierDispatchRange(&j, &T, 128, 192, 1u | VIEW);
  if (mode == 1) AgxD3d12JobBarrierDispatchRange(&j, &U, 0, 16, VIEW);
  if (mode == 2) AgxD3d12JobBarrierDispatchRange(&j, &U, 64, 128, 1u | VIEW);
  if (mode == 4) AgxD3d12JobCrossTableRange(&j, 1, &RT, 0, 64, VIEW);
  if (mode == 5) AgxD3d12JobCrossTableRange(&j, 1, &T, 0, 16, 1u | VIEW);
  end();
  begin(DRAW);
  shadow = AgxD3d12JobCrossRefusal(&j, &shadow_detail);
  printf("compute %u range %u shadow %u.%u\n", j.Cross.Compute.Count, j.RangeCompute.Count, shadow, shadow_detail);
  batch(2u, 0);
  end();
  if (j.Cross.Compute.Count) printf("compute set kept past its batch\n");
}
int main(void) {
  for (int m = 0; m <= 6; ++m) run(m);
  return 0;
}
"""

SAME_EXECUTION = COMMON + r"""
/* the same replay with and without table placement: every same-execution
 * set and verdict must be identical at every step */
static unsigned long long hash;
static void mix(unsigned long long v) { hash = (hash ^ v) * 1099511628211ull; }
static void set(const AGX_D3D12_JOB_RANGE_SET *s) {
  mix(s->Count); mix(s->Overflow); mix(s->Uav);
  for (unsigned i = 0; i < s->Count; ++i) {
    mix((unsigned long long)(size_t)s->R[i].Heap); mix(s->R[i].Lo); mix(s->R[i].Hi); mix(s->R[i].Uav);
  }
}
static void step(void) {
  set(&j.RangeDraw); set(&j.RangeRender); set(&j.RangeLast); set(&j.RangeDispatch); set(&j.RangeCompute);
  set(&j.RangeLastCompute); set(&j.Post); set(&j.Resolved);
  mix(AgxD3d12JobBarrierRefusal(&j)); mix(AgxD3d12JobBarrierRefusalRanges(&j));
  mix(AgxD3d12JobRenderTaRefusal(&j, 0)); mix(AgxD3d12JobOv8Range(&j)); mix(AgxD3d12JobOv8Barrier(&j));
}
static unsigned long long run(int tables) {
  hash = 14695981039346656037ull;
  AgxD3d12JobBarriersInit(&j);
  AgxD3d12JobBarrierExecution(&j, 1);
  AgxD3d12JobCrossBegin(&j, 0);
  for (int f = 0; f < 3; ++f) {
    begin(DISPATCH);
    AgxD3d12JobBarrierDispatchRange(&j, &B, 0, 64, 1u | VIEW);
    if (tables) AgxD3d12JobCrossTableRange(&j, 1, &T, 0, 256, 1u | VIEW);
    step();
    end(); step();
    begin(DRAW);
    batch(2u, 0); step();
    AgxD3d12JobBarrierDrawRange(&j, &RT, 0, 4096, TARGET);
    if (tables) {
      AgxD3d12JobCrossTableRange(&j, 0, &T, 0, 256, VIEW);
      AgxD3d12JobCrossTableRange(&j, 0, &U, 0, 64, 1u | VIEW);
    }
    step();
    end(); step();
    barrier(TRANSITION, 3, &RT, 0, 4096); step();
    begin(DISPATCH);
    batch(1u, 1); step();
    AgxD3d12JobBarrierDispatchRange(&j, &RT, 0, 64, VIEW);
    if (tables) AgxD3d12JobCrossTableRange(&j, 1, &U, 0, 64, VIEW);
    end(); step();
  }
  batch(2u, 0); step();
  return hash;
}
int main(void) {
  const unsigned long long plain = run(0), placed = run(1);
  printf("%s\n", plain == placed ? "same" : "different");
  return 0;
}
"""

MASK = COMMON + r"""
/* execution N: C (writes B), R (target RT, reads V), final flush; N+1: C0
 * (writes A, reads B), submitted by the next render's first draw. Each mode
 * sets one condition. */
static AGX_D3D12_JOB_CROSS_MASK m;
static char many[140 * 64];
static unsigned contract_broken, verdict_changed, refusal_hidden;
static void run(int mode) {
  unsigned detail = 0, detail2 = 0, shadow, shadow2, mask, k;
  AgxD3d12JobBarriersInit(&j);
  AgxD3d12JobBarrierExecution(&j, mode != 1);
  AgxD3d12JobCrossBegin(&j, 0);
  begin(DISPATCH);
  AgxD3d12JobBarrierDispatchRange(&j, &B, 0, 64, 1u | VIEW);
  end();
  begin(DRAW);
  batch(2u, 0);
  AgxD3d12JobBarrierDrawRange(&j, &RT, 0, 4096, TARGET);
  AgxD3d12JobBarrierDrawRange(&j, &V, 0, 256, VIEW | VERTEX);
  if (mode == 6) for (k = 0; k < 130; ++k) AgxD3d12JobBarrierDrawRange(&j, &many[k * 64], 0, 32, VIEW);
  if (mode == 7) AgxD3d12JobBarrierDrawRange(&j, 0, 0, 0, VIEW);
  if (mode == 8) AgxD3d12JobCrossUnplaced(&j, 0);
  end();
  if (mode == 3) { begin(CLEAR); end(); }       /* before the kept render's submission */
  if (mode == 14) barrier(TRANSITION, 9, 0, 0, 0);
  if (mode == 21) barrier(TRANSITION, 9, &A, 0, 64);
  if (mode == 121) barrier(TRANSITION, 9, &V, 0, 64);
  if (mode == 22) barrier(UAV, 0, 0, 0, 0);
  if (mode == 4) { begin(CLEAR); batch(1u, 1); end(); } /* the clear submits the render */
  else if (mode == 2) { batch(1u, 1); }
  else if (mode != 102) batch(1u, 1);
  if (mode == 5) AgxD3d12JobBarrierForeign(&j);
  if (mode == 102) { batch(1u, 1); batch(2u, 0); } /* ends with a compute */
  AgxD3d12JobCrossEnd(&j, mode != 2);
  AgxD3d12JobBarrierExecution(&j, mode != 100);
  AgxD3d12JobCrossBegin(&j, 0);
  if (mode == 9) AgxD3d12JobCrossBreak(&j, 9);
  if (mode == 10) { begin(CLEAR); end(); }
  if (mode == 11) AgxD3d12JobBarrierForeign(&j);
  if (mode == 16 || mode == 19 || mode == 20) {
    begin(COPY);
    if (mode == 16) AgxD3d12JobCrossCopy(&j, 0, 0, 64, &B, 0, 64);
    if (mode == 19) AgxD3d12JobCrossCopy(&j, &RT, 4000, 4064, &B, 0, 64);
    if (mode == 20) AgxD3d12JobCrossCopy(&j, &V, 128, 192, &B, 0, 64);
    end();
  }
  if (mode != 15) {
    begin(DISPATCH);
    AgxD3d12JobBarrierDispatchRange(&j, &A, 0, 64, 1u | VIEW);
    AgxD3d12JobBarrierDispatchRange(&j, &B, 0, 64, VIEW);
    if (mode == 12) AgxD3d12JobBarrierDispatchRange(&j, 0, 0, 0, VIEW);
    if (mode == 112) AgxD3d12JobCrossTableRange(&j, 1, 0, 0, 0, VIEW);
    if (mode == 13) AgxD3d12JobCrossUnplaced(&j, 1);
    if (mode == 17) AgxD3d12JobBarrierDispatchRange(&j, &RT, 100, 200, VIEW);
    if (mode == 117) AgxD3d12JobCrossTableRange(&j, 1, &RT, 100, 200, VIEW);
    if (mode == 18) AgxD3d12JobBarrierDispatchRange(&j, &V, 0, 16, 1u | VIEW);
    if (mode == 118) AgxD3d12JobCrossTableRange(&j, 1, &V, 0, 16, 1u | VIEW);
    if (mode == 121) AgxD3d12JobBarrierDispatchRange(&j, &V, 0, 16, VIEW);
    end();
  }
  begin(DRAW);
  shadow = AgxD3d12JobCrossRefusal(&j, &detail);
  mask = AgxD3d12JobCrossMask(&j, &m);
  shadow2 = AgxD3d12JobCrossRefusal(&j, &detail2);
  if (shadow != shadow2 || detail != detail2) verdict_changed = 1u;
  /* N4c-v4: bit 23 (the N4c-v3 8/3 rule) is receipt only */
  if (!j.Cross.Kept && ((mask & ~AGX_D3D12_JOB_MASK_NAMED) == 0u) != (shadow == 0u)) contract_broken = 1u;
  if (shadow && !mask) refusal_hidden = 1u;
  printf("%d %u.%u %x", mode, shadow, detail, mask);
  for (k = 0; k < AGX_D3D12_JOB_MASK_CLASSES; ++k)
    if (m.Pairs[k])
      printf(" p%u=%u %s%llu-%llu/%s%llu-%llu", k, m.Pairs[k], m.First[k][0].Current.Heap == &RT ? "RT" :
             m.First[k][0].Current.Heap == &V ? "V" : "?", m.First[k][0].Current.Lo, m.First[k][0].Current.Hi,
             m.First[k][0].Kept.Heap == &RT ? "RT" : m.First[k][0].Kept.Heap == &V ? "V" : "?",
             m.First[k][0].Kept.Lo, m.First[k][0].Kept.Hi);
  printf("\n");
  batch(2u, 0);
  end();
}
int main(void) {
  static const int modes[] = {0, 1, 100, 102, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 112, 13, 14, 15, 16,
                              17, 117, 18, 118, 19, 20, 21, 121, 22};
  for (unsigned i = 0; i < sizeof(modes) / sizeof(modes[0]); ++i) run(modes[i]);
  /* the counts and sets of the receipt */
  run(0);
  printf("counts %u %u %u %u %u %u %u %u %u %u\n", m.KeptLastForeign, m.KeptLastPlaced, m.KeptLastForeignKinds,
         m.Foreign, m.Copies, m.ForeignKinds, m.ShadowRender, m.Compute, m.RangeCompute, m.Copy);
  printf("%s %s %s\n", contract_broken ? "contract broken" : "contract",
         verdict_changed ? "verdict changed" : "verdict kept", refusal_hidden ? "refusal hidden" : "refusals shown");
  return 0;
}
"""


class N4cV3Placement(unittest.TestCase):
    def run_c(self, code):
        ran = build_run(code, [], [D3D12])
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        return ran.stdout.split('\n')[:-1]

    def test_gm_shape_placed_resolve(self):
        self.assertEqual(self.run_c(GM_SHAPE), [
            '0 kept 0.0 last 1/1 shadow 0.0',    # the placed resolve no longer refuses R7
            '1 kept 6.2 last 1/0 shadow 6.2',    # not placed: as today
            '2 kept 6.2 last 2/1 shadow 6.2',    # an unplaced clear before R7 still refuses
            '3 kept 6.2 last 1/1 shadow 6.2',    # a placed resolve after R7's submission still refuses
            '4 kept 0.0 last 1/1 shadow 8.1',    # C0 reads the resolve destination (a kept write)
            '5 kept 0.0 last 1/1 shadow 0.0'])   # C0 reads past it

    def test_local_after_place(self):
        self.assertEqual(self.run_c(LOCAL), [
            'placed 1/1 active 1 dst 1',
            'local 0/0 active 0 dst 0',
            'again 0/0 render 0',
            'draw 0/0 render 0'])

    def test_table_ranges_reach_the_kept_render_at_the_end(self):
        self.assertEqual(self.run_c(TABLES), [
            '0 kept 0.0 render 3 compute 0 range 2 shadow 8.2',   # C0 writes a table-read texture
            '1 kept 0.0 render 3 compute 0 range 2 shadow 8.1',   # C0 reads a table UAV write
            '2 kept 0.0 render 3 compute 0 range 2 shadow 0.0',   # disjoint
            '3 kept 0.0 render 3 compute 0 range 1 shadow 0.0',
            '4 kept 0.0 render 3 compute 1 range 1 shadow 0.0',   # a dispatch-table read of what only Ra wrote
            '5 kept 0.0 render 3 compute 1 range 1 shadow 8.2',   # a dispatch-table write of what Rb reads
            '6 kept 11.1 render 0 compute 0 range 1 shadow 11.1'])  # the kept set overflows

    def test_table_ranges_never_reach_same_execution_sets(self):
        self.assertEqual(self.run_c(SAME_EXECUTION), ['same'])

    def test_refusal_mask(self):
        lines = self.run_c(MASK)
        bits = {line.split()[0]: int(line.split()[2], 16) for line in lines if line[0].isdigit()}
        verdicts = {line.split()[0]: line.split()[1] for line in lines if line[0].isdigit()}
        self.assertEqual(bits['0'], 0)
        self.assertEqual(verdicts['0'], '0.0')
        expected = {'1': 0, '100': 0, '102': 1, '2': 2, '3': 3, '4': 4, '5': 5, '6': 6, '7': 7, '8': 8,
                    '9': 9, '10': 10, '11': 11, '12': 12, '112': 12, '13': 13, '14': 14, '15': 15, '16': 16,
                    '17': 17, '117': 17, '18': 18, '118': 18, '19': 19, '20': 20, '121': 21, '22': 22}
        for mode, bit in expected.items():
            self.assertTrue(bits[mode] & (1 << bit), (mode, bit, lines))
            self.assertNotEqual(verdicts[mode], '0.0', mode)
        # one cause, one bit (driver work in a compute batch also stays ComputeForeign across the
        # boundary (5) and leaves its writes unplaced (11))
        self.assertEqual(bits['5'], (1 << 5) | (1 << 11))
        self.assertEqual(bits['11'], (1 << 11) | (1 << 12))
        for mode in ('3', '4', '9', '10', '13', '15', '16', '17', '18', '19', '20', '22'):
            self.assertEqual(bits[mode], 1 << expected[mode], (mode, lines))
        # N4c-v4: a barrier on a range only the compute uses orders nothing (receipt bit 23 only);
        # one on a range the kept render accessed too is 8/3 (bit 21, and 23)
        self.assertEqual(bits['21'], 1 << 23, lines)
        self.assertEqual(verdicts['21'], '0.0')
        self.assertEqual(bits['121'], (1 << 21) | (1 << 23), lines)
        self.assertIn('p0=1 RT100-200/RT0-4096', [line for line in lines if line.startswith('17 ')][0])
        self.assertIn('p1=1 V0-16/V0-256', [line for line in lines if line.startswith('18 ')][0])
        self.assertIn('p2=1 RT4000-4064/RT0-4096', [line for line in lines if line.startswith('19 ')][0])
        self.assertIn('p3=1 V128-192/V0-256', [line for line in lines if line.startswith('20 ')][0])
        self.assertIn('counts 0 0 0 0 0 0 2 0 2 0', lines)
        self.assertEqual(lines[-1], 'contract verdict kept refusals shown')

    def test_owner_and_replay_wiring(self):
        owner = OWNER.read_text()
        ddi = DDI.read_text()
        # A: staged by the replay, consumed by the native non-deferred resolve
        resolve = owner[owner.index('extern "C" HRESULT AgxD3d12OwnerResolveQueries('):]
        resolve = resolve[:resolve.index('\n}\n')]
        self.assertLess(resolve.index('const void *job_heap = owner_job_resolve_take();'),
                        resolve.index('if (defer) {'))
        native = resolve[resolve.index('owner_resolve_drain(owner, kResolveDrainNative);') - 80:]
        self.assertIn('const UINT drained = owner->ResolveDrained;\n  owner_resolve_drain(owner, kResolveDrainNative);',
                      native)
        self.assertLess(native.index('if (job_heap && owner->ResolveDrained == drained) '
                                     'owner_job_place(job_heap, offset, offset + bytes);'),
                        native.index('pipe->get_query_result_resource('))
        stage = owner[owner.index('extern "C" void AgxD3d12OwnerJobResolveHeap('):]
        self.assertLess(stage.index('if (!AgxWin32Ov2CrossFeature) return;'), stage.index('owner_job_resolve_heap = '))
        replay = ddi[ddi.index('if (q.op == kQueryResolve) {'):]
        self.assertLess(replay.index('if (job_command) AgxD3d12OwnerJobResolveHeap(q.dst);'),
                        replay.index('hr = AgxD3d12OwnerResolveQueries('))
        # B: the walk feeds the sink where it appends reads and writes, gated like A
        place = owner[owner.index('extern "C" BOOL AgxD3d12OwnerJobTablePlacement('):]
        self.assertIn('AgxWin32Ov2CrossFeature && AgxD3d12OwnerJobBarriersEnabled()', place[:place.index('\n}\n')])
        walk = ddi[ddi.index('static HRESULT SetStageHeapWalk(AGX_D3D12_OWNER *owner, unsigned stage, '
                             'const AgxD3d12Dispatch *b, AgxD3d12HeapScratch **scratch,\n'
                             '                                bool *minmax, JobRangeSink table) {'):]
        walk = walk[:walk.index('\n}\n')]
        self.assertLess(walk.index('if (table) JobDescriptorRange(table, view, 1u | job_stage | AGX_D3D12_JOB_SRC_VIEW);'),
                        walk.index('h.writes[h.binding.WriteCount++] = texture;'))
        self.assertIn('if (table) JobHeapRange(table, counter, counter_offset, 4u, 1u | job_stage | AGX_D3D12_JOB_SRC_VIEW);',
                      walk)
        self.assertLess(walk.index('if (table) JobDescriptorRange(table, view, job_stage | AGX_D3D12_JOB_SRC_VIEW);'),
                        walk.index('h.textures[h.binding.TextureCount++] = texture;'))
        self.assertEqual(walk.count('if (table)'), 3)
        dispatch = ddi[ddi.index('static __declspec(noinline) HRESULT ReplayDispatch('):]
        dispatch = dispatch[:dispatch.index('\n}\n')]
        self.assertIn('AgxD3d12OwnerJobTablePlacement() ? AgxD3d12OwnerJobDispatchTableRange : nullptr;', dispatch)
        self.assertIn('if (job && JobStageHeapTable(dispatch) && (!table || FAILED(heap_hr))) '
                      'AgxD3d12OwnerJobUnplaced(1u);', dispatch)
        draw = ddi[ddi.index('static __declspec(noinline) HRESULT ReplayDraw('):]
        draw = draw[:draw.index('\n}\n')]
        self.assertIn('AgxD3d12OwnerJobTablePlacement() ? AgxD3d12OwnerJobDrawTableRange : nullptr;', draw)
        self.assertEqual(draw.count('AgxD3d12OwnerJobUnplaced(0u);'), 2)
        self.assertNotIn('AgxD3d12OwnerJobUnplaced', ddi[ddi.index('static void JobDrawRanges('):
                                                         ddi.index('static __declspec(noinline) HRESULT ReplayDraw(')])
        # C: the mask receipt next to the copy receipt, summary in the same writer
        receipt = owner[owner.index('static void owner_ov2_receipt_locked(void) {'):]
        receipt = receipt[:receipt.index('\n}\n')]
        self.assertIn('AdmissionUmdDiagnostic("measure-d3d12-ov2-mask", S_OK, owner_cross_mask_values, 17u);', receipt)
        self.assertIn('"measure-d3d12-ov2-mask-pair"', receipt)
        self.assertIn('"measure-d3d12-ov2-mask-summary"', owner)
        hook = owner[owner.index('static unsigned owner_batch_kind(unsigned kind) {'):]
        hook = hook[:hook.index('static void owner_batch_deps(')]
        self.assertLess(hook.index('const unsigned shadow = AgxD3d12JobCrossRefusal(&owner_job_barriers, &detail);'),
                        hook.index('owner_cross_mask_take(shadow, detail);'))
        self.assertLess(hook.index('owner_cross_mask_take(shadow, detail);'),
                        hook.index('AgxD3d12JobCrossBatch(&owner_job_barriers, kind, cross_clean);'))
        take = owner[owner.index('static void owner_cross_mask_take(unsigned shadow, unsigned detail) {'):]
        self.assertIn('AgxD3d12JobCrossMask(&owner_job_barriers, &owner_cross_mask);', take[:take.index('\n}\n')])
        destroy = owner[owner.index('extern "C" HRESULT AgxD3d12OwnerDestroy('):]
        self.assertIn('owner_cross_mask_summary_locked(); /* N4c-v3: the end of the run */',
                      destroy[:destroy.index('ReleaseSRWLockExclusive(&owner_job_barriers_lock);')])


if __name__ == '__main__':
    unittest.main()
