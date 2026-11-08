"""OV6: finite access ranges for the UMD independence verdict
(investigation/analysis/OV6-exact-ranges.md).

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
- An open range [offset, ~0) from a root descriptor or an ExecuteIndirect
  argument makes every later access of the same heap conflict, so the OV5
  verdict refuses independent renders (EXP2274 reason 10 as an upper bound):
  every unsized access must end at its placed resource or at the heap.
- A full 48-entry set refused GravityMark renders as "unplaceable" (11): the
  set holds 128 entries and the receipt tells a full set from a no-heap access.
- The heap span registry must never be freed through an alias heap (the
  placed resource's alias copies the parent's pointer).
"""
import re
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
D3D12 = ROOT / 'drivers/apple-agx/d3d12'
DDI = (D3D12 / 'agx_d3d12_ddi.cpp').read_text()
HEADER = (D3D12 / 'agx_d3d12_job_barriers.h').read_text()
OWNER = (ROOT / 'drivers/apple-agx/mesa/winsys/agx_d3d12_windows.cpp').read_text()


def body(source, name):
    match = re.search(rf"\b{name}\s*\([^;{{]*?\)\s*\{{", source, re.S)
    assert match, name
    depth, index = 1, match.end()
    while depth:
        depth += {"{": 1, "}": -1}.get(source[index], 0)
        index += 1
    return source[match.end():index]


SETS = r'''
#include <assert.h>
#include "agx_d3d12_job_barriers.h"
static AGX_D3D12_JOB_BARRIERS j;
static unsigned values[AGX_D3D12_JOB_BARRIER_VALUES];
int main(void) {
  unsigned i;
  AgxD3d12JobBarriersInit(&j); AgxD3d12JobBarrierExecution(&j, 1u);
  /* a compute batch writing [0, 64) of heap A, then a render with 100 distinct vertex reads of heap B */
  AgxD3d12JobBarrierBegin(&j, 2); AgxD3d12JobBarrierDispatchRange(&j, (const void *)0xA, 0, 64, 1u); AgxD3d12JobBarrierEnd(&j);
  AgxD3d12JobBarrierBatch(&j, 2u, values);
  AgxD3d12JobBarrierBegin(&j, 5);
  for (i = 0; i < 100; ++i) AgxD3d12JobBarrierDrawRange(&j, (const void *)0xB, i * 1024ull, i * 1024ull + 16, AGX_D3D12_JOB_VERTEX);
  AgxD3d12JobBarrierEnd(&j);
  assert(j.RangeRender.Count == 100 && j.RangeRender.Overflow == 0);
  assert(AgxD3d12JobRenderTaRefusal(&j, 0) == 0);
  /* the same heap as the compute write but a finite, disjoint range: still independent */
  AgxD3d12JobBarrierBegin(&j, 5); AgxD3d12JobBarrierDrawRange(&j, (const void *)0xA, 64, 4096, AGX_D3D12_JOB_VERTEX); AgxD3d12JobBarrierEnd(&j);
  assert(AgxD3d12JobRenderTaRefusal(&j, 0) == 0);
  /* an overlapping vertex read of the compute write: a real conflict */
  AgxD3d12JobBarrierBegin(&j, 5); AgxD3d12JobBarrierDrawRange(&j, (const void *)0xA, 60, 68, AGX_D3D12_JOB_VERTEX); AgxD3d12JobBarrierEnd(&j);
  assert(AgxD3d12JobRenderTaRefusal(&j, 0) == 10);
  /* a no-heap access marks bit 2; a full set marks bit 1; both refuse with 11 */
  AgxD3d12JobBarriersInit(&j); AgxD3d12JobBarrierExecution(&j, 1u);
  AgxD3d12JobBarrierBegin(&j, 2); AgxD3d12JobBarrierDispatchRange(&j, (const void *)0xA, 0, 64, 1u); AgxD3d12JobBarrierEnd(&j);
  AgxD3d12JobBarrierBatch(&j, 2u, values);
  AgxD3d12JobBarrierBegin(&j, 5); AgxD3d12JobBarrierDrawRange(&j, 0, 0, 0, 1u | AGX_D3D12_JOB_VERTEX); AgxD3d12JobBarrierEnd(&j);
  assert(j.RangeRender.Overflow == AGX_D3D12_JOB_RANGE_NULL && AgxD3d12JobRenderTaRefusal(&j, 0) == 11);
  AgxD3d12JobBarriersInit(&j); AgxD3d12JobBarrierExecution(&j, 1u);
  AgxD3d12JobBarrierBegin(&j, 2); AgxD3d12JobBarrierDispatchRange(&j, (const void *)0xA, 0, 64, 1u); AgxD3d12JobBarrierEnd(&j);
  AgxD3d12JobBarrierBatch(&j, 2u, values);
  AgxD3d12JobBarrierBegin(&j, 5);
  for (i = 0; i <= AGX_D3D12_JOB_RANGES; ++i) AgxD3d12JobBarrierDrawRange(&j, (const void *)0xB, i * 1024ull, i * 1024ull + 16, 0u);
  AgxD3d12JobBarrierEnd(&j);
  assert(j.RangeRender.Overflow == AGX_D3D12_JOB_RANGE_FULL && AgxD3d12JobRenderTaRefusal(&j, 0) == 11);
  return 0;
}
'''


class RangeSetTests(unittest.TestCase):
    def test_capacity_and_overflow_bits(self):
        with tempfile.TemporaryDirectory(prefix='ov6-') as tmp:
            src = Path(tmp) / 'sets.c'
            src.write_text(SETS)
            exe = Path(tmp) / 'sets'
            built = subprocess.run(['clang', '-std=c99', '-Wall', '-Wextra', '-Werror',
                                    '-fsanitize=address,undefined', '-I', str(D3D12), str(src), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stderr)
        self.assertIn('#define AGX_D3D12_JOB_RANGES 128u', HEADER)
        # merges keep the bits (a copied set carries its source's reasons)
        self.assertNotIn('to->Overflow = 1u', HEADER)


class DdiRangeTests(unittest.TestCase):
    def test_unsized_accesses_end_at_the_resource_or_heap_never_open(self):
        for name in ('JobDescriptorRange', 'JobHeapRange'):
            fn = body(DDI, name)
            self.assertIn('HeapSpanEnd(', fn)
            self.assertNotIn('~0ull', fn)
        end = body(DDI, 'HeapSpanEnd')
        self.assertIn('DescriptorHeapBytes(heap)', end)
        self.assertIn('span.offset + span.bytes > end', end)

    def test_indirect_arguments_are_bounded_by_the_signature(self):
        fn = body(DDI, 'JobIndirectRanges')
        self.assertIn('(indirect.max_count - 1u) * indirect.stride + indirect.record_bytes', fn)
        self.assertIn('indirect.count_offset, 4u', fn)
        self.assertIn('indirect.record_bytes = signature->record_bytes;', DDI)
        self.assertEqual(DDI.count('JobIndirectRanges('), 3)  # definition, dispatch, draw
        self.assertNotIn('d.indirect.args_offset, 0, vertex', DDI)

    def test_spans_follow_placed_buffers_and_never_an_alias(self):
        self.assertIn('HeapAddSpan(base->heap, offset, resource_arg->Width);', DDI)
        add = body(DDI, 'HeapAddSpan')
        self.assertIn('heap->alias', add)
        free = body(DDI, 'HeapFreeSpans')
        self.assertIn('!heap->alias', free)
        release = body(DDI, 'ReleaseHeap')
        self.assertLess(release.index('HeapFreeSpans(heap)'), release.index('memset(heap, 0'))

    def test_uav_clears_are_placeable(self):
        self.assertIn('kCommandClearUav) JobDescriptorRange(sink, &clears[k].clear_uav.view, 1u | AGX_D3D12_JOB_SRC_CLEAR)', DDI)

    def test_receipt_carries_the_overflow_nibbles(self):
        self.assertIn('values[27] = owner_ov5_overflow_sequence;', OWNER)
        self.assertIn('"measure-d3d12-ov2-refusal", S_OK, values, 29u', OWNER)
        self.assertIn('owner_ov5_overflow_sequence = 0;', OWNER)
        self.assertIn('(owner_job_barriers.RangeLastCompute.Overflow & 3u) << 2u', OWNER)


if __name__ == '__main__':
    unittest.main()
