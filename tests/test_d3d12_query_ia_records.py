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
import hashlib
import importlib.util
import json
import subprocess
import tempfile
import unittest

from test_d3d10_shader_fields_init import ROOT, local_tree, project
from test_d3d12_owner_allocation_failure import function

WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys'
SCRIPTS = ROOT / 'drivers/apple-agx/mesa/scripts'
KERNEL = WINSYS / 'agx_win32_query_ia.cl'
HELPER = WINSYS / 'agx_win32_query_ia.h'
PROJECTION = SCRIPTS / 'native-asahi-query-ia.py'

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

HELPER_PROGRAM = HARNESS + LITERAL + r'''
#include "pipe/p_defines.h"
#define perf_debug(dev, ...) ((void)(dev))
struct pipe_resource { unsigned width0; };
struct pipe_draw_info { unsigned mode; bool primitive_restart; };
struct pipe_draw_indirect_info {
   unsigned offset, stride, draw_count;
   struct pipe_resource *buffer, *indirect_draw_count;
   void *count_from_stream_output;
};
struct agx_query { uint64_t slot; unsigned writes; };
struct agx_device { unsigned unused; };
struct pipe_screen { struct agx_device dev; };
struct agx_encoder { void *bo; uint8_t *current; };
struct agx_context;
struct agx_batch { struct agx_context *ctx; struct agx_encoder cdm; unsigned reads; };
struct agx_win32_indirect_stream;
struct agx_context {
   struct { struct pipe_screen *screen; } base;
   struct agx_win32_indirect_stream *windows_indirect_stream;
   bool windows_query_ia_records, active_queries, in_tess, active_draw_without_restart, any_faults;
   struct agx_query *pipeline_statistics[PIPE_STAT_QUERY_COUNT];
   struct { void *shader; } stage[MESA_SHADER_STAGES];
   void *cond_query;
   struct agx_batch *batch;
};
struct agx_grid { unsigned x; };
#define AGX_BARRIER_ALL 1
static struct agx_grid agx_1d(unsigned x) { return (struct agx_grid){x}; }
static uint8_t cdm_storage[64];
static unsigned allocations, passthrough, faults, dispatches, last_grid;
static bool fail_allocation;
static struct agx_device *agx_device(struct pipe_screen *s) { return &s->dev; }
static struct agx_batch *agx_get_batch(struct agx_context *c) { return c->batch; }
static struct agx_encoder agx_encoder_allocate(struct agx_batch *b, struct agx_device *d) {
   (void)b; (void)d; ++allocations;
   return fail_allocation ? (struct agx_encoder){0} : (struct agx_encoder){cdm_storage, cdm_storage};
}
static void AgxWin32AsahiMarkContextFault(struct agx_context *c, unsigned site) {
   assert(site); c->any_faults = true; ++faults;
}
static uint8_t *record_base;
static uint64_t agx_indirect_buffer_ptr(struct agx_batch *b, const struct pipe_draw_indirect_info *in) {
   ++b->reads; return (uintptr_t)record_base + in->offset;
}
static uint64_t agx_get_query_address(struct agx_batch *b, struct agx_query *q) {
   (void)b; assert(q); ++q->writes; return (uintptr_t)&q->slot;
}
static bool agx_needs_passthrough_gs(struct agx_context *c, const struct pipe_draw_info *i,
                                     const struct pipe_draw_indirect_info *in, bool *xfb) {
   (void)c; (void)i; assert(in); *xfb = false; return passthrough != 0;
}
/* The dispatch executes the production kernel over its whole grid. */
#define libagx_win32_query_ia_records(b, grid, barrier, a0, a1, a2, a3, a4, rec, n, stride, mode) do { \
   ++dispatches; last_grid = (grid).x; assert((barrier) == AGX_BARRIER_ALL); \
   for (unsigned lane = 0; lane < (grid).x + 31u; ++lane) { \
      cl_global_id.x = lane; \
      libagx_win32_query_ia_records(a0, a1, a2, a3, a4, rec, n, stride, mode); \
   } } while (0)
#include "agx_win32_query_ia.h"

static struct agx_query queries[PIPE_STAT_QUERY_COUNT];
static struct pipe_resource buffer;
static struct pipe_draw_indirect_info in;
static struct pipe_draw_info info;
static struct agx_context ctx;
static struct agx_batch batch;
static struct pipe_screen screen;
static uint32_t native[5];

static void reset(unsigned count, unsigned stride, unsigned mode) {
   memset(queries, 0, sizeof(queries)); memset(&ctx, 0, sizeof(ctx));
   memset(&batch, 0, sizeof(batch)); memset(native, 0, sizeof(native));
   free(record_base);
   record_base = calloc(count, stride);
   for (unsigned i = 0; i < count; ++i) {
      uint32_t *r = (uint32_t *)(record_base + (size_t)i * stride);
      r[0] = 4 + 3 * i; r[1] = 1 + i % 3;
      native[0] += r[0] * r[1];
      if (mode != MESA_PRIM_PATCHES) native[1] += literal_prims(mode, r[0]) * r[1];
   }
   buffer.width0 = count * stride;
   in = (struct pipe_draw_indirect_info){.offset = 0, .stride = stride,
        .draw_count = count, .buffer = &buffer};
   info = (struct pipe_draw_info){.mode = mode};
   batch.ctx = &ctx; ctx.batch = &batch; ctx.base.screen = &screen;
   ctx.active_queries = true;
   for (unsigned i = 0; i < PIPE_STAT_QUERY_COUNT; ++i) ctx.pipeline_statistics[i] = &queries[i];
   static int vs; ctx.stage[MESA_SHADER_VERTEX].shader = &vs;
   allocations = passthrough = faults = dispatches = last_grid = 0; fail_allocation = false;
}

static void expect_native(bool clipper) {
   assert(queries[PIPE_STAT_QUERY_IA_VERTICES].slot == native[0]);
   assert(queries[PIPE_STAT_QUERY_VS_INVOCATIONS].slot == native[0]);
   assert(queries[PIPE_STAT_QUERY_IA_PRIMITIVES].slot == native[1]);
   assert(queries[PIPE_STAT_QUERY_C_PRIMITIVES].slot == (clipper ? native[1] : 0));
   assert(queries[PIPE_STAT_QUERY_C_INVOCATIONS].slot == (clipper ? native[1] : 0));
   /* Shader-side and other-stage statistics are never written here. */
   assert(!queries[PIPE_STAT_QUERY_PS_INVOCATIONS].slot && !queries[PIPE_STAT_QUERY_GS_INVOCATIONS].slot);
   assert(!queries[PIPE_STAT_QUERY_PS_INVOCATIONS].writes && !queries[PIPE_STAT_QUERY_CS_INVOCATIONS].writes);
}

static void refused(const char *why) {
   assert(!agx_win32_query_ia_records(&ctx, &info, &in) && why);
   assert(!dispatches && !faults && !batch.reads);
   for (unsigned i = 0; i < PIPE_STAT_QUERY_COUNT; ++i) assert(!queries[i].slot && !queries[i].writes);
}

int main(void) {
   unsigned modes[] = {MESA_PRIM_TRIANGLES, MESA_PRIM_TRIANGLE_STRIP, MESA_PRIM_LINES, MESA_PRIM_POINTS};
   for (unsigned m = 0; m < 4; ++m) {
      reset(150, 20, modes[m]);
      assert(agx_win32_query_ia_records(&ctx, &info, &in));
      assert(dispatches == 1 && last_grid == 150 && allocations == 1 && batch.reads == 1 && !faults);
      expect_native(true);
   }
   /* An existing CDM encoder is reused. */
   reset(5, 16, MESA_PRIM_TRIANGLES); batch.cdm = (struct agx_encoder){cdm_storage, cdm_storage};
   assert(agx_win32_query_ia_records(&ctx, &info, &in) && !allocations && dispatches == 1);
   expect_native(true);
   /* A GS or TES writes the clipper counters itself (native: scratch page). */
   static int shader;
   reset(7, 16, MESA_PRIM_TRIANGLES); ctx.stage[MESA_SHADER_GEOMETRY].shader = &shader;
   assert(agx_win32_query_ia_records(&ctx, &info, &in)); expect_native(false);
   assert(!queries[PIPE_STAT_QUERY_C_PRIMITIVES].writes && !queries[PIPE_STAT_QUERY_C_INVOCATIONS].writes);
   reset(7, 16, MESA_PRIM_TRIANGLES); ctx.stage[MESA_SHADER_TESS_EVAL].shader = &shader;
   assert(agx_win32_query_ia_records(&ctx, &info, &in)); expect_native(false);
   /* Only the clipper statistics bound, with a GS: nothing for IA to count. */
   reset(7, 16, MESA_PRIM_TRIANGLES); ctx.stage[MESA_SHADER_GEOMETRY].shader = &shader;
   ctx.pipeline_statistics[PIPE_STAT_QUERY_IA_VERTICES] = ctx.pipeline_statistics[PIPE_STAT_QUERY_IA_PRIMITIVES] =
      ctx.pipeline_statistics[PIPE_STAT_QUERY_VS_INVOCATIONS] = NULL;
   refused("gs clipper only");
   /* Absent queries get no address and no writer registration. */
   reset(9, 16, MESA_PRIM_TRIANGLES);
   ctx.pipeline_statistics[PIPE_STAT_QUERY_IA_PRIMITIVES] = NULL;
   ctx.pipeline_statistics[PIPE_STAT_QUERY_C_INVOCATIONS] = NULL;
   assert(agx_win32_query_ia_records(&ctx, &info, &in));
   assert(!queries[PIPE_STAT_QUERY_IA_PRIMITIVES].slot && !queries[PIPE_STAT_QUERY_C_INVOCATIONS].slot);
   assert(queries[PIPE_STAT_QUERY_IA_VERTICES].slot == native[0] && queries[PIPE_STAT_QUERY_C_PRIMITIVES].slot == native[1]);

   /* Every native-only case keeps the per-draw native path. */
   reset(8, 16, MESA_PRIM_TRIANGLES); ctx.active_queries = false; refused("queries inactive");
   reset(8, 16, MESA_PRIM_TRIANGLES); memset(ctx.pipeline_statistics, 0, sizeof(ctx.pipeline_statistics)); refused("no statistics");
   reset(8, 16, MESA_PRIM_TRIANGLES); info.primitive_restart = true; refused("restart scans indices");
   reset(8, 16, MESA_PRIM_PATCHES); refused("patches");
   reset(8, 16, MESA_PRIM_TRIANGLES); ctx.in_tess = true; refused("inside tessellation");
   reset(8, 16, MESA_PRIM_TRIANGLES); ctx.active_draw_without_restart = true; refused("restart unroll");
   reset(8, 16, MESA_PRIM_TRIANGLES); ctx.cond_query = &shader; refused("render condition");
   reset(8, 16, MESA_PRIM_TRIANGLES); passthrough = 1; refused("passthrough gs");
   reset(8, 16, MESA_PRIM_TRIANGLES); ctx.windows_indirect_stream = (void *)&shader; refused("gpu stream counts");
   reset(8, 16, MESA_PRIM_TRIANGLES); ctx.windows_query_ia_records = true; refused("already counted");
   reset(8, 16, MESA_PRIM_TRIANGLES); in.draw_count = 1; refused("single draw is native");
   reset(8, 16, MESA_PRIM_TRIANGLES); in.indirect_draw_count = &buffer; refused("gpu count");
   reset(8, 16, MESA_PRIM_TRIANGLES); in.count_from_stream_output = &shader; refused("draw auto");
   reset(8, 16, MESA_PRIM_TRIANGLES); in.stride = 6; refused("unaligned stride");
   reset(8, 16, MESA_PRIM_TRIANGLES); in.stride = 4; refused("short stride");
   reset(8, 16, MESA_PRIM_TRIANGLES); in.offset = 2; refused("unaligned offset");
   reset(8, 16, MESA_PRIM_TRIANGLES); buffer.width0 = 7 * 16 + 7; refused("last record out of range");
   reset(8, 16, MESA_PRIM_TRIANGLES); ctx.stage[MESA_SHADER_VERTEX].shader = NULL; refused("no vertex shader");
   /* The last record may end exactly at the buffer end (8 counted bytes). */
   reset(8, 16, MESA_PRIM_TRIANGLES); buffer.width0 = 7 * 16 + 8;
   assert(agx_win32_query_ia_records(&ctx, &info, &in)); expect_native(true);
   /* CDM allocation failure faults the context; nothing is dispatched. */
   reset(8, 16, MESA_PRIM_TRIANGLES); fail_allocation = true;
   assert(!agx_win32_query_ia_records(&ctx, &info, &in) && ctx.any_faults && faults == 1 && !dispatches);
   free(record_base);
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


def projection_module():
    spec = importlib.util.spec_from_file_location('query_ia_projection', PROJECTION)
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


class QueryIaRecords(unittest.TestCase):
    def test_kernel_matches_native_per_record_sums(self):
        self.assertTrue(KERNEL.exists(), 'batched IA statistics kernel is missing')
        compile_run(self, KERNEL_PROGRAM, 'kernel')

    def test_helper_dispatches_once_and_keeps_native_cases(self):
        self.assertTrue(HELPER.exists(), 'batched IA statistics helper is missing')
        compile_run(self, HELPER_PROGRAM, 'helper')

    def test_projection_counts_unrolled_records_once(self):
        with tempfile.TemporaryDirectory() as td:
            out = project(Path(td) / 'native')
            state = (out / 'src/gallium/drivers/asahi/agx_state.c').read_text()
            body = function(state, 'static void\nagx_draw_vbo_windows_body(')
            unroll = body[body.index('   if (indirect && indirect->draw_count > 1 && !indirect->indirect_draw_count) {'):]
            unroll = unroll[:unroll.index('      return;\n   }') + len('      return;\n   }')]
            call = unroll.index('agx_win32_query_ia_records(ctx, info, indirect)')
            self.assertLess(call, unroll.index('util_draw_multi_unroll_indirect('))
            self.assertLess(unroll.index('if (ctx->any_faults) return;'),
                            unroll.index('util_draw_multi_unroll_indirect('))
            self.assertLess(unroll.index('util_draw_multi_unroll_indirect('),
                            unroll.index('ctx->windows_query_ia_records = outer_ia;'))
            # The native per-draw gate skips only the already counted records.
            start = body.index('   /* Increment IA statistics')
            guard = body[body.index('   if (', start):body.index('      uint64_t ptr;', start)]
            self.assertIn('!ctx->windows_query_ia_records', guard)
            self.assertLess(state.index('#include "agx_win32_query_ia.h"'),
                            state.index('static void\nagx_draw_vbo_windows_body('))
            header = (out / 'src/gallium/drivers/asahi/agx_state.h').read_text()
            self.assertIn('unsigned windows_query_ia_records;', header)
            library = (out / 'src/asahi/lib/libagx_shaders.h').read_text()
            self.assertIn('    LIBAGX_WIN32_QUERY_IA_RECORDS,\n', library)
            self.assertIn('#define libagx_win32_query_ia_records(', library)
            self.assertIn('struct libagx_win32_query_ia_records_args {', library)

    def test_generated_bundle_matches_sources_and_both_chip_tables(self):
        module = projection_module()
        directory = module.generated(ROOT)  # verifies source/output SHA-256 pins
        manifest = json.loads((directory / 'manifest.json').read_text())
        self.assertEqual(manifest['targets'], ['g13g', 'g13x'])
        mesa = local_tree('reference', 'mesa')
        for name, path in (('libcl.h', 'src/compiler/libcl/libcl.h'),
                           ('shader_enums.h', 'src/compiler/shader_enums.h')):
            self.assertEqual(hashlib.sha256((mesa / path).read_bytes()).hexdigest(),
                             manifest['inputs'][name])
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        source = (build / 'src/asahi/lib/libagx_shaders.c').read_text()
        merged = module.project_library_c(source, ROOT)
        for chip in ('g13g', 'g13x'):
            table = merged.split('const uint32_t *libagx_' + chip + '[', 1)[1]
            table = table[:table.index('};')]
            self.assertIn('[LIBAGX_WIN32_QUERY_IA_RECORDS] = libagx_win32_query_ia_records_0_' + chip + ',', table)
            self.assertIn('[LIBAGX_HELPER]', table)
        self.assertEqual(merged.count('static const uint32_t libagx_win32_query_ia_records_'), 2)
        with self.assertRaises(RuntimeError):
            module.project_library_c(merged, ROOT)
        for script in ('build-native-asahi-state.py', 'build-asahi-runtime-closure.py'):
            self.assertIn('native-asahi-query-ia.py', (SCRIPTS / script).read_text(), script)


    def test_projected_dispatch_links_both_tables(self):
        """WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
        A header-only insertion, a missing chip table entry or a mismatched
        enum index would launch no binary or another program. Link the real
        projected library (all Windows kernels) and invoke the macro.
        """
        from test_dxbc_to_nir import host_flags
        mesa = local_tree('reference', 'mesa')
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            out = project(p / 'native')
            source = (build / 'src/asahi/lib/libagx_shaders.c').read_text()
            for name in ('indirect-stream', 'query-timestamp', 'query-ia'):
                spec = importlib.util.spec_from_file_location(
                    name.replace('-', '_'), SCRIPTS / ('native-asahi-' + name + '.py'))
                m = importlib.util.module_from_spec(spec)
                spec.loader.exec_module(m)
                source = m.project_library_c(source, ROOT)
            library = out / 'src/asahi/lib/libagx_shaders.c'
            library.write_text(source)
            (p / 'test.c').write_text(r"""
#include <assert.h>
#include <stdint.h>
#include <string.h>
#include "libagx_shaders.h"
static unsigned calls;
static void dispatch(unsigned context, unsigned grid, unsigned barrier,
                     enum libagx_program program, const void *data, unsigned size)
{
   assert(context == 7 && grid == 150 && barrier == 3);
   assert(program == LIBAGX_WIN32_QUERY_IA_RECORDS);
   assert(size == sizeof(struct libagx_win32_query_ia_records_args) && size == 60);
   struct libagx_win32_query_ia_records_args a;
   memcpy(&a, data, sizeof(a));
   assert(a.ia_vertices == 0x1111 && a.ia_primitives == 0x2222 && a.vs_invocations == 0x3333);
   assert(a.c_primitives == 0x4444 && a.c_invocations == 0x5555 && a.records == 0x123456789aull);
   assert(a.count == 150 && a.stride_B == 20 && a.mode == 4);
   ++calls;
}
#define MESA_DISPATCH_PRECOMP dispatch
int main(void)
{
   libagx_win32_query_ia_records(7, 150, 3, 0x1111, 0x2222, 0x3333, 0x4444, 0x5555,
                                 0x123456789aull, 150, 20, 4);
   assert(calls == 1);
   for (unsigned i = 0; i < LIBAGX_NUM_PROGRAMS; ++i)
      assert(libagx_g13g[i] && libagx_g13x[i]);
   assert(libagx_g13g[LIBAGX_WIN32_QUERY_IA_RECORDS] != libagx_g13g[LIBAGX_WIN32_QUERY_TIMESTAMP]);
   assert(libagx_g13x[LIBAGX_WIN32_QUERY_IA_RECORDS] != libagx_g13x[LIBAGX_WIN32_INDIRECT_STREAM]);
   assert(libagx_g13g[LIBAGX_WIN32_QUERY_IA_RECORDS] != libagx_g13x[LIBAGX_WIN32_QUERY_IA_RECORDS]);
   return 0;
}
""")
            r = subprocess.run([
                'clang', '-std=c11', '-Wall', '-Werror', '-Dstatic_assert=_Static_assert',
                '-fsanitize=address,undefined', '-I' + str(out / 'src/asahi/lib'),
                '-I' + str(mesa / 'src'), *host_flags(build), '-UNDEBUG',
                str(p / 'test.c'), str(library), '-o', str(p / 'test')],
                capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)
            r = subprocess.run([str(p / 'test')], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)

if __name__ == '__main__':
    unittest.main()
