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
- when several open batches are flushed back to back (agx_flush_all), each
  Finish first retires the completed submission that still holds the
  residency set, so the next submission is not refused;
- a held set owned by no batch of this context is still refused;
- EXP1071 hardware: a retired submission returns its private scene lease at
  once, so open batches flushed back to back hold at most one lease (holding
  each until cleanup exhausted the process range: D3DERR_OUTOFVIDEOMEMORY).
- EXP1082: a submission still running on the GPU (its fence not signalled)
  is neither waited for nor retired by Begin; it stays submitted and holding
  while the next batch is built, and that batch's Finish retires it before
  submitting. A held set owned by no batch of this context still refuses
  Begin.
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

PROGRAM = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#define AGX_MAX_BATCHES 4
#define BITSET_TEST(set, i) ((((set)[(i) / 32u]) >> ((i) % 32u)) & 1u)
#define BITSET_CLEAR(set, i) ((set)[(i) / 32u] &= ~(1u << ((i) % 32u)))
#define BITSET_SET(set, i) ((set)[(i) / 32u] |= (1u << ((i) % 32u)))
typedef unsigned APPLE_AGX_U32;
#include <string.h>
typedef struct { unsigned ManagerId, ManagerGeneration, SceneId, SceneGeneration; } APPLE_AGX_G4_PRIVATE_LEASE;
#define APPLE_AGX_G3_PRIVATE_MAGIC 1u
#define APPLE_AGX_G3_PRIVATE_VERSION 1u
#define APPLE_AGX_G3_PRIVATE_RELEASE 2u
typedef struct { unsigned Magic, Version, Bytes, Operation, ManagerId, ManagerGeneration,
  SceneId, SceneGeneration; } APPLE_AGX_G3_PRIVATE_REQUEST;
static unsigned live_leases;
static int private_escape(void *context, APPLE_AGX_G3_PRIVATE_REQUEST *r) {
  (void)context; if (r->Operation != APPLE_AGX_G3_PRIVATE_RELEASE || !live_leases) return 0;
  --live_leases; return 1;
}
typedef struct {
  struct { uint64_t *Held; uint64_t RenderFence; unsigned Terminal; void *Context;
    struct { int (*PrivateEscape)(void *, APPLE_AGX_G3_PRIVATE_REQUEST *); } Ops; } Gpuva;
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
static unsigned flushes, retires;
static uint64_t completed_fence = ~0ull;
static int AgxWin32GpuvaComplete(void *space, uint64_t fence) {
  (void)space; return fence && fence <= completed_fence;
}
static uint64_t held_storage;
static int AgxWin32GpuvaRetire(void *space, uint64_t completion) {
  AGX_WIN32_ASAHI_BACKEND *b = (AGX_WIN32_ASAHI_BACKEND *)
      ((char *)space - offsetof(AGX_WIN32_ASAHI_BACKEND, Gpuva));
  if (!b->Gpuva.Held || completion != b->Gpuva.RenderFence) return 0;
  b->Gpuva.Held = NULL; b->Gpuva.RenderFence = 0; ++retires;
  return 1;
}
static void agx_flush_batch(struct agx_context *ctx, struct agx_batch *batch) {
  (void)ctx; (void)batch; ++flushes;
}
static void agx_sync_batch(struct agx_context *ctx, struct agx_batch *batch) {
  unsigned i = (unsigned)(batch - ctx->batches.slots);
  free(batch->windows_batch); batch->windows_batch = NULL;
  BITSET_CLEAR(ctx->batches.submitted, i);
}
@@FUNCTIONS@@
/* Mirrors AgxWin32GpuvaSubmit: refused while another submission holds. */
static int submit(AGX_WIN32_ASAHI_BACKEND *b, struct agx_batch *batch, uint64_t fence) {
  if (!retire_held(batch, b) || b->Gpuva.Held) return 0;
  /* prepare_process_buffers: this batch's scene lease. */
  capsule(batch)->Lease = (APPLE_AGX_G4_PRIVATE_LEASE){1, 1, (unsigned)fence, 1}; ++live_leases;
  b->Gpuva.Held = &held_storage; b->Gpuva.RenderFence = fence;
  AGX_G4_BATCH *g = capsule(batch); g->Submitted = 1; g->Fence = fence;
  unsigned i = (unsigned)(batch - batch->ctx->batches.slots);
  BITSET_CLEAR(batch->ctx->batches.active, i); BITSET_SET(batch->ctx->batches.submitted, i);
  return 1;
}
int main(void) {
  AGX_WIN32_ASAHI_BACKEND backend = {{0}, 1, 0};
  backend.Gpuva.Ops.PrivateEscape = private_escape;
  struct pipe_screen screen = {{&backend}};
  struct agx_batch slots[AGX_MAX_BATCHES];
  struct agx_bo vdm;
  struct agx_context ctx = {{&screen}, 0, {slots, {0}, {0}}};
  for (unsigned i = 0; i < AGX_MAX_BATCHES; ++i) slots[i] = (struct agx_batch){&ctx, NULL, {&vdm}};
  /* A opens, then B opens while A is still active: A is not flushed. */
  BITSET_SET(ctx.batches.active, 0); assert(AgxWin32AsahiBatchBegin(&slots[0]));
  BITSET_SET(ctx.batches.active, 1); assert(AgxWin32AsahiBatchBegin(&slots[1]));
  assert(flushes == 0 && slots[0].windows_batch && slots[1].windows_batch);
  /* agx_flush_all: A then B back to back; B's Finish retires A first. */
  assert(submit(&backend, &slots[0], 10));
  assert(submit(&backend, &slots[1], 11));
  assert(retires == 1 && ((AGX_G4_BATCH *)slots[0].windows_batch)->Retired);
  assert(live_leases == 1 && !((AGX_G4_BATCH *)slots[0].windows_batch)->Lease.SceneId);
  /* C opens: the completed submitted batches are retired and cleaned up. */
  BITSET_SET(ctx.batches.active, 2); assert(AgxWin32AsahiBatchBegin(&slots[2]));
  assert(flushes == 0 && retires == 2 && !backend.Gpuva.Held && live_leases == 0);
  assert(!slots[0].windows_batch && !slots[1].windows_batch && slots[2].windows_batch);
  /* A held set that no batch of this context owns is still refused. */
  backend.Gpuva.Held = &held_storage; backend.Gpuva.RenderFence = 99;
  assert(!submit(&backend, &slots[2], 12));
  free(slots[2].windows_batch); slots[2].windows_batch = NULL;
  BITSET_CLEAR(ctx.batches.active, 2);
  /* EXP1082: ... and a foreign held set refuses a new batch. */
  BITSET_SET(ctx.batches.active, 3); assert(!AgxWin32AsahiBatchBegin(&slots[3]));
  backend.Gpuva.Held = NULL; backend.Gpuva.RenderFence = 0;
  assert(AgxWin32AsahiBatchBegin(&slots[3]));
  /* D submits; the GPU has not finished it yet. */
  completed_fence = 19;
  assert(submit(&backend, &slots[3], 20));
  unsigned before = retires;
  /* E begins without waiting for or retiring D. */
  BITSET_SET(ctx.batches.active, 0); assert(AgxWin32AsahiBatchBegin(&slots[0]));
  assert(retires == before && slots[3].windows_batch && !capsule(&slots[3])->Retired);
  assert(backend.Gpuva.Held && live_leases == 1 && BITSET_TEST(ctx.batches.submitted, 3));
  /* E's Finish retires D (the wait) before it submits. */
  assert(submit(&backend, &slots[0], 21));
  assert(retires == before + 1 && capsule(&slots[3])->Retired && live_leases == 1);
  /* Once E completes, the next Begin retires and cleans up both. */
  completed_fence = 21;
  BITSET_SET(ctx.batches.active, 1); assert(AgxWin32AsahiBatchBegin(&slots[1]));
  assert(retires == before + 2 && !slots[3].windows_batch && !slots[0].windows_batch);
  assert(!backend.Gpuva.Held && live_leases == 0 && flushes == 0);
  free(slots[1].windows_batch);
  puts("PASS");
  return 0;
}
'''


class BatchBeginKeepsActive(unittest.TestCase):
    def test_new_batch_keeps_other_active_batches_open(self):
        source = SRC.read_text()
        functions = "\n".join(body(source, name) for name in (
            "backend", "capsule", "batch_refuse", "release_lease", "AgxWin32AsahiBatchPoll",
            "held_by", "retire_held", "AgxWin32AsahiBatchBegin"))
        program = PROGRAM.replace("@@FUNCTIONS@@", functions)
        program = program.replace("#include <stdlib.h>", "#include <stdlib.h>\n#include <stddef.h>")
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "begin.c"
            exe = Path(directory) / "begin"
            path.write_text(program)
            built = subprocess.run([os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra",
                                    "-Werror", "-Wno-unused-function", "-fsanitize=address,undefined",
                                    str(path), "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)

    def test_finish_retires_the_completed_holder_before_its_lease(self):
        source = SRC.read_text()
        finish = body(source, "AgxWin32AsahiBatchFinish")
        self.assertLess(finish.index("retire_held(batch,b)"), finish.index("prepare_process_buffers("))


if __name__ == "__main__":
    unittest.main()
