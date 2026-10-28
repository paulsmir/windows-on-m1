"""rc105 receipt-only: what D3D12 ResourceBarrier asks for
(drivers/apple-agx/d3d12/agx_d3d12_barrier_stats.h; receipt
measure-d3d12-barriers per Execute). Every barrier stays a no-op.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
- The counters must classify render-target, depth-write and UAV hand-offs to
  read states (the transitions GravityMark's shadow map, G-buffer and TAA
  rely on), aliasing (type 1 and 0022 ranged with the ALIASING flag), UAV,
  ranged and split-begin barriers, with the d3d12umddi.h values; a
  BEGIN_ONLY half is not a hand-off.
- Both ResourceBarrier entries (0003 and 0022) must count, Reset must clear
  the counts, and Execute must report them.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
BARRIERS = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_barrier_stats.h'
DDI = ROOT / 'drivers/apple-agx/d3d12/agx_d3d12_ddi.cpp'

BARRIER_HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include "agx_d3d12_barrier_stats.h"
int main(void) {
  unsigned c[AGX_D3D12_BARRIER_COUNTERS] = {0};
  AgxD3d12BarrierCount(c, 0, 0, 0x10, 0x80);       /* shadow map: DEPTH_WRITE -> PIXEL_SHADER_RESOURCE */
  AgxD3d12BarrierCount(c, 0, 0, 0x10, 0x40 | 0x80); /* DEPTH_WRITE -> both shader resource states */
  AgxD3d12BarrierCount(c, 0, 0, 0x4, 0x40);        /* G-buffer: RENDER_TARGET -> NON_PIXEL_SHADER_RESOURCE */
  AgxD3d12BarrierCount(c, 0, 0, 0x8, 0x200);       /* UAV -> INDIRECT_ARGUMENT */
  AgxD3d12BarrierCount(c, 0, 0, 0x8, 0x800);       /* UAV -> COPY_SOURCE */
  AgxD3d12BarrierCount(c, 0, 0, 0x80, 0x4);        /* SRV -> RENDER_TARGET: no hand-off */
  AgxD3d12BarrierCount(c, 0, 1, 0x4, 0x80);        /* split begin: counted, not a hand-off */
  AgxD3d12BarrierCount(c, 0, 2, 0x4, 0x80);        /* split end: the hand-off */
  AgxD3d12BarrierCount(c, 1, 0, 0, 0);             /* aliasing */
  AgxD3d12BarrierCount(c, 3, 8, 0, 0);             /* ranged aliasing (0022) */
  AgxD3d12BarrierCount(c, 3, 0, 0, 0);             /* ranged */
  AgxD3d12BarrierCount(c, 2, 0, 0, 0);             /* UAV */
  AgxD3d12BarrierCount(c, 9, 0, 0x4, 0x80);        /* unknown type: ignored */
  assert(c[AGX_D3D12_BARRIER_TRANSITIONS] == 8);
  assert(c[AGX_D3D12_BARRIER_DEPTH_TO_READ] == 2);
  assert(c[AGX_D3D12_BARRIER_RT_TO_READ] == 2);
  assert(c[AGX_D3D12_BARRIER_UAV_TO_READ] == 2);
  assert(c[AGX_D3D12_BARRIER_SPLIT_BEGIN] == 1);
  assert(c[AGX_D3D12_BARRIER_ALIASING] == 2);
  assert(c[AGX_D3D12_BARRIER_RANGED] == 2);
  assert(c[AGX_D3D12_BARRIER_UAV] == 1);
  puts("PASS");
  return 0;
}
'''



def run_c(source, include, name, cflags=()):
    with tempfile.TemporaryDirectory(prefix='rc105-') as tmp:
        src = Path(tmp) / (name + '.c')
        exe = Path(tmp) / name
        src.write_text(source)
        r = subprocess.run(['clang', '-std=c99', '-Wall', '-Wextra', '-Werror', *cflags, '-I', str(include),
                            str(src), '-o', str(exe)], capture_output=True, text=True)
        if r.returncode:
            return r.returncode, r.stderr
        r = subprocess.run([str(exe)], capture_output=True, text=True)
        return r.returncode, r.stdout + r.stderr


class BarrierStats(unittest.TestCase):
    def test_classification(self):
        code, out = run_c(BARRIER_HARNESS, BARRIERS.parent, 'barriers')
        self.assertEqual(code, 0, out)
        self.assertIn('PASS', out)

    def test_ddi_counts_both_barrier_entries_and_reports_per_execute(self):
        ddi = DDI.read_text()
        self.assertEqual(ddi.count('AgxD3d12BarrierCount(list->barriers'), 2)
        self.assertIn('memset(list->barriers, 0, sizeof(list->barriers));', ddi)
        self.assertIn('AdmissionUmdDiagnostic("measure-d3d12-barriers", S_OK, barriers, AGX_D3D12_BARRIER_COUNTERS);', ddi)



if __name__ == '__main__':
    unittest.main()
