"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
RC67 (EXP1988 HD: 28 QUERY escapes on 64 KiB slots per Execute, 12.4 ms): the
11 PIPELINE_STATISTICS_SINGLE queries of one D3D12 index each owned a 64 KiB
BO, and a busy Begin swapped in 11 fresh ones. They must share exactly one
BO (query i at 16 * i, no other group in it), be reset as a unit so a busy
member never makes agx_begin_query allocate a lone BO, keep each public
pipe_query identity, retire old storage only by generation, free the shared
BO after its last member, and leave the queries untouched on failure. The
D3D12 owner must group before beginning, and only for statistics Begin.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_d3d10_shader_fields_init import ROOT, local_tree, project
from test_d3d12_query_owner import PRELUDE as OWNER_PRELUDE

WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys'

LIFECYCLE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include "pipe/p_defines.h"
#ifndef ARRAY_SIZE
#define ARRAY_SIZE(a) (sizeof(a) / sizeof((a)[0]))
#endif
#define AGX_MAX_BATCHES 4
#define AGX_BO_WRITEBACK 4u
struct pipe_context { void *screen; };
struct pipe_query;
struct agx_va { uint64_t addr; };
struct agx_bo { struct agx_va *va; struct agx_va storage; unsigned refs; uint8_t map[256]; size_t size; };
struct agx_ptr { void *cpu; uint64_t gpu; };
struct agx_query {
   struct agx_query *windows_next;
   uint64_t windows_timestamp_seed;
   unsigned windows_timestamp_count;
   struct agx_bo *windows_timestamp_bos[AGX_MAX_BATCHES];
   unsigned type, index;
   uint64_t writer_generation[AGX_MAX_BATCHES];
   struct agx_bo *bo;
   struct agx_ptr ptr;
};
struct agx_device { unsigned unused; };
struct agx_context {
   struct pipe_context base;
   struct agx_query *windows_retired_queries;
   struct { uint64_t generation[AGX_MAX_BATCHES]; } batches;
};
static struct agx_device device;
static struct agx_context context;
static unsigned created, freed, fail_after = ~0u;
static uint64_t next_va = 0x1100000000ull;
static struct agx_context *agx_context(struct pipe_context *p) { return (struct agx_context *)p; }
static struct agx_device *agx_device(void *s) { (void)s; return &device; }
static struct agx_bo *agx_bo_create(struct agx_device *d, size_t n, unsigned a, unsigned f, const char *label) {
   (void)d; (void)a; assert(f == AGX_BO_WRITEBACK && !strcmp(label, "Query") && n <= 256);
   if (created >= fail_after) return NULL;
   struct agx_bo *bo = calloc(1, sizeof(*bo));
   bo->va = &bo->storage; bo->storage.addr = next_va; next_va += 0x10000;
   bo->refs = 1; bo->size = n; ++created;
   return bo;
}
static void *agx_bo_map(struct agx_bo *bo) { return bo->map; }
static void agx_bo_reference(struct agx_bo *bo) { ++bo->refs; }
static void agx_bo_unreference(struct agx_device *d, struct agx_bo *bo) {
   (void)d; if (!bo) return; assert(bo->refs); if (!--bo->refs) { ++freed; free(bo); }
}
static bool is_query_busy(struct agx_context *ctx, struct agx_query *q) {
   for (unsigned i = 0; i < AGX_MAX_BATCHES; ++i)
      if (q->writer_generation[i] == ctx->batches.generation[i]) return true;
   return false;
}
/* Native single-query creation, as agx_win32_query_reset would call it. */
static struct pipe_query *agx_create_query(struct pipe_context *p, unsigned type, unsigned index) {
   struct agx_query *q = calloc(1, sizeof(*q));
   q->type = type; q->index = index;
   for (unsigned i = 0; i < AGX_MAX_BATCHES; ++i) q->writer_generation[i] = UINT64_MAX;
   q->bo = agx_bo_create(agx_device(p->screen), 16, 0, AGX_BO_WRITEBACK, "Query");
   q->ptr = (struct agx_ptr){q->bo->map, q->bo->va->addr};
   return (struct pipe_query *)q;
}
static void agx_win32_query_release_snapshot(struct agx_context *, struct agx_query *);
/* GPUVA agx_destroy_query: busy storage retires, idle storage is freed. */
static void agx_destroy_query(struct pipe_context *p, struct pipe_query *pq) {
   struct agx_context *ctx = agx_context(p); struct agx_query *q = (struct agx_query *)pq;
   if (is_query_busy(ctx, q)) { q->windows_next = ctx->windows_retired_queries; ctx->windows_retired_queries = q; return; }
   agx_win32_query_release_snapshot(ctx, q);
   agx_bo_unreference(&device, q->bo); free(q);
}
#include "agx_win32_query_lifecycle.h"
#include "agx_win32_query_group.h"
'''

GROUP_PROGRAM = LIFECYCLE + r'''
#define N 11
static struct pipe_query *group[N];
static struct agx_query *member(unsigned i) { return (struct agx_query *)group[i]; }
static void expect_shared(void) {
   struct agx_bo *bo = member(0)->bo;
   assert(bo && bo->refs == N && bo->size == 16 * N);
   for (unsigned i = 0; i < N; ++i) {
      struct agx_query *q = member(i);
      assert(q->bo == bo && q->type == PIPE_QUERY_PIPELINE_STATISTICS_SINGLE && q->index == i);
      assert(q->ptr.gpu == bo->va->addr + 16 * i && q->ptr.cpu == bo->map + 16 * i);
      assert(!is_query_busy(&context, q));
   }
}
static unsigned retired(void) { unsigned n = 0; for (struct agx_query *q = context.windows_retired_queries; q; q = q->windows_next) ++n; return n; }

int main(void) {
   struct pipe_context *pipe = &context.base;
   const unsigned type = PIPE_QUERY_PIPELINE_STATISTICS_SINGLE;
   /* Invalid requests change nothing. */
   assert(!AgxWin32AsahiQueryGroupBegin(pipe, type, group, 0));
   assert(!AgxWin32AsahiQueryGroupBegin(pipe, type, group, AGX_WIN32_QUERY_GROUP_MAX + 1));
   assert(!AgxWin32AsahiQueryGroupBegin(pipe, PIPE_QUERY_OCCLUSION_COUNTER, group, N));
   assert(!AgxWin32AsahiQueryGroupBegin(pipe, type, NULL, N));
   assert(!created && !group[0]);

   /* First Begin: one BO for the whole index. */
   assert(AgxWin32AsahiQueryGroupBegin(pipe, type, group, N) && created == 1);
   expect_shared();
   struct pipe_query *identity[N]; memcpy(identity, group, sizeof(group));
   struct agx_bo *first = member(0)->bo;

   /* Idle Begin: nothing allocated, nothing moved; the per-query reset of
    * agx_begin_query then allocates nothing either. */
   assert(AgxWin32AsahiQueryGroupBegin(pipe, type, group, N) && created == 1);
   for (unsigned i = 0; i < N; ++i) assert(agx_win32_query_reset(&context, member(i)));
   assert(created == 1 && member(0)->bo == first); expect_shared();

   /* One busy member (a resolve reader in batch 2) moves the whole group. */
   member(5)->writer_generation[2] = context.batches.generation[2];
   member(5)->ptr.cpu = first->map + 16 * 5; /* storage the GPU still reads */
   assert(AgxWin32AsahiQueryGroupBegin(pipe, type, group, N) && created == 2);
   assert(!memcmp(identity, group, sizeof(group)));
   expect_shared();
   assert(member(0)->bo != first && retired() == 1 && first->refs == 1 && !freed);
   assert(context.windows_retired_queries->bo == first && context.windows_retired_queries->index == 5);
   for (unsigned i = 0; i < N; ++i) assert(agx_win32_query_reset(&context, member(i)));
   assert(created == 2);
   /* The reader retires: the old BO is freed exactly once. */
   ++context.batches.generation[2];
   AgxWin32AsahiQueryCleanup(&context);
   assert(!retired() && freed == 1);

   /* Allocation failure leaves the queries and their BO untouched. */
   member(0)->writer_generation[1] = context.batches.generation[1];
   struct agx_bo *second = member(0)->bo;
   fail_after = created;
   assert(!AgxWin32AsahiQueryGroupBegin(pipe, type, group, N));
   assert(!memcmp(identity, group, sizeof(group)) && member(3)->bo == second && second->refs == N && !retired());
   fail_after = ~0u; ++context.batches.generation[1];

   /* Separate native BOs (a group created before failure handling or by the
    * per-query path) are regrouped; absent members are created. */
   struct pipe_query *loose[N] = {0};
   for (unsigned i = 0; i < N; ++i) if (i != 7) loose[i] = agx_create_query(pipe, type, i);
   unsigned before = created, freed_before = freed;
   memcpy(group, loose, sizeof(group));
   assert(AgxWin32AsahiQueryGroupBegin(pipe, type, group, N) && created == before + 1);
   expect_shared();
   assert(freed == freed_before + N - 1 && !retired());
   for (unsigned i = 0; i < N; ++i) if (i != 7) assert(group[i] == loose[i]);

   /* Destroying every member frees the shared BO once. */
   freed_before = freed;
   for (unsigned i = 0; i < N; ++i) agx_destroy_query(pipe, group[i]);
   assert(freed == freed_before + 1);
   /* The first group's identities still hold the second group's storage. */
   for (unsigned i = 0; i < N; ++i) agx_destroy_query(pipe, identity[i]);
   assert(freed == freed_before + 2 && created == freed);
   return 0;
}
'''

OWNER_PROGRAM = r'''
static std::vector<std::pair<int,unsigned>> calls; /* 0 group, 1 begin, 2 end */
static pipe_query **grouped; static unsigned group_count;
int AgxWin32AsahiQueryGroupBegin(pipe_context *pipe, unsigned type, pipe_query **group, unsigned count) {
   assert(type == PIPE_QUERY_PIPELINE_STATISTICS_SINGLE && count == 11);
   for (unsigned i = 0; i < count; ++i) if (!group[i]) group[i] = pipe->create_query(pipe, type, i);
   grouped = group; group_count = count; calls.push_back({0, 0}); return 1;
}
int main() {
   pipe_context pipe;
   pipe.begin_query = [](pipe_context *, pipe_query *q) { calls.push_back({1, q->index}); q->result = 0; return true; };
   pipe.end_query = [](pipe_context *, pipe_query *q) { calls.push_back({2, q->index}); return true; };
   AGX_D3D12_OWNER owner{&pipe, 0};
   AGX_D3D12_QUERY_HEAP *h = nullptr;
   assert(AgxD3d12OwnerCreateQueryHeap(&owner, 2, 3, &h) == S_OK);
   assert(AgxD3d12OwnerQuery(&owner, h, 3, 2, 1) == S_OK);
   assert(grouped == &h->Query[2 * 11] && calls.size() == 12 && calls[0].first == 0);
   for (unsigned s = 0; s < 11; ++s) assert(calls[1 + s].first == 1 && calls[1 + s].second == s);
   calls.clear(); grouped = nullptr;
   assert(AgxD3d12OwnerQuery(&owner, h, 3, 2, 0) == S_OK);
   assert(!grouped && calls.size() == 11 && calls[0].first == 2);
   AgxD3d12OwnerDestroyQueryHeap(&owner, h);
   /* Occlusion heaps never group. */
   calls.clear();
   assert(AgxD3d12OwnerCreateQueryHeap(&owner, 0, 1, &h) == S_OK);
   assert(AgxD3d12OwnerQuery(&owner, h, 0, 0, 1) == S_OK && !grouped && calls.size() == 1);
   AgxD3d12OwnerDestroyQueryHeap(&owner, h);
   return 0;
}
'''


def run(test, cmd, binary):
    r = subprocess.run(cmd, capture_output=True, text=True)
    test.assertEqual(r.returncode, 0, r.stderr)
    r = subprocess.run([str(binary)], capture_output=True, text=True)
    test.assertEqual(r.returncode, 0, r.stderr)


class QueryGroup(unittest.TestCase):
    def test_group_shares_one_bo_and_resets_as_a_unit(self):
        self.assertTrue((WINSYS / 'agx_win32_query_group.h').exists(), 'query group helper is missing')
        mesa = local_tree('reference', 'mesa')
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            (p / 'group.c').write_text(GROUP_PROGRAM)
            run(self, ['clang', '-std=c11', '-Wall', '-Werror', '-Wno-unused-function',
                       '-fsanitize=address,undefined', '-I' + str(WINSYS),
                       '-I' + str(mesa / 'src'), '-I' + str(mesa / 'include'),
                       '-I' + str(mesa / 'src/gallium/include'),
                       str(p / 'group.c'), '-o', str(p / 'group')], p / 'group')

    def test_owner_groups_statistics_before_begin_only(self):
        text = (WINSYS / 'agx_d3d12_windows.cpp').read_text()
        begin = text.index('struct AGX_D3D12_QUERY_HEAP {')
        end = text.index('/* C7c:', begin)
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            (p / 'owner.cpp').write_text('#define APPLE_AGX_GPUVA_WINSYS 1\n' + OWNER_PRELUDE +
                                         'int AgxWin32AsahiQueryGroupBegin(pipe_context *, unsigned, pipe_query **, unsigned);\n' +
                                         text[begin:end] + OWNER_PROGRAM)
            run(self, ['clang++', '-std=c++17', '-fsanitize=address,undefined',
                       str(p / 'owner.cpp'), '-o', str(p / 'owner')], p / 'owner')

    def test_projected_query_unit_includes_group_and_compiles(self):
        from test_dxbc_to_nir import host_flags
        mesa = local_tree('reference', 'mesa')
        build = local_tree('accelerated-desktop-ad03', 'mesa-build')
        with tempfile.TemporaryDirectory() as td:
            p = Path(td)
            out = project(p / 'native')
            unit = out / 'src/gallium/drivers/asahi/agx_query.c'
            text = unit.read_text()
            self.assertLess(text.index('#include "agx_win32_query_lifecycle.h"'),
                            text.index('#include "agx_win32_query_group.h"'))
            self.assertLess(text.index('#include "agx_win32_query_group.h"'),
                            text.index('\nagx_begin_query('))
            scene = (WINSYS / 'agx_win32_asahi_scene.h').read_text()
            externc = scene[scene.index('extern "C" {'):]
            self.assertIn('int AgxWin32AsahiQueryGroupBegin(struct pipe_context *, unsigned Type,', externc)
            device = out / 'src/asahi/lib/agx_device.h'
            device.write_text(device.read_text().replace(
                '#ifdef _WIN32\n   void *windows_private; /* Windows native allocation owner, never a GPU address */\n#endif',
                '   void *windows_private; /* Host compile platform shim. */'))
            (p / 'xf86drm.h').write_text('#pragma once\nint drmSyncobjImportSyncFile(int, unsigned, int);\n')
            includes = [p, out / 'src', out / 'include', out / 'include/drm-uapi',
                        out / 'src/asahi/lib', out / 'src/asahi/libagx', out / 'src/asahi/layout',
                        out / 'src/asahi/compiler', out / 'src/gallium/drivers/asahi',
                        build / 'src/asahi', build / 'src/asahi/genxml', build / 'src/asahi/libagx',
                        build / 'src/poly', build / 'src/compiler/nir',
                        mesa / 'src/gallium/auxiliary', mesa / 'src/gallium/include', mesa / 'src/virtio/vdrm',
                        WINSYS, ROOT / 'drivers/apple-agx/shared/include']
            r = subprocess.run(['clang', '-std=c11', '-DAPPLE_AGX_GPUVA_WINSYS', '-Wno-c23-extensions',
                                '-Werror=implicit-function-declaration',
                                '-include', str(p / 'xf86drm.h'),
                                *['-I' + str(d) for d in includes], *host_flags(build),
                                '-fsyntax-only', str(unit)], capture_output=True, text=True)
            self.assertEqual(r.returncode, 0, r.stderr)


if __name__ == '__main__':
    unittest.main()
