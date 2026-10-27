"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
RC67 (EXP1986 GravityMark HD: 2.18 M "Input assembly counters", about 1020
per Execute): with a pipeline-statistics query active, native Asahi counts
every unrolled record of a multi-draw indirect with its own libagx dispatch
and CDM barrier. The replacement counts all records of one unrolled call in a
single dispatch. It must produce exactly the native per-record sums (low 32
bits, count * instances vertices, decomposed primitives * instances), never
read a record past the CPU-known count, keep every native-only case (restart,
patches, passthrough GS, render condition, GS/TES clipper rule, absent
queries) on the native per-draw path, and suppress native counting only for
the sub-draws it already counted.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_d3d10_shader_fields_init import ROOT, local_tree

WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys'
KERNEL = WINSYS / 'agx_win32_query_ia.cl'

HARNESS = r'''
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned uint;
#define global
#define constant const
static struct { unsigned x; } cl_global_id;
#include "compiler/libcl/libcl.h"
#undef KERNEL
#define KERNEL(n) static void
#define atomic_add(p,v) __atomic_fetch_add((p),(v),__ATOMIC_RELAXED)
#include "agx_win32_query_ia.cl"
#undef NDEBUG
#include <assert.h>
'''

# Literal decompositions, independent of the production arithmetic.
LITERAL = r'''
static uint32_t literal_prims(unsigned mode, uint32_t n) {
   switch (mode) {
   case MESA_PRIM_POINTS: return n;
   case MESA_PRIM_LINES: return n / 2;
   case MESA_PRIM_LINE_STRIP: return n >= 2 ? n - 1 : 0;
   case MESA_PRIM_LINE_LOOP: return n >= 2 ? n : 0;
   case MESA_PRIM_TRIANGLES: return n / 3;
   case MESA_PRIM_TRIANGLE_STRIP:
   case MESA_PRIM_TRIANGLE_FAN: return n >= 3 ? n - 2 : 0;
   default: abort();
   }
}
'''

KERNEL_PROGRAM = HARNESS + LITERAL + r'''
static void run(unsigned mode, unsigned count, unsigned stride, bool reverse) {
   /* Exactly count records: ASan reports any read past the last one. */
   uint8_t *records = calloc(count ? count : 1, stride);
   uint32_t expect_v = 0, expect_p = 0;
   for (unsigned i = 0; i < count; ++i) {
      uint32_t *r = (uint32_t *)(records + (size_t)i * stride);
      r[0] = 3 + 7 * i; r[1] = 1 + i % 5;
      if (stride >= 20) { r[2] = 0xdead; r[3] = 0xbeef; r[4] = 99; }
      expect_v += r[0] * r[1];
      expect_p += literal_prims(mode, r[0]) * r[1];
   }
   uint64_t slots[5] = {101, 202, 303, 404, 505};
   unsigned lanes = (count + 31u) / 32u * 32u + 32u; /* rounded grid plus spare */
   for (unsigned k = 0; k < lanes; ++k) {
      cl_global_id.x = reverse ? lanes - 1 - k : k;
      libagx_win32_query_ia_records((uintptr_t)&slots[0], (uintptr_t)&slots[1],
         (uintptr_t)&slots[2], (uintptr_t)&slots[3], (uintptr_t)&slots[4],
         (uintptr_t)records, count, stride, mode);
   }
   /* Native IA uses the low 32 bits of each 64-bit slot. */
   assert(slots[0] == (uint32_t)(101 + expect_v));
   assert(slots[2] == (uint32_t)(303 + expect_v));
   assert(slots[1] == (uint32_t)(202 + expect_p));
   assert(slots[3] == (uint32_t)(404 + expect_p));
   assert(slots[4] == (uint32_t)(505 + expect_p));
   /* Zero addresses are absent queries and are never dereferenced. */
   for (unsigned k = 0; k < lanes; ++k) {
      cl_global_id.x = k;
      libagx_win32_query_ia_records(0, 0, 0, 0, 0, (uintptr_t)records, count, stride, mode);
   }
   free(records);
}

int main(void) {
   unsigned modes[] = {MESA_PRIM_POINTS, MESA_PRIM_LINES, MESA_PRIM_LINE_STRIP,
                       MESA_PRIM_LINE_LOOP, MESA_PRIM_TRIANGLES,
                       MESA_PRIM_TRIANGLE_STRIP, MESA_PRIM_TRIANGLE_FAN};
   unsigned counts[] = {0, 1, 2, 31, 32, 33, 150, 1000};
   unsigned strides[] = {16, 20, 32};
   for (unsigned m = 0; m < 7; ++m)
      for (unsigned c = 0; c < 8; ++c)
         for (unsigned s = 0; s < 3; ++s)
            run(modes[m], counts[c], strides[s], (c + s) & 1);
   /* GravityMark MaxCount with indexed 20-byte records. */
   run(MESA_PRIM_TRIANGLES, 60000, 20, true);
   /* 32-bit wrap of the product and of the sum, as native uint32 counters. */
   uint32_t big[2][2] = {{0x10000u, 0x10001u}, {0xffffffffu, 3u}};
   uint64_t slots[5] = {0};
   for (unsigned k = 0; k < 64; ++k) {
      cl_global_id.x = k;
      libagx_win32_query_ia_records((uintptr_t)&slots[0], (uintptr_t)&slots[1],
         (uintptr_t)&slots[2], (uintptr_t)&slots[3], (uintptr_t)&slots[4],
         (uintptr_t)big, 2, 8, MESA_PRIM_POINTS);
   }
   uint32_t v = 0x10000u * 0x10001u + 0xffffffffu * 3u;
   assert(slots[0] == v && slots[1] == v && slots[2] == v && slots[3] == v && slots[4] == v);
   return 0;
}
'''

def compile_run(test, source, name):
    mesa = local_tree('reference', 'mesa')
    test.assertIsNotNone(mesa)
    with tempfile.TemporaryDirectory() as td:
        p = Path(td)
        (p / (name + '.c')).write_text(source)
        cmd = ['clang', '-std=c11', '-Werror', '-Wall', '-Wno-unused-function',
               '-Wno-c23-extensions', '-fsanitize=address,undefined', '-DNDEBUG',
               '-Dstatic_assert=_Static_assert', '-DHAVE___BUILTIN_CLZ',
               '-I' + str(WINSYS), '-I' + str(mesa / 'src'), '-I' + str(mesa / 'include'),
               '-I' + str(mesa / 'src/gallium/include'),
               str(p / (name + '.c')), '-o', str(p / name)]
        r = subprocess.run(cmd, capture_output=True, text=True)
        test.assertEqual(r.returncode, 0, r.stderr)
        r = subprocess.run([str(p / name)], capture_output=True, text=True)
        test.assertEqual(r.returncode, 0, r.stderr)


class QueryIaRecords(unittest.TestCase):
    def test_kernel_matches_native_per_record_sums(self):
        self.assertTrue(KERNEL.exists(), 'batched IA statistics kernel is missing')
        compile_run(self, KERNEL_PROGRAM, 'kernel')


if __name__ == '__main__':
    unittest.main()
