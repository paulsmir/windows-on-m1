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
- EXP1161 (WGL): a held set owned by no batch of this context belongs to
  another GL context of the same screen (one G4 context and fence
  timeline per screen); it is not refused -- making room retires it
  through the space, as a CPU map does, and its batch retires by fence;
- EXP1071 hardware: a retired submission returns its private scene lease at
  once, so open batches flushed back to back hold at most the leases of the
  submissions still in flight (holding each until cleanup exhausted the
  process range: D3DERR_OUTOFVIDEOMEMORY).
- EXP1082: a submission still running on the GPU (its fence not signalled)
  is neither waited for nor retired by Begin; it stays submitted and holding
  while the next batch is built.
- EXP1093/EXP1203: up to AGX_GPUVA_IN_FLIGHT submissions may be in flight
  (EXP1090: 26 % of DWM's wall time was the wait for the previous
  submission before each one; EXP1203: CS 1.6 submits three per frame). A
  submission waits only when all are in flight, and then only for the
  oldest, in GPU (FIFO) order; retiring the newest set (a CPU map,
  native_map) retires the older ones first; an older one alone can be
  retired, leaving the newer ones in flight.
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
#define AGX_MAX_BATCHES 8
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
 * retiring the oldest in-flight submission; the space refuses a submission
 * without a free older slot. */
#define DEPTH AGX_GPUVA_IN_FLIGHT
static int submit(AGX_WIN32_ASAHI_BACKEND *b, struct agx_batch *batch, uint64_t fence) {
  if (!retire_held(batch, b) || b->Gpuva.OlderCount == DEPTH - 1u || b->Gpuva.Terminal) return 0;
  capsule(batch)->Lease = (APPLE_AGX_G4_PRIVATE_LEASE){1, 1, fence, 1}; ++live_leases;
  if (b->Gpuva.Held) {  /* gpuva_shift */
    AGX_WIN32_GPUVA_SET *o = &b->Gpuva.Older[b->Gpuva.OlderCount++];
    o->Handles = b->Gpuva.Held; o->Count = b->Gpuva.HeldCount; o->Fence = b->Gpuva.RenderFence;
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
  /* agx_flush_all: batches submitted back to back while the GPU runs none
   * of them: DEPTH submissions stay in flight without a wait, oldest first. */
  completed_fence = 9;
  for (unsigned i = 0; i < DEPTH; ++i) {
    if (i >= 2u) assert(open_batch(&ctx, i));
    assert(submit(&backend, &slots[i], 10u + i));
  }
  assert(evicts == 0 && waits == 0 && live_leases == DEPTH && !capsule(&slots[0])->Retired);
  assert(backend.Gpuva.OlderCount == DEPTH - 1u && backend.Gpuva.Older[0].Fence == 10 &&
         backend.Gpuva.RenderFence == 10u + DEPTH - 1u);
  /* Another opens: all still running, all kept. */
  assert(open_batch(&ctx, DEPTH) && evicts == 0 && slots[0].windows_batch && slots[1].windows_batch);
  /* Its submission waits for the oldest only, in GPU order. */
  assert(submit(&backend, &slots[DEPTH], 10u + DEPTH));
  assert(evicts == 1 && waits == 1 && capsule(&slots[0])->Retired && !capsule(&slots[1])->Retired);
  assert(live_leases == DEPTH && backend.Gpuva.Older[0].Fence == 11 &&
         backend.Gpuva.RenderFence == 10u + DEPTH);
  /* Everything completes: the next Begin retires and cleans up every one. */
  completed_fence = 10u + DEPTH;
  assert(open_batch(&ctx, DEPTH + 1u));
  assert(evicts == DEPTH + 1u && !backend.Gpuva.Held && !backend.Gpuva.OlderCount && live_leases == 0);
  for (unsigned i = 0; i <= DEPTH; ++i) assert(!slots[i].windows_batch);
  free(slots[DEPTH + 1u].windows_batch); slots[DEPTH + 1u].windows_batch = NULL;
  BITSET_CLEAR(ctx.batches.active, DEPTH + 1u);
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
  assert(evicts == before + 2 && !backend.Gpuva.Held && !backend.Gpuva.OlderCount);
  assert(!AgxWin32GpuvaFenceHeld(&backend.Gpuva, 20) && !capsule(&slots[3])->Retired);
  assert(open_batch(&ctx, 1) && !slots[3].windows_batch && !slots[0].windows_batch);
  assert(evicts == before + 2 && waits == waited + 4 && live_leases == 0);
  /* F and G in flight; retiring the older set alone keeps G in flight. */
  completed_fence = 29;
  assert(submit(&backend, &slots[1], 30));
  assert(open_batch(&ctx, 2) && submit(&backend, &slots[2], 31));
  assert(AgxWin32GpuvaRetire(&backend.Gpuva, 30));
  assert(backend.Gpuva.Held && backend.Gpuva.RenderFence == 31 && !backend.Gpuva.OlderCount);
  /* A fence that no in-flight set has is refused. */
  assert(!AgxWin32GpuvaRetire(&backend.Gpuva, 30) && !AgxWin32GpuvaRetire(&backend.Gpuva, 32));
  completed_fence = 31;
  assert(open_batch(&ctx, 3) && !slots[1].windows_batch && !slots[2].windows_batch);
  assert(!backend.Gpuva.Held && live_leases == 0 && flushes == 0);
  /* EXP1161: a second GL context on the same screen submits; this context's
   * Begin and submissions are not refused. Room for one more submission is
   * made by retiring the other context's (oldest) set through the space;
   * that batch then retires by its fence without a second eviction. */
  struct agx_batch other_slots[AGX_MAX_BATCHES];
  struct agx_context other = {{&screen}, 0, {other_slots, {0}, {0}}};
  for (unsigned i = 0; i < AGX_MAX_BATCHES; ++i) other_slots[i] = (struct agx_batch){&other, NULL, {&vdm}};
  completed_fence = 39;
  assert(open_batch(&other, 0) && submit(&backend, &other_slots[0], 40));
  for (unsigned i = 0; i + 1u < DEPTH; ++i)
    assert(open_batch(&ctx, i) && submit(&backend, &slots[i], 41u + i));
  assert(backend.Gpuva.Older[0].Fence == 40 && backend.Gpuva.RenderFence == 40u + DEPTH - 1u);
  unsigned foreign_evicts = evicts;
  assert(open_batch(&ctx, DEPTH) && submit(&backend, &slots[DEPTH], 40u + DEPTH));
  assert(evicts == foreign_evicts + 1 && backend.Gpuva.Older[0].Fence == 41 &&
         backend.Gpuva.RenderFence == 40u + DEPTH);
  assert(!capsule(&other_slots[0])->Retired);
  assert(AgxWin32AsahiBatchPoll(&other_slots[0], 1000) && capsule(&other_slots[0])->Retired);
  assert(evicts == foreign_evicts + 1);
  completed_fence = 40u + DEPTH;
  assert(open_batch(&ctx, DEPTH + 1u));
  for (unsigned i = 0; i + 1u < DEPTH; ++i) assert(!slots[i].windows_batch);
  assert(!slots[DEPTH].windows_batch);
  assert(!backend.Gpuva.Held && !backend.Gpuva.OlderCount && live_leases == 0);
  free(other_slots[0].windows_batch);
  free(slots[DEPTH + 1u].windows_batch);
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
            "submitted_at", "retire_held", "AgxWin32AsahiBatchBegin"))
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

    def test_space_bounds_the_depth(self):
        space = SPACE.read_text()
        submit = body(space, "AgxWin32GpuvaSubmit")
        # A submission without a free older slot is refused; the newest set
        # becomes an older one on every path that installs a new Held.
        self.assertIn("space->OlderCount == AGX_GPUVA_IN_FLIGHT - 1u)", submit)
        self.assertEqual(submit.count("gpuva_shift(space);"), submit.count("space->Held = handles;"))
        self.assertIn("space->OlderCount ||", body(space, "AgxWin32GpuvaUnbind"))
        # The UMD lets a slot join a submission while fewer than the depth
        # hold it; the two bounds must agree.
        umd = (ROOT / "drivers/apple-agx/render-admission/umd/src/umd_internal.h").read_text()
        header = (ROOT / "drivers/apple-agx/mesa/winsys/agx_win32_gpuva.h").read_text()
        import re
        a = re.search(r"#define AGX_GPUVA_IN_FLIGHT (\d+)u", header).group(1)
        b = re.search(r"#define ADMISSION_UMD_IN_FLIGHT_SETS (\d+)u", umd).group(1)
        self.assertEqual(a, b)


if __name__ == "__main__":
    unittest.main()
