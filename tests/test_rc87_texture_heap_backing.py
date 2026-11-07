"""rc87: a texture-only D3D12 heap gets no full-size BO until a tile mapping needs it.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
EXP2017 (GravityMark FHD 100k and 200k): the app created four texture-only
DEFAULT heaps (DDI heap flags 0x20 twice, 0x2 twice; 400 MiB) and placed 39
textures in them. The native owner gives every placed texture a BO of its
own (C36), yet CreateHeapAndResource also gave each heap a GPU-local BO of
its full size that nothing ever read: 400 MiB of segment 3 on top of the
textures' 249 MiB, with the working set (about 3.2 GiB of native BOs) above
the LOCAL budget (about 2.9 GiB) and 41 / 1255 residency trims. The model
below replays those receipts and requires that the deferred heaps cost only
their 64 KiB stubs while every other heap keeps its backing. The predicate
must keep buffer-capable, flag-less and CPU-visible heaps backed (placed
buffers and Map use their memory), and the DDI must back a deferred heap
before a tile mapping or a placed buffer uses it, and free its stub.
"""
from pathlib import Path
import os
import re
import subprocess
import sys
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
D3D12 = ROOT / 'drivers/apple-agx/d3d12'
HEADER = D3D12 / 'agx_d3d12_heap_backing.h'
DDI = D3D12 / 'agx_d3d12_ddi.cpp'
TOOL = ROOT / 'tools/agx_working_set_inventory.py'

# EXP2017 gm2017a-umd.log: {heap flags, CPU page property, MiB} of every
# heap-alone creation (resource type BUFFER spanning the heap).
EXP2017_HEAPS = [(0x20, 0, 128), (0x20, 0, 128), (0x2, 0, 128), (0x2, 0, 16),
                 (0x4, 0, 256), (0x4, 0, 128), (0xc, 1, 128), (0xc, 1, 32), (0xc, 2, 0.5)]

HARNESS = r'''
#include <assert.h>
#include <stdio.h>
#include "agx_d3d12_heap_backing.h"
int main(void) {
  /* texture-only DEFAULT heaps: deferred */
  assert(AgxD3d12HeapDefersBacking(0x2u, 0u));
  assert(AgxD3d12HeapDefersBacking(0x20u, 0u));
  assert(AgxD3d12HeapDefersBacking(0x22u, 0u));
  assert(AgxD3d12HeapDefersBacking(0x22u | 0x40u, 0u));
  /* placed buffers may use the memory */
  assert(!AgxD3d12HeapDefersBacking(0x4u, 0u));
  assert(!AgxD3d12HeapDefersBacking(0x26u, 0u));
  assert(!AgxD3d12HeapDefersBacking(0xcu, 0u));
  /* no category named: keep the backing */
  assert(!AgxD3d12HeapDefersBacking(0u, 0u));
  assert(!AgxD3d12HeapDefersBacking(0x10u, 0u));
  /* CPU-visible (UPLOAD/READBACK/custom): Map reads the memory */
  assert(!AgxD3d12HeapDefersBacking(0x2u, 1u));
  assert(!AgxD3d12HeapDefersBacking(0x20u, 2u));
  puts("rc87 heap backing predicate: PASS");
  return 0;
}
'''


def receipt(name, values, hr=0):
    return 'x %s hr=0x%08x pid=1 tid=1 session=1 qpc=%d %s\n' % (
        name, hr, receipt.qpc, ' '.join('%08x' % v for v in values))


receipt.qpc = 1


def exp2017_log(deferred):
    """The EXP2017 heap and placed-texture receipts as the DDI writes them."""
    out = []
    for flags, cpu, mib in EXP2017_HEAPS:
        size = int(mib * (1 << 20)) >> 16
        receipt.qpc += 1
        out.append(receipt('measure-d3d12-create-heap-resource', [1, size, cpu, 0, flags, 1, 1, size << 16, 1, 3]))
        status = 3 if deferred and flags in (0x2, 0x20) and cpu == 0 else 2
        out.append(receipt('measure-d3d12-heap', [status, size, 1, 0, 0]))
    # 39 placed textures (15 RT/DS, 178 MiB; 24 sampled, 70.5 MiB): own BOs
    for i in range(39):
        out.append(receipt('measure-d3d12-create-heap-resource', [0, 0, 0, 0, 0, 1, 3, 0x400, 0, 2]))
        out.append(receipt('measure-d3d12-committed-texture',
                           [0x1c, 0x400, 0x400, 0xbe if i < 15 else 0x2f, 0x201 if i < 15 else 0x202]))
    return ''.join(out)


class TextureHeapBacking(unittest.TestCase):
    def test_predicate(self):
        self.assertTrue(HEADER.exists(), 'agx_d3d12_heap_backing.h is missing')
        with tempfile.TemporaryDirectory(prefix='rc87-heap-') as tmp:
            src = Path(tmp) / 'harness.c'
            exe = Path(tmp) / 'harness'
            src.write_text(HARNESS)
            built = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Werror',
                                    '-I', str(D3D12), str(src), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr[-3000:])
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def inventory(self, deferred):
        sys.path.insert(0, str(TOOL.parent))
        try:
            import agx_working_set_inventory as tool
        finally:
            sys.path.pop(0)
        with tempfile.TemporaryDirectory(prefix='rc87-inv-') as tmp:
            log = Path(tmp) / 'umd.log'
            log.write_text(exp2017_log(deferred))
            inv, _ = tool.inventory(str(log))
        return {k: v[2] for k, v in inv.items()}

    def test_exp2017_texture_heaps_cost_only_their_stubs(self):
        before, after = self.inventory(False), self.inventory(True)
        heap = lambda inv: sum(v for k, v in inv.items() if 'texture-only heap' in k)
        self.assertEqual(heap(before), 400 << 20)
        self.assertEqual(heap(after), 4 << 16)
        # placed textures and every other heap keep their bytes
        rest = lambda inv: {k: v for k, v in inv.items() if 'texture-only heap' not in k}
        self.assertEqual(rest(before), rest(after))
        self.assertEqual(sum(before.values()) - sum(after.values()), (400 << 20) - (4 << 16))

    def test_ddi_defers_backs_and_frees(self):
        ddi = DDI.read_text()
        self.assertIn('#include "agx_d3d12_heap_backing.h"', ddi)
        # the heap-alone / committed path chooses through the predicate
        create = ddi[ddi.index('static HRESULT APIENTRY CreateHeapAndResource('):]
        create = create[:create.index('\n}\n')]
        self.assertRegex(create, r'AgxD3d12HeapDefersBacking\([^;]*\)\s*&&\s*DeferTextureHeaps\(\)\s*\?\s*'
                                 r'CreateDeferredHeap\(device, sized, heap\)')
        # a placed buffer gets the real backing before it takes the heap VA
        placed = create[create.index('UINT reason = !base'):create.index('resource->gpuva = base->heap->gpuva + offset;')]
        self.assertIn('EnsureHeapBacking(device, base->heap)', placed)
        # a tile mapping backs the heap before the native check
        tiles = ddi[ddi.index('static VOID APIENTRY UpdateTileMappings('):]
        tiles = tiles[:tiles.index('\n}\n')]
        self.assertLess(tiles.index('EnsureHeapBacking(device, heap)'), tiles.index('heap->native'))
        # the stub is released with the heap, materialized or not
        release = ddi[ddi.index('static void ReleaseHeap('):]
        release = release[:release.index('\n}\n')]
        native = release.index('    if (heap->native) {')
        self.assertLess(release.index('heap->stub'), native)
        self.assertIn('AgxD3d12OwnerDestroyBuffer', release[release.index('heap->stub'):native])
        # backing is GPU-only (cpu_visible FALSE) and registered for lookups
        ensure = ddi[ddi.index('static HRESULT EnsureHeapBacking('):]
        ensure = ensure[:ensure.index('\n}\n')]
        self.assertRegex(ensure, r'AgxD3d12OwnerCreateBuffer\(owner, heap->bytes, FALSE, &buffer\)')
        self.assertIn('RegisterHeap(device, heap)', ensure)
        self.assertTrue(re.search(r'AcquireSRWLockExclusive\(&g_heap_backing_lock\)', ensure))


if __name__ == '__main__':
    unittest.main()
