"""EXP1199: a compressed texture is decompressed on its first CPU update only.

EXP1198 (steady state, CS 1.6): 1.2 "GPU write staging blit" flushes per
frame -- every lightmap re-upload into a compressed texture ran a u_blitter
pass in its own batch, submitted at once. The GPUVA profile now decompresses
a compressed texture when the CPU writes a level that already holds data, so
later updates tile on the CPU. Invariants of the generated decision
(native-asahi-batch-lifecycle.py, agx_transfer_map): an update (write to a
valid level) of a compressed texture decompresses it, once; an initial
upload (level not yet valid), a read, a buffer, an uncompressed texture and
shared/scanout/depth-stencil resources are never decompressed; the D3D10
profile is unchanged.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py"

PROGRAM = r'''
#include <assert.h>
#include <stdbool.h>
#define APPLE_AGX_GPUVA_WINSYS 1
enum { PIPE_MAP_READ = 1, PIPE_MAP_WRITE = 2 };
enum { PIPE_BUFFER = 0, PIPE_TEXTURE_2D = 2 };
enum { PIPE_BIND_DEPTH_STENCIL = 1, PIPE_BIND_RENDER_TARGET = 2, PIPE_BIND_SAMPLER_VIEW = 8,
       PIPE_BIND_SHARED = 16, PIPE_BIND_SCANOUT = 32 };
struct pipe_resource { int target; unsigned bind; };
struct ail_layout { bool compressed; };
struct agx_resource { struct pipe_resource base; struct ail_layout layout; unsigned valid; };
struct agx_context { int unused; };
static unsigned decompressions;
static bool ail_is_level_logically_compressed(const struct ail_layout *l, unsigned level) { (void)level; return l->compressed; }
static bool agx_resource_valid(struct agx_resource *r, unsigned level) { return r->valid & (1u << level); }
static void agx_decompress(struct agx_context *ctx, struct agx_resource *r, const char *why) {
  (void)ctx; (void)why; r->layout.compressed = false; ++decompressions;
}
static void map(struct agx_context *ctx, struct agx_resource *rsrc, unsigned level, unsigned usage) {
  struct pipe_resource *resource = &rsrc->base;
@@DECISION@@
}
int main(void) {
  struct agx_context ctx = {0};
  struct agx_resource lightmap = {{PIPE_TEXTURE_2D, PIPE_BIND_SAMPLER_VIEW | PIPE_BIND_RENDER_TARGET}, {true}, 0};
  map(&ctx, &lightmap, 0, PIPE_MAP_WRITE);             /* initial upload */
  assert(decompressions == 0 && lightmap.layout.compressed);
  lightmap.valid = 1;
  map(&ctx, &lightmap, 0, PIPE_MAP_READ);              /* read back */
  assert(decompressions == 0);
  map(&ctx, &lightmap, 0, PIPE_MAP_WRITE);             /* update */
  assert(decompressions == 1 && !lightmap.layout.compressed);
  map(&ctx, &lightmap, 0, PIPE_MAP_WRITE);             /* later updates */
  assert(decompressions == 1);
  struct agx_resource mip = {{PIPE_TEXTURE_2D, PIPE_BIND_SAMPLER_VIEW}, {true}, 1};
  map(&ctx, &mip, 1, PIPE_MAP_WRITE);                  /* first upload of level 1 */
  assert(decompressions == 1 && mip.layout.compressed);
  struct agx_resource buffer = {{PIPE_BUFFER, 0}, {true}, 1};
  map(&ctx, &buffer, 0, PIPE_MAP_WRITE);
  assert(decompressions == 1);
  unsigned keep[3] = {PIPE_BIND_SHARED, PIPE_BIND_SCANOUT, PIPE_BIND_DEPTH_STENCIL};
  for (unsigned i = 0; i < 3; ++i) {
    struct agx_resource r = {{PIPE_TEXTURE_2D, PIPE_BIND_SAMPLER_VIEW | keep[i]}, {true}, 1};
    map(&ctx, &r, 0, PIPE_MAP_WRITE);
    assert(decompressions == 1 && r.layout.compressed);
  }
  return 0;
}
'''


class CpuTextureUpdateTests(unittest.TestCase):
    def test_compressed_texture_decompresses_on_first_update_only(self):
        source = GENERATOR.read_text()
        match = re.search(r"(#ifdef APPLE_AGX_GPUVA_WINSYS\n   /\* EXP1199: decompress.*?#endif\n)", source, re.S)
        self.assertIsNotNone(match, "EXP1199 decision not found")
        program = PROGRAM.replace("@@DECISION@@", match.group(1))
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "update.c"
            exe = Path(directory) / "update"
            path.write_text(program)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror", "-Wno-unused-function",
                            "-fsanitize=address,undefined", str(path), "-o", str(exe)], check=True, cwd=ROOT)
            ran = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
