"""EXP1071: a new batch leaves other active batches open, as Linux Mesa does.

EXP1070 measure-perf-reason: 6338 of DWM's 9636 synchronous submissions
(66 %) came from AgxWin32AsahiBatchBegin flushing every other active batch
when a new framebuffer was bound ("Begin drain: flush active"). Asahi keeps
up to AGX_MAX_BATCHES open and flushes one only for a reason (read/write
hazard, CPU access, Gallium flush); returning to a still-open framebuffer
appends to it instead of starting another pass. Invariants:
- Begin does not flush another active batch;
- Begin still retires completed submitted batches, so the residency set is
  free for the new batch;
- a held set owned by no batch of this context is still refused;
- EXP1071 hardware: a retired submission returns its private scene lease at
  once, so open batches flushed back to back hold at most the leases of the
  submissions still in flight (holding each until cleanup exhausted the
  process range: D3DERR_OUTOFVIDEOMEMORY).
- EXP1082: a submission still running on the GPU (its fence not signalled)
  is neither waited for nor retired by Begin; it stays submitted and holding
  while the next batch is built.
- EXP1093: two submissions may be in flight (EXP1090: 26 % of DWM's wall
  time was the wait for the previous submission before each one). A
  submission waits only for the older of the two, in GPU (FIFO) order; a
  third is never submitted while two are in flight; retiring the newest
  set (a CPU map, native_map) retires the older one first; the older one
  alone can be retired, leaving the newest in flight.
The space functions are the real winsys ones (agx_win32_gpuva.c).
"""
from pathlib import Path
import os
import subprocess
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parent))
from g3_vidmm_replay import body  # noqa: E402

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva_batch.c"
SPACE = ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva.c"

PROGRAM = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "agx_win32_gpuva.h"
#define AGX_MAX_BATCHES 4
#define BITSET_TEST(set, i) ((((set)[(i) / 32u]) >> ((i) % 32u)) & 1u)
#define BITSET_CLEAR(set, i) ((set)[(i) / 32u] &= ~(1u << ((i) % 32u)))
#define BITSET_SET(set, i) ((set)[(i) / 32u] |= (1u << ((i) % 32u)))
typedef unsigned APPLE_AGX_U32;
static unsigned live_leases, evicts, waits;
static uint64_t completed_fence = ~0ull;
static int private_escape(void *context, APPLE_AGX_G3_PRIVATE_REQUEST *r) {
  (void)context; if (r->Operation != APPLE_AGX_G3_PRIVATE_RELEASE || !live_leases) return 0;
  --live_leases; return 1;
}
/* The GPU completes in order: waiting for a fence completes every earlier one. */
static int wait_render(void *context, uint64_t fence) {
  (void)context; ++waits; if (completed_fence < fence) completed_fence = fence; return 1;
}
static int query_render(void *context, uint64_t fence) { (void)context; return fence <= completed_fence; }
static int evict(void *context, const uint64_t *handles, unsigned count) {
  (void)context; (void)handles; (void)count; ++evicts; return 1;
}
typedef struct {
  AGX_WIN32_GPUVA_SPACE Gpuva;
  int GpuvaReady, Failed;
} AGX_WIN32_ASAHI_BACKEND;
struct agx_bo { int unused; };
struct agx_device { void *windows_private; };
struct pipe_screen { struct agx_device dev; };
struct agx_batch;
struct agx_context {
  struct { struct pipe_screen *screen; } base;
  int any_faults;
  struct { struct agx_batch *slots; unsigned active[1], submitted[1]; } batches;
};
struct agx_batch {
  struct agx_context *ctx;
  void *windows_batch;
  struct { struct agx_bo *bo; } vdm;
};
typedef struct {
  struct agx_bo *Command;
  APPLE_AGX_G4_PRIVATE_LEASE Lease;
  uint64_t Fence;
  unsigned Entered, Submitted, Retired, Rejected;
} AGX_G4_BATCH;
static struct agx_device *agx_device(struct pipe_screen *s) { return &s->dev; }
static void AgxWin32AsahiPublishPages(struct agx_device *d) { (void)d; }
static void AgxWin32PerfNote(const char *fmt, ...) { (void)fmt; }
void (*AgxWin32BatchRefusalHook)(unsigned, unsigned, unsigned, unsigned);
static unsigned flushes;
static void agx_flush_batch(struct agx_context *ctx, struct agx_batch *batch) {
  (void)ctx; (void)batch; ++flushes;
}
static void agx_sync_batch(struct agx_context *ctx, struct agx_batch *batch) {
  unsigned i = (unsigned)(batch - ctx->batches.slots);
  free(batch->windows_batch); batch->windows_batch = NULL;
  BITSET_CLEAR(ctx->batches.submitted, i);
}
@@SPACE@@
@@FUNCTIONS@@
/* Mirrors AgxWin32AsahiBatchFinish + AgxWin32GpuvaSubmit: room is made by
 * retiring the older in-flight submission; the space refuses a third. */
static int submit(AGX_WIN32_ASAHI_BACKEND *b, struct agx_batch *batch, uint64_t fence) {
  if (!retire_held(batch, b) || b->Gpuva.Older.Handles || b->Gpuva.Terminal) return 0;
  capsule(batch)->Lease = (APPLE_AGX_G4_PRIVATE_LEASE){1, 1, fence, 1}; ++live_leases;
  if (b->Gpuva.Held) {  /* gpuva_shift */
    b->Gpuva.Older.Handles = b->Gpuva.Held; b->Gpuva.Older.Count = b->Gpuva.HeldCount;
    b->Gpuva.Older.Fence = b->Gpuva.RenderFence;
  }
  b->Gpuva.Held = calloc(1, sizeof(uint64_t)); b->Gpuva.HeldCount = 1; b->Gpuva.RenderFence = fence;
  AGX_G4_BATCH *g = capsule(batch); g->Submitted = 1; g->Fence = fence;
  unsigned i = (unsigned)(batch - batch->ctx->batches.slots);
  BITSET_CLEAR(batch->ctx->batches.active, i); BITSET_SET(batch->ctx->batches.submitted, i);
  return 1;
}
static int open_batch(struct agx_context *ctx, unsigned i) {
  BITSET_SET(ctx->batches.active, i); return AgxWin32AsahiBatchBegin(&ctx->batches.slots[i]);
}
int main(void) {
  AGX_WIN32_ASAHI_BACKEND backend; memset(&backend, 0, sizeof(backend));
  backend.GpuvaReady = 1;
  static int owner;
  backend.Gpuva.Context = &owner;
  backend.Gpuva.Ops.PrivateEscape = private_escape;
  backend.Gpuva.Ops.WaitRender = wait_render;
  backend.Gpuva.Ops.QueryRender = query_render;
  backend.Gpuva.Ops.Evict = evict;
  struct pipe_screen screen = {{&backend}};
  struct agx_batch slots[AGX_MAX_BATCHES];
  struct agx_bo vdm;
  struct agx_context ctx = {{&screen}, 0, {slots, {0}, {0}}};
  for (unsigned i = 0; i < AGX_MAX_BATCHES; ++i) slots[i] = (struct agx_batch){&ctx, NULL, {&vdm}};
  /* A opens, then B opens while A is still active: A is not flushed. */
  assert(open_batch(&ctx, 0) && open_batch(&ctx, 1));
  assert(flushes == 0 && slots[0].windows_batch && slots[1].windows_batch);
  /* agx_flush_all: A then B back to back, the GPU still running A: B is
   * submitted without waiting for A; both are in flight, A the older. */
  completed_fence = 9;
  assert(submit(&backend, &slots[0], 10) && submit(&backend, &slots[1], 11));
  assert(evicts == 0 && waits == 0 && live_leases == 2 && !capsule(&slots[0])->Retired);
  assert(backend.Gpuva.Older.Fence == 10 && backend.Gpuva.RenderFence == 11);
  /* C opens: both still running, both kept. */
  assert(open_batch(&ctx, 2) && evicts == 0 && slots[0].windows_batch && slots[1].windows_batch);
  /* C's submission waits for A only, in GPU order; B stays in flight. */
  assert(submit(&backend, &slots[2], 12));
  assert(evicts == 1 && waits == 1 && capsule(&slots[0])->Retired && !capsule(&slots[1])->Retired);
  assert(live_leases == 2 && backend.Gpuva.Older.Fence == 11 && backend.Gpuva.RenderFence == 12);
  /* Everything completes: the next Begin retires B then C and cleans up A, B, C. */
  completed_fence = 12;
  assert(open_batch(&ctx, 3));
  assert(evicts == 3 && !backend.Gpuva.Held && !backend.Gpuva.Older.Handles && live_leases == 0);
  assert(!slots[0].windows_batch && !slots[1].windows_batch && !slots[2].windows_batch);
  /* A held set that no batch of this context owns is still refused. */
  uint64_t foreign = 5;
  backend.Gpuva.Held = &foreign; backend.Gpuva.HeldCount = 1; backend.Gpuva.RenderFence = 99;
  assert(!submit(&backend, &slots[3], 13));
  free(slots[3].windows_batch); slots[3].windows_batch = NULL; BITSET_CLEAR(ctx.batches.active, 3);
  assert(!open_batch(&ctx, 3));
  backend.Gpuva.Held = NULL; backend.Gpuva.HeldCount = 0; backend.Gpuva.RenderFence = 0;
  /* D and E submitted, the GPU on neither. */
  completed_fence = 19;
  assert(open_batch(&ctx, 3) && submit(&backend, &slots[3], 20));
  assert(open_batch(&ctx, 0) && submit(&backend, &slots[0], 21));
  unsigned before = evicts, waited = waits;
  /* A CPU map of a BO the newest set names (native_map) retires it: the
   * older set is retired first, then the newest; neither batch is marked
   * retired, and a later Begin waits only for their fences. */
  assert(AgxWin32GpuvaFenceHeld(&backend.Gpuva, 20) && AgxWin32GpuvaFenceHeld(&backend.Gpuva, 21));
  assert(AgxWin32GpuvaRetire(&backend.Gpuva, 21));
  assert(evicts == before + 2 && !backend.Gpuva.Held && !backend.Gpuva.Older.Handles);
  assert(!AgxWin32GpuvaFenceHeld(&backend.Gpuva, 20) && !capsule(&slots[3])->Retired);
  assert(open_batch(&ctx, 1) && !slots[3].windows_batch && !slots[0].windows_batch);
  assert(evicts == before + 2 && waits == waited + 4 && live_leases == 0);
  /* F and G in flight; retiring the older set alone keeps G in flight. */
  completed_fence = 29;
  assert(submit(&backend, &slots[1], 30));
  assert(open_batch(&ctx, 2) && submit(&backend, &slots[2], 31));
  assert(AgxWin32GpuvaRetire(&backend.Gpuva, 30));
  assert(backend.Gpuva.Held && backend.Gpuva.RenderFence == 31 && !backend.Gpuva.Older.Handles);
  /* A fence that no in-flight set has is refused. */
  assert(!AgxWin32GpuvaRetire(&backend.Gpuva, 30) && !AgxWin32GpuvaRetire(&backend.Gpuva, 32));
  completed_fence = 31;
  assert(open_batch(&ctx, 3) && !slots[1].windows_batch && !slots[2].windows_batch);
  assert(!backend.Gpuva.Held && live_leases == 0 && flushes == 0);
  free(slots[3].windows_batch);
  puts("PASS");
  return 0;
}
'''


class BatchBeginKeepsActive(unittest.TestCase):
    def test_new_batch_keeps_other_active_batches_open(self):
        source = SRC.read_text()
        space = SPACE.read_text()
        functions = "\n".join(body(source, name) for name in (
            "backend", "capsule", "batch_refuse", "release_lease", "AgxWin32AsahiBatchPoll",
            "submitted_at", "held_by", "retire_held", "AgxWin32AsahiBatchBegin"))
        space_functions = "\n".join(body(space, name) for name in (
            "AgxWin32GpuvaRetire", "AgxWin32GpuvaComplete", "AgxWin32GpuvaFenceHeld"))
        program = PROGRAM.replace("@@SPACE@@", space_functions).replace("@@FUNCTIONS@@", functions)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "begin.c"
            exe = Path(directory) / "begin"
            path.write_text(program)
            built = subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                                    "-Werror", "-Wno-unused-function", "-fsanitize=address,undefined",
                                    "-I", str(ROOT / "drivers/apple-agx/mesa/winsys"),
                                    "-I", str(ROOT / "drivers/apple-agx/shared/include"),
                                    str(path), "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_finish_retires_the_completed_holder_before_its_lease(self):
        source = SRC.read_text()
        finish = body(source, "AgxWin32AsahiBatchFinish")
        self.assertLess(finish.index("retire_held(batch,b)"), finish.index("prepare_process_buffers("))

    def test_space_bounds_the_depth_at_two(self):
        space = SPACE.read_text()
        submit = body(space, "AgxWin32GpuvaSubmit")
        # A third submission is refused while two are in flight; the newest
        # set becomes the older one on every path that installs a new Held.
        self.assertIn("space->Terminal || space->Older.Handles) return gpuva_refuse", submit)
        self.assertEqual(submit.count("gpuva_shift(space);"), submit.count("space->Held = handles;"))
        self.assertIn("space->Older.Handles ||", body(space, "AgxWin32GpuvaUnbind"))


if __name__ == "__main__":
    unittest.main()
