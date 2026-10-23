"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

EXP1909: after residency OOM latches the Windows backend failure, a texture
writeback must not pass a failed BO map (including NULL + mip/layer offset)
to ail_tile. It must report a context fault, release the transfer, and avoid
marking the level valid. Run the actual projected helper and unmap bodies.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_d3d10_shader_fields_init import function_body, project


HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>
enum { PIPE_MAP_WRITE = 1, PIPE_BUFFER = 2 };
struct pipe_resource { int target, refs; };
struct pipe_box { unsigned x, y, z, width, height, depth; };
struct pipe_transfer {
   struct pipe_resource *resource;
   unsigned level, usage, stride, layer_stride;
   struct pipe_box box;
};
struct agx_transfer {
   struct pipe_transfer base;
   struct { struct pipe_resource *rsrc; } staging;
   void *map;
};
struct ail_layout { unsigned offset; };
struct agx_resource {
   struct pipe_resource base;
   void *bo;
   struct ail_layout layout;
   unsigned data_valid;
};
struct pipe_context { unsigned faults; };
static unsigned tiled, released, blitted, fail_call, map_calls;
static uint8_t storage[256];
static void *agx_bo_map(void *bo) {
   ++map_calls;
   return map_calls == fail_call ? NULL : bo;
}
static unsigned ail_get_layer_level_B(const struct ail_layout *l, unsigned z, unsigned level) {
   return l->offset + z * 32 + level * 16;
}
static struct agx_transfer *agx_transfer(struct pipe_transfer *t) { return (void *)t; }
#define agx_context(p) (p)
static void AgxWin32AsahiMarkContextFault(struct pipe_context *p, unsigned site) {
   assert(site); ++p->faults;
}
static bool ail_is_level_twiddled_uncompressed(const struct ail_layout *l, unsigned level) {
   (void)l; (void)level; return true;
}
static void agx_blit_from_staging(struct pipe_context *p, struct agx_transfer *t) {
   (void)p; (void)t; ++blitted;
}
#define agx_resource(p) ((struct agx_resource *)(p))
static void agx_flush_readers(struct pipe_context *p, struct agx_resource *r, const char *s) {
   (void)p; (void)r; (void)s;
}
static void ail_tile(void *map, void *src, const struct ail_layout *l,
                     unsigned level, unsigned stride, unsigned x, unsigned y,
                     unsigned width, unsigned height) {
   (void)l; (void)level; (void)stride; (void)x; (void)y; (void)width; (void)height;
   /* A real tiler would fault on this input. Keep RED deterministic. */
   assert((uintptr_t)map >= (uintptr_t)storage &&
          (uintptr_t)map < (uintptr_t)(storage + sizeof(storage)));
   *(uint8_t *)map = *(uint8_t *)src; ++tiled;
}
static void pipe_resource_reference(struct pipe_resource **p, struct pipe_resource *v) {
   assert(!v); if (*p) --(*p)->refs; *p = NULL;
}
#define BITSET_SET(v, n) ((v) |= 1u << (n))
static void release_transfer(void *p) { ++released; free(p); }
#define FREE(p) release_transfer(p)
static void *agx_map_texture_cpu(struct agx_resource *rsrc, unsigned level, unsigned z)
@@HELPER@@
static void agx_transfer_unmap(struct pipe_context *pctx, struct pipe_transfer *transfer)
@@UNMAP@@
int main(void) {
   /* No map, then failure at a nonzero layer/mip offset, then a later-layer
    * failure; success must still tile every layer and mark the level valid. */
   for (unsigned scenario = 0; scenario < 4; ++scenario) {
      struct pipe_context ctx = {0};
      struct agx_resource r = {0};
      struct agx_transfer *t = calloc(1, sizeof(*t));
      r.base.refs = 1; r.bo = storage;
      t->base.resource = &r.base;
      t->base.usage = PIPE_MAP_WRITE;
      t->base.level = scenario ? 1 : 0;
      t->base.box.z = scenario ? 1 : 0;
      t->base.box.depth = 2;
      t->base.layer_stride = 1;
      t->map = malloc(2); memset(t->map, 0x7b, 2);
      map_calls = tiled = released = blitted = 0;
      fail_call = scenario < 2 ? 1 : scenario == 2 ? 2 : 0;
      agx_transfer_unmap(&ctx, &t->base);
      assert(released == 1 && r.base.refs == 0 && blitted == 0);
      if (fail_call) {
         assert(ctx.faults == 1 && r.data_valid == 0);
         assert(tiled == (scenario == 2 ? 1 : 0));
      } else {
         assert(ctx.faults == 0 && tiled == 2 && r.data_valid == 2);
         assert(storage[48] == 0x7b && storage[80] == 0x7b);
      }
   }
}
'''


class TextureMapFailure(unittest.TestCase):
    def test_failed_map_never_reaches_tiler_and_cleans_up(self):
        with tempfile.TemporaryDirectory(prefix="c75-map-") as tmp:
            out = project(Path(tmp) / "out")
            if out is None:
                self.skipTest("reference Mesa tree not available")
            asahi = out / "src/gallium/drivers/asahi"
            helper = function_body((asahi / "agx_state.h").read_text(), "agx_map_texture_cpu")
            unmap = function_body((asahi / "agx_pipe.c").read_text(), "agx_transfer_unmap")
            src, exe = Path(tmp) / "test.c", Path(tmp) / "test"
            src.write_text(HARNESS.replace("@@HELPER@@", helper).replace("@@UNMAP@@", unmap))
            built = subprocess.run(["clang", "-std=c11", "-Wall", "-Wextra",
                                    "-Wno-unused-function", "-fsanitize=address,undefined",
                                    str(src), "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            run = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(run.returncode, 0, run.stdout + run.stderr)


if __name__ == "__main__":
    unittest.main()
