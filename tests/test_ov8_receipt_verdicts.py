"""OV8 receipt verdicts for a render after a compute batch.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
These verdicts decide whether a render may run ahead of the compute before it. A
barrier on a compute write, an overlapping access or a prelude reading compute output
must not read as independent, and disjoint ranges must not read as a conflict.
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
#define A ((const void *)0xA)
static void compute(void) {
  AgxD3d12JobBarriersInit(&j); AgxD3d12JobBarrierExecution(&j, 1u);
  AgxD3d12JobBarrierBegin(&j, 2);
  AgxD3d12JobBarrierDispatchRange(&j, A, 0, 64, 1u | AGX_D3D12_JOB_SRC_ROOT);
  AgxD3d12JobBarrierEnd(&j);
  AgxD3d12JobBarrierBatch(&j, 2u, values);
}
static void draw(unsigned long long lo, unsigned long long hi, unsigned flags) {
  AgxD3d12JobBarrierBegin(&j, 5);
  AgxD3d12JobBarrierDrawRange(&j, A, lo, hi, flags);
  AgxD3d12JobBarrierEnd(&j);
}
int main(void) {
  compute(); draw(64, 128, AGX_D3D12_JOB_VERTEX | AGX_D3D12_JOB_SRC_VB);
  assert(AgxD3d12JobOv8Range(&j) == 0 && AgxD3d12JobOv8Barrier(&j) == 0 && AgxD3d12JobOv8Prelude(&j, 1) == 0);
  compute(); draw(60, 68, AGX_D3D12_JOB_SRC_VIEW);           /* a pixel-stage read of a compute write */
  assert(AgxD3d12JobOv8Range(&j) == 10);
  compute(); AgxD3d12JobBarrierRange(&j, 0u, 7u, A, 0, 64); /* a transition of the compute's write */
  assert(AgxD3d12JobOv8Barrier(&j) == 8);
  compute(); AgxD3d12JobBarrierRange(&j, 2u, 0u, 0, 0, 0);  /* a global UAV barrier */
  assert(AgxD3d12JobOv8Barrier(&j) == 8);
  compute(); draw(100, 128, AGX_D3D12_JOB_VERTEX | AGX_D3D12_JOB_SRC_VB);
  AgxD3d12JobBarrierRange(&j, 0u, 7u, A, 0, 64);           /* after the first draw: not an ordering of the compute */
  assert(AgxD3d12JobOv8Barrier(&j) == 0);
  compute(); draw(0, 32, AGX_D3D12_JOB_VERTEX | AGX_D3D12_JOB_SRC_IND_ARGS);
  assert(AgxD3d12JobOv8Prelude(&j, 1) == 9 && AgxD3d12JobOv8Prelude(&j, 0) == 0);
  return 0;
}
'''


class Ov8Verdicts(unittest.TestCase):
    def test_verdicts(self):
        with tempfile.TemporaryDirectory(prefix='ov8-') as tmp:
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
