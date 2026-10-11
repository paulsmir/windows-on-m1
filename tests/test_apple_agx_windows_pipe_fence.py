"""EXP1195: pipe fences of the GPUVA profile never wait at creation.

EXP1146 implemented a flush that asks for a fence as agx_sync_all plus a
fence that is signalled by construction, so every fence request (glFinish,
glFenceSync, stw's end-of-frame wait, the ICD present ring) blocked until
the GPU had finished everything, and a zero-timeout fence_finish could never
report "not yet". Invariants of the generated GPUVA-profile code
(native-asahi-batch-lifecycle.py): a flush with a fence submits (flush_all)
and never syncs; the fence holds the newest accepted submission's completion
fence; fence_finish with timeout 0 only polls; an infinite finish waits once
through the backend; a finite one polls until its deadline; a faulted
context gets no fence; references count (no double free under ASan); the
D3D10 profile keeps the EXP1146 behaviour.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
GENERATOR = ROOT / "drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py"

PRELUDE = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdlib.h>
#define APPLE_AGX_GPUVA_WINSYS 1
#define OS_TIMEOUT_INFINITE 0xffffffffffffffffull
#define MIN2(a, b) ((a) < (b) ? (a) : (b))
struct pipe_reference { int count; };
static void pipe_reference_init(struct pipe_reference *r, int n) { r->count = n; }
static bool pipe_reference(struct pipe_reference *dst, struct pipe_reference *src) {
  if (dst == src) return false;
  if (src) ++src->count;
  return dst && --dst->count == 0;
}
struct pipe_fence_handle;
struct pipe_screen { int unused; };
struct pipe_context { struct pipe_screen *screen; };
struct agx_device { uint64_t signalled; unsigned waits; int wait_result; };
struct agx_context { struct pipe_context base; bool any_faults; uint64_t newest; unsigned flushes, syncs; };
static struct agx_device the_device;
static struct agx_device *agx_device(struct pipe_screen *s) { (void)s; return &the_device; }
static struct agx_context *agx_context(struct pipe_context *p) { return (struct agx_context *)p; }
static void agx_flush_all(struct agx_context *ctx, const char *why) { (void)why; ++ctx->flushes; }
static void agx_sync_all(struct agx_context *ctx, const char *why) { (void)why; ++ctx->syncs; }
static uint64_t AgxWin32AsahiNewestFence(struct agx_context *ctx) { return ctx->newest; }
static int AgxWin32AsahiFenceSignalled(struct agx_device *d, uint64_t f) { return !f || d->signalled >= f; }
static int AgxWin32AsahiFenceWait(struct agx_device *d, uint64_t f) {
  ++d->waits; if (d->wait_result) d->signalled = f; return d->wait_result;
}
static int64_t now_ns;
static unsigned sleeps;
static int64_t os_time_get_nano(void) { return now_ns; }
static void os_time_sleep(int64_t us) { ++sleeps; now_ns += us * 1000; }
'''

MAIN = r'''
int main(void) {
  struct pipe_screen screen = {0};
  struct agx_context ctx = {0}; ctx.base.screen = &screen;
  struct pipe_fence_handle *fence = NULL, *copy = NULL;
  /* a plain flush submits, creates nothing */
  agx_flush(&ctx.base, NULL, 0);
  assert(ctx.flushes == 1 && ctx.syncs == 0);
  /* a fence request submits and does not wait */
  ctx.newest = 5; the_device.signalled = 3;
  agx_flush(&ctx.base, &fence, 0);
  assert(fence && ctx.flushes == 2 && ctx.syncs == 0 && the_device.waits == 0);
  assert(!agx_windows_fence_finish(&screen, NULL, fence, 0) && the_device.waits == 0);
  the_device.signalled = 5;
  assert(agx_windows_fence_finish(&screen, NULL, fence, 0));
  /* infinite: one backend wait */
  ctx.newest = 9; the_device.wait_result = 1;
  agx_flush(&ctx.base, &fence, 0);   /* replaces (releases) the old fence */
  assert(agx_windows_fence_finish(&screen, NULL, fence, OS_TIMEOUT_INFINITE) && the_device.waits == 1);
  /* finite: polls until its deadline, no backend wait */
  ctx.newest = 12; the_device.wait_result = 0;
  agx_flush(&ctx.base, &fence, 0);
  assert(!agx_windows_fence_finish(&screen, NULL, fence, 1000000) && the_device.waits == 1 && sleeps > 0);
  /* references */
  agx_windows_fence_reference(&screen, &copy, fence);
  agx_windows_fence_reference(&screen, &fence, NULL);
  assert(!fence && copy);
  assert(!agx_windows_fence_finish(&screen, NULL, copy, 0));
  agx_windows_fence_reference(&screen, &copy, NULL);
  /* nothing submitted yet: signalled */
  ctx.newest = 0;
  agx_flush(&ctx.base, &fence, 0);
  assert(agx_windows_fence_finish(&screen, NULL, fence, 0));
  agx_windows_fence_reference(&screen, &fence, NULL);
  /* a faulted context gets no fence but still flushes */
  ctx.any_faults = true; ctx.newest = 20;
  unsigned before = ctx.flushes;
  agx_flush(&ctx.base, &fence, 0);
  assert(!fence && ctx.flushes == before + 1 && ctx.syncs == 0);
  return 0;
}
'''


class WindowsPipeFenceTests(unittest.TestCase):
    def test_gpuva_fences_do_not_wait_at_creation(self):
        source = GENERATOR.read_text()
        fence = re.search(r"'''(#ifdef APPLE_AGX_GPUVA_WINSYS\n#include \"util/os_time.h\"\n.*?)#else\n", source, re.S)
        self.assertIsNotNone(fence, "GPUVA fence code not found")
        body = re.search(r"s=body\(s,'agx_flush','''(.*?)'''\)", source, re.S)
        self.assertIsNotNone(body, "agx_flush body not found")
        code = fence.group(1).replace('#include "util/os_time.h"\n', '')
        code = code.replace('#ifdef APPLE_AGX_GPUVA_WINSYS\n', '', 1)
        flush = ("static void\nagx_flush(struct pipe_context *pctx, struct pipe_fence_handle **fence, unsigned flags)\n{\n"
                 + body.group(1) + "\n}\n")
        program = PRELUDE + code + flush + MAIN
        # The D3D10 profile keeps EXP1146.
        self.assertIn('agx_sync_all(ctx, "Windows fence");', body.group(1).split('#else')[1])
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "fence.c"
            exe = Path(directory) / "fence"
            path.write_text(program)
            subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                            "-Wno-unused-function", "-fsanitize=address,undefined", str(path), "-o", str(exe)],
                           check=True, cwd=ROOT)
            ran = subprocess.run([str(exe)], cwd=ROOT, capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)


if __name__ == "__main__":
    unittest.main()
