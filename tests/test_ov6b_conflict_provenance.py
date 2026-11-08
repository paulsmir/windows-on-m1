"""OV6b: conflict provenance for the OV5 vertex-stage verdict.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Provenance bits share the range flag word with the bits the verdict reads. A bit
leaking into the write or vertex test would change verdicts on every render, and
the reported pair must be the one that produced verdict 10.
"""
import subprocess
import tempfile
import unittest
from pathlib import Path

D3D12 = Path(__file__).resolve().parents[1] / 'drivers/apple-agx/d3d12'
SCENARIO = r'''
#include <assert.h>
#include "agx_d3d12_job_barriers.h"
static AGX_D3D12_JOB_BARRIERS j;
static unsigned values[AGX_D3D12_JOB_BARRIER_VALUES];
static void compute(unsigned flags) {
  AgxD3d12JobBarriersInit(&j); AgxD3d12JobBarrierExecution(&j, 1u);
  AgxD3d12JobBarrierBegin(&j, 2);
  AgxD3d12JobBarrierDispatchRange(&j, (const void *)0xA, 0, 64, flags);
  AgxD3d12JobBarrierEnd(&j);
  AgxD3d12JobBarrierBatch(&j, 2u, values);
}
static void draw(unsigned long long lo, unsigned long long hi, unsigned flags) {
  AgxD3d12JobBarrierBegin(&j, 5);
  AgxD3d12JobBarrierDrawRange(&j, (const void *)0xA, lo, hi, flags);
  AgxD3d12JobBarrierEnd(&j);
}
int main(void) {
  AGX_D3D12_JOB_RANGE x, y;
  compute(1u | AGX_D3D12_JOB_SRC_ROOT);
  draw(60, 68, AGX_D3D12_JOB_VERTEX | AGX_D3D12_JOB_SRC_VB);
  assert(AgxD3d12JobRenderTaRefusal(&j, 0) == 10);
  assert(AgxD3d12JobRenderTaConflict(&j, &x, &y) == 1);
  assert(x.Uav == (AGX_D3D12_JOB_VERTEX | AGX_D3D12_JOB_SRC_VB) && x.Lo == 60 && x.Hi == 68);
  assert(y.Uav == (1u | AGX_D3D12_JOB_SRC_ROOT) && y.Lo == 0 && y.Hi == 64);
  /* a compute read with only source bits set is not a write */
  compute(AGX_D3D12_JOB_SRC_MASK);
  draw(60, 68, AGX_D3D12_JOB_VERTEX | AGX_D3D12_JOB_SRC_VIEW);
  assert(AgxD3d12JobRenderTaRefusal(&j, 0) == 0);
  assert(AgxD3d12JobRenderTaConflict(&j, &x, &y) == 0);
  /* a pixel-stage range with source bits is not a vertex access */
  compute(1u | AGX_D3D12_JOB_SRC_ROOT);
  draw(60, 68, AGX_D3D12_JOB_SRC_MASK & ~AGX_D3D12_JOB_VERTEX);
  assert(AgxD3d12JobRenderTaRefusal(&j, 0) == 0);
  /* a vertex-stage UAV binding against a compute read is reported as a conflict */
  compute(AGX_D3D12_JOB_SRC_ROOT);
  draw(60, 68, 1u | AGX_D3D12_JOB_VERTEX | AGX_D3D12_JOB_SRC_ROOT);
  assert(AgxD3d12JobRenderTaRefusal(&j, 0) == 10);
  assert(AgxD3d12JobRenderTaConflict(&j, &x, &y) == 1 && (x.Uav & 1u) && !(y.Uav & 1u));
  return 0;
}
'''


class ConflictProvenance(unittest.TestCase):
    def test_source_bits_do_not_change_verdicts(self):
        with tempfile.TemporaryDirectory(prefix='ov6b-') as tmp:
            src = Path(tmp) / 't.c'
            src.write_text(SCENARIO)
            exe = Path(tmp) / 't'
            built = subprocess.run(['clang', '-std=c99', '-Wall', '-Wextra', '-Werror',
                                    '-fsanitize=address,undefined', '-I', str(D3D12), str(src), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)


if __name__ == '__main__':
    unittest.main()
