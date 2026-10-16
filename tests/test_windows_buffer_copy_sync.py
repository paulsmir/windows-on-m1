"""EXP1312: the Windows CPU buffer copy waits for the GPU like a CPU transfer.

Invariant caught: AgxWin32AsahiBufferCopy (the D3D ResourceCopy path for
buffers) memcpy'd from the source BO without syncing its GPU writer, and
into the destination without syncing its users. EXP1312: CopyStructureCount
wrote 86 into a buffer on a compute batch (the destination read back 86
through a synced map), yet CopyResource(staging, count) copied the stale
CPU contents (256 / 0). Asahi's agx_prepare_for_map syncs the writer before
any CPU access and the readers before a CPU write; the copy must do both.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_asahi_scene.c"

PROGRAM = r'''
#include <assert.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>
typedef unsigned long long APPLE_AGX_U64;
enum pipe_texture_target { PIPE_BUFFER = 0, PIPE_TEXTURE_2D = 1 };
struct pipe_screen { int unused; };
struct pipe_context { struct pipe_screen *screen; };
struct pipe_resource { struct pipe_screen *screen; enum pipe_texture_target target; unsigned width0; };
struct util_range { unsigned start, end; };
struct agx_bo { unsigned char cpu[64]; int pending; unsigned char gpu[64]; };
struct agx_resource { struct pipe_resource base; struct agx_bo *bo; struct util_range valid_buffer_range;
                      unsigned data_valid[1]; };
struct agx_context { struct pipe_context base; };
#define BITSET_SET(set, bit) ((set)[(bit) / 32] |= 1u << ((bit) % 32))
static struct agx_resource *agx_resource(struct pipe_resource *p) { return (struct agx_resource *)p; }
static struct agx_context *agx_context(struct pipe_context *p) { return (struct agx_context *)p; }
static void *agx_bo_map(struct agx_bo *bo) { return bo->cpu; }
static void util_range_add(struct pipe_resource *r, struct util_range *range, unsigned s, unsigned e) {
  (void)r; if (s < range->start) range->start = s; if (e > range->end) range->end = e;
}
static unsigned synced_writers, synced_readers;
/* A pending GPU write reaches the CPU copy only when its batch is synced. */
static void settle(struct agx_resource *r) {
  if (r->bo->pending) { memcpy(r->bo->cpu, r->bo->gpu, sizeof(r->bo->cpu)); r->bo->pending = 0; }
}
static void agx_sync_writer(struct agx_context *c, struct agx_resource *r, const char *why) {
  (void)c; (void)why; ++synced_writers; settle(r);
}
static void agx_sync_readers(struct agx_context *c, struct agx_resource *r, const char *why) {
  (void)c; (void)why; ++synced_readers; settle(r);
}
@@FUNCTION@@
int main(void) {
  struct pipe_screen screen = {0};
  struct agx_context ctx = {{&screen}};
  struct agx_bo src_bo = {{0}, 0, {0}}, dst_bo = {{0}, 0, {0}};
  struct agx_resource src = {{&screen, PIPE_BUFFER, 16}, &src_bo, {0, 16}, {0}};
  struct agx_resource dst = {{&screen, PIPE_BUFFER, 16}, &dst_bo, {0, 0}, {0}};
  unsigned value = 256u, counted = 86u, got = 0;
  memcpy(src_bo.cpu, &value, 4);      /* stale CPU copy */
  memcpy(src_bo.gpu, &counted, 4);    /* the compute copy's result */
  src_bo.pending = 1;
  assert(AgxWin32AsahiBufferCopy(&ctx.base, &dst.base, 0, &src.base, 0, 4));
  memcpy(&got, dst_bo.cpu, 4);
  assert(got == 86u);
  assert(synced_writers >= 1 && synced_readers >= 1);
  /* Its own preconditions are unchanged. */
  assert(!AgxWin32AsahiBufferCopy(&ctx.base, &src.base, 0, &src.base, 0, 4));
  assert(!AgxWin32AsahiBufferCopy(&ctx.base, &dst.base, 0, &src.base, 0, 17));
  return 0;
}
'''


class WindowsBufferCopySync(unittest.TestCase):
    def test_copy_syncs_the_gpu_first(self):
        text = SRC.read_text()
        function = re.search(r"int AgxWin32AsahiBufferCopy\(.*?\n\}", text, re.S).group(0)
        program = PROGRAM.replace("@@FUNCTION@@", function)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / "copy.c"; exe = Path(tmp) / "copy"
            src.write_text(program)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-function", "-fsanitize=address,undefined", str(src), "-o", str(exe)],
                           check=True)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
