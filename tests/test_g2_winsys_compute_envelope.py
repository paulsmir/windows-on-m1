"""G2: the winsys compute submission is exactly what the KMD admits.

AgxWin32AsahiBatchFinishCompute (agx_win32_gpuva_batch.c) turns a Mesa
compute-only batch into an AGX4 compute envelope. This replay runs the
production winsys function against stub Mesa objects and hands the
envelope it submits to the production KMD parser (AppleAgxG4ParseComputeEx)
with the KMD's G13 context-store size. Invariants:
- the DMA buffer is the command BO and holds the native header + compute
  command, the private data repeats it behind the compute header;
- the per-job context-store buffer is a separate writable BO in the
  residency set, 64 KiB aligned and at least the G13 size;
- the written set is every BO the batch is the Mesa writer of, and only
  those (the UMD refreshes their CPU copies after the job);
- flags and timestamps are refused before anything is created, and a
  refused submission rejects the batch without a fence;
- GPU timestamps: Mesa's timestamp handles are stripped (the KMD records
  every compute job's firmware start/end);
- EXP1339: a helper program (compute scratch: a USC program in the
  pipeline pool, a scratch buffer in the batch, Mesa's configuration
  preamble scratch << 16, here 0) travels in the envelope the KMD admits.
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
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <stddef.h>
#include <string.h>
#include "agx_win32_gpuva.h"
#include "apple_agx_g4_submit.h"
#include "apple_agx_g13_compute_work.h"
#define AGX_MAX_BATCHES 8
#define AGX_WIN32_ENCODER_TAIL 0x100u
/* asahi_drm.h subset */
enum { DRM_ASAHI_CMD_RENDER = 0, DRM_ASAHI_CMD_COMPUTE = 1 };
struct drm_asahi_cmd_header { uint16_t cmd_type, size, vdm_barrier, cdm_barrier; };
struct drm_asahi_helper_program { uint32_t binary, cfg; uint64_t data; };
struct drm_asahi_timestamp { uint32_t handle, offset; };
struct drm_asahi_timestamps { struct drm_asahi_timestamp start, end; };
struct drm_asahi_cmd_compute {
  uint32_t flags, sampler_count;
  uint64_t cdm_ctrl_stream_base, cdm_ctrl_stream_end, sampler_heap;
  struct drm_asahi_helper_program helper;
  struct drm_asahi_timestamps ts;
};
static struct drm_asahi_cmd_header agx_cmd_header(bool compute, uint16_t v, uint16_t c) {
  struct drm_asahi_cmd_header h = { compute ? DRM_ASAHI_CMD_COMPUTE : DRM_ASAHI_CMD_RENDER,
      compute ? sizeof(struct drm_asahi_cmd_compute) : 0u, v, c };
  return h;
}
/* Mesa objects */
struct agx_va { uint64_t addr; };
struct agx_bo { size_t size; void *_map; struct agx_va *va; int refs;
                AGX_WIN32_GPUVA_BO view; uint32_t handle; };
struct util_dynarray { void *data; unsigned size; };
#define util_dynarray_num_elements(buf, type) ((buf)->size / sizeof(type))
#define util_dynarray_foreach(buf, type, elem) \
  for (type *elem = (type *)(buf)->data; elem < (type *)((char *)(buf)->data + (buf)->size); elem++)
struct agx_pool { struct util_dynarray bos; struct agx_bo *transient_bo; unsigned transient_offset; };
struct agx_encoder { struct agx_bo *bo; uint8_t *current, *end; };
struct agx_device { void *windows_private; struct agx_bo *zero_bo, *scratch_bo; };
struct agx_screen { struct agx_device dev; struct agx_bo *rodata; };
struct pipe_screen { int unused; };
struct agx_context { struct { struct pipe_screen *screen; } base; int any_faults;
                     struct agx_bo *timestamps; };
struct agx_batch {
  struct agx_context *ctx; void *windows_batch; int compute;
  struct agx_encoder vdm, cdm;
  struct { unsigned bit_count; uint32_t handles[8]; unsigned count; } bo_list;
  struct agx_pool pool, pipeline_pool;
  struct { struct agx_bo *bo; } sampler_heap;
};
typedef struct {
  struct agx_device *Native;
  AGX_WIN32_GPUVA_SPACE Gpuva;
  int GpuvaReady, Failed;
  uint64_t NewestFence;
  uint64_t TimestampFrequency, TimestampOffset;
} AGX_WIN32_ASAHI_BACKEND;
#define AGX_WIN32_ASAHI_FAIL(b, site) ((b)->Failed = (int)(site))
static struct agx_screen screen;
static struct pipe_screen pipe;
static struct agx_screen *agx_screen(struct pipe_screen *s) { (void)s; return &screen; }
static struct agx_device *agx_device(struct pipe_screen *s) { (void)s; return &screen.dev; }
static int agx_batch_is_compute(const struct agx_batch *b) { return b->compute; }
static void *agx_bo_map(struct agx_bo *bo) { return bo->_map; }
#define AGX_BATCH_FOREACH_BO_HANDLE(batch, handle) \
  for (unsigned _i = 0; _i < (batch)->bo_list.count && ((handle) = (int)(batch)->bo_list.handles[_i], 1); ++_i)
/* BO registry: every created or registered BO has a GPUVA view. */
static struct agx_bo *bos[32]; static unsigned nbos; static uint64_t next_va = 0x2000000ULL;
static struct agx_bo *make_bo(size_t size) {
  struct agx_bo *bo = calloc(1, sizeof(*bo)); struct agx_va *va = calloc(1, sizeof(*va));
  bo->size = (size + 0xffffu) & ~(size_t)0xffffu; bo->_map = calloc(1, bo->size);
  va->addr = next_va; next_va += bo->size + 0x10000u; bo->va = va; bo->refs = 1;
  bo->handle = 100u + nbos;
  bo->view = (AGX_WIN32_GPUVA_BO){ 0x5000u + nbos, va->addr, bo->size, 1u };
  bos[nbos++] = bo; return bo;
}
static unsigned creates, refuse_create_at = 99;
static struct agx_bo *agx_bo_create(struct agx_device *d, size_t size, unsigned align,
                                    unsigned flags, const char *label) {
  (void)d; (void)flags; (void)label; assert(align == 0u);
  return ++creates == refuse_create_at ? NULL : make_bo(size);
}
static void agx_bo_unreference(struct agx_device *d, struct agx_bo *bo) { (void)d; --bo->refs; }
static const AGX_WIN32_GPUVA_BO *AgxWin32AsahiGpuvaBo(AGX_WIN32_ASAHI_BACKEND *b, struct agx_bo *bo) {
  (void)b; return bo ? &bo->view : NULL;
}
static struct agx_bo *AgxWin32AsahiLookupBo(struct agx_device *d, int handle) {
  (void)d; for (unsigned i = 0; i < nbos; ++i) if ((int)bos[i]->handle == handle) return bos[i];
  return NULL;
}
static struct agx_batch *writer_of[256];
static struct agx_batch *agx_writer_get(struct agx_context *ctx, unsigned handle) {
  (void)ctx; return handle < 256u ? writer_of[handle] : NULL;
}
static int AgxWin32AsahiShadowPublish(struct agx_bo *bo, size_t bytes) { (void)bo; (void)bytes; return 1; }
static int retire_held(struct agx_batch *batch, AGX_WIN32_ASAHI_BACKEND *b) { (void)batch; (void)b; return 1; }
void (*AgxWin32BatchRefusalHook)(unsigned, unsigned, unsigned, unsigned);
/* The submission the KMD would receive. */
static struct {
  unsigned calls, count, written_count;
  AGX_WIN32_GPUVA_BO refs[32], written[16], command;
  uint32_t command_bytes, private_bytes;
  unsigned char private_data[256];
} sub;
int AgxWin32GpuvaSubmit(AGX_WIN32_GPUVA_SPACE *s, const AGX_WIN32_GPUVA_BO *const *refs,
    unsigned count, const AGX_WIN32_GPUVA_BO *command, uint32_t command_bytes,
    const AGX_WIN32_GPUVA_BO *const *written, unsigned written_count,
    const void *private_data, uint32_t private_bytes, uint64_t *fence) {
  (void)s; ++sub.calls; assert(count <= 32u && written_count <= 16u && private_bytes <= 256u);
  sub.count = count; sub.written_count = written_count; sub.command = *command;
  sub.command_bytes = command_bytes; sub.private_bytes = private_bytes;
  for (unsigned i = 0; i < count; ++i) sub.refs[i] = *refs[i];
  for (unsigned i = 0; i < written_count; ++i) sub.written[i] = *written[i];
  memcpy(sub.private_data, private_data, private_bytes);
  *fence = 41u; return 1;
}
@@FUNCTIONS@@
/* KMD side: the process graph maps exactly the residency set (writable
 * except the command), and the DMA buffer is the CPU envelope. */
static int kmd_access(void *context, unsigned long long va, unsigned int bytes, int write,
                      APPLE_AGX_G4_ACCESS_KIND kind, unsigned int ordinal) {
  (void)context; (void)ordinal;
  for (unsigned i = 0; i < sub.count; ++i) {
    const AGX_WIN32_GPUVA_BO *r = &sub.refs[i];
    if (va >= r->Va && va + bytes <= r->Va + r->Bytes) {
      if (kind == AppleAgxG4AccessCpuEnvelope) return !write && r->Va == sub.command.Va;
      return !write || r->Allocation != sub.command.Allocation;
    }
  }
  return 0;
}
static int in_refs(const struct agx_bo *bo) {
  for (unsigned i = 0; i < sub.count; ++i) if (sub.refs[i].Allocation == bo->view.Allocation) return 1;
  return 0;
}
int main(void) {
  AGX_WIN32_ASAHI_BACKEND b; memset(&b, 0, sizeof(b));
  struct agx_context ctx = {{&pipe}, 0, NULL};
  struct agx_batch batch; memset(&batch, 0, sizeof(batch));
  screen.dev.windows_private = &b; b.Native = &screen.dev; b.GpuvaReady = 1;
  screen.rodata = make_bo(0x10000); screen.dev.zero_bo = make_bo(0x10000);
  screen.dev.scratch_bo = make_bo(0x10000);
  struct agx_bo *cdm = make_bo(0x20000), *pool = make_bo(0x40000);
  next_va = APPLE_AGX_G4_USC_EXECUTION_BASE; /* the pipeline pool is USC-addressable */
  struct agx_bo *low = make_bo(0x40000);
  next_va = 0x3000000ULL;
  struct agx_bo *src = make_bo(0x10000), *dst = make_bo(0x10000), *kernel = make_bo(0x10000);
  struct agx_bo *pools[1] = { pool }, *lows[1] = { low };
  batch.ctx = &ctx; batch.compute = 1;
  batch.cdm.bo = cdm; batch.cdm.current = (uint8_t *)cdm->_map + 0x60;
  batch.bo_list.bit_count = 128; batch.bo_list.count = 4;
  batch.bo_list.handles[0] = src->handle; batch.bo_list.handles[1] = dst->handle;
  batch.bo_list.handles[2] = kernel->handle; batch.bo_list.handles[3] = cdm->handle;
  batch.pool.bos.data = pools; batch.pool.bos.size = sizeof pools; batch.pool.transient_bo = pool;
  batch.pipeline_pool.bos.data = lows; batch.pipeline_pool.bos.size = sizeof lows;
  batch.pipeline_pool.transient_bo = low;
  writer_of[dst->handle] = &batch;
  AGX_G4_BATCH *g = calloc(1, sizeof(*g)); batch.windows_batch = g;
  struct drm_asahi_cmd_compute command = {0};
  command.cdm_ctrl_stream_base = cdm->va->addr;
  command.cdm_ctrl_stream_end = cdm->va->addr + 0x60;
  /* The CDM scope is entered and left once; a render batch is refused. */
  batch.compute = 0; assert(!AgxWin32AsahiBatchComputeEnter(&batch)); batch.compute = 1;
  assert(AgxWin32AsahiBatchComputeEnter(&batch) && g->ComputeEntered == 1);
  assert(!AgxWin32AsahiBatchFinishCompute(&batch, &command) && !sub.calls && !g->Rejected);
  assert(AgxWin32AsahiBatchComputeLeave(&batch) && !g->ComputeEntered);
  assert(AgxWin32AsahiBatchComputeFinalize(&batch, batch.cdm.current));
  assert(!AgxWin32AsahiBatchComputeFinalize(&batch, (uint8_t *)pool->_map));
  /* Flags are refused before anything exists. */
  command.flags = 1u;
  assert(!AgxWin32AsahiBatchFinishCompute(&batch, &command) && !creates && !g->Rejected);
  command.flags = 0u;
  /* GPU timestamps: Mesa's handle/offsets never reach the KMD, which
   * records every compute job's firmware start/end itself. */
  command.ts.start.handle = 3u; command.ts.start.offset = 0x40u;
  command.ts.end.handle = 3u; command.ts.end.offset = 0x48u;
  /* EXP1339: a compute helper (scratch) is submitted. */
  command.helper.binary = 0x40u; command.helper.cfg = 0u; command.helper.data = src->va->addr;
  assert(AgxWin32AsahiBatchFinishCompute(&batch, &command));
  assert(g->Submitted && g->Fence == 41u && b.NewestFence == 41u && sub.calls == 1u);
  /* The DMA buffer is the command BO; the private data is the envelope. */
  assert(g->Command && g->Preemption && sub.command.Va == g->Command->va->addr &&
         sub.command_bytes == sizeof(APPLE_AGX_G4_NATIVE_HEADER) + sizeof(APPLE_AGX_G4_NATIVE_COMPUTE) &&
         sub.private_bytes == sizeof(APPLE_AGX_G4_COMPUTE_ENVELOPE));
  APPLE_AGX_G4_COMPUTE_ENVELOPE envelope; memcpy(&envelope, sub.private_data, sizeof(envelope));
  assert(memcmp(g->Command->_map, &envelope.Native, sub.command_bytes) == 0);
  assert(envelope.Native.Type == APPLE_AGX_G4_COMPUTE && envelope.Native.VdmBarrier == 0u &&
         envelope.Native.CdmBarrier == 0u);
  assert(envelope.Header.Preemption.Va == g->Preemption->va->addr &&
         envelope.Header.Preemption.Bytes >= APPLE_AGX_G13_COMPUTE_PREEMPT_BYTES);
  /* Residency: command, stream, preemption, device pages, batch BOs, pools. */
  assert(in_refs(g->Command) && in_refs(cdm) && in_refs(g->Preemption) && in_refs(screen.rodata) &&
         in_refs(screen.dev.zero_bo) && in_refs(screen.dev.scratch_bo) && in_refs(src) &&
         in_refs(dst) && in_refs(kernel) && in_refs(pool) && in_refs(low));
  /* Written: the BO the batch writes (the copy destination), only it. */
  assert(sub.written_count == 1u && sub.written[0].Allocation == dst->view.Allocation);
  /* The KMD admits exactly this envelope. */
  APPLE_AGX_G4_COMPUTE_VIEW view; APPLE_AGX_G4_FAILURE failure;
  assert(AppleAgxG4IsCompute(sub.private_data, sub.private_bytes));
  assert(AppleAgxG4ParseComputeEx(sub.private_data, sub.private_bytes, sub.private_bytes,
      sub.command.Va, sub.command_bytes, APPLE_AGX_G13_COMPUTE_PREEMPT_BYTES,
      kmd_access, NULL, &view, &failure) == AppleAgxG4ParseOk);
  assert(view.Compute.CdmCtrlStreamBase == cdm->va->addr && view.Preemption.Va == g->Preemption->va->addr);
  assert(view.Compute.Helper.Binary == 0x40u && view.Compute.Helper.Data == src->va->addr &&
         view.Compute.Helper.Cfg == 0u);
  assert(!view.Compute.Timestamps.StartHandle && !view.Compute.Timestamps.EndHandle &&
         !view.Compute.Timestamps.StartOffset && !view.Compute.Timestamps.EndOffset);
  /* Release returns both job BOs once the job retired. */
  g->Retired = 1;
  struct agx_bo *command_bo = g->Command, *preempt_bo = g->Preemption;
  assert(AgxWin32AsahiBatchRelease(&batch) && !batch.windows_batch &&
         command_bo->refs == 0 && preempt_bo->refs == 0);
  /* A failed creation rejects the batch without a submission. */
  AGX_G4_BATCH *h = calloc(1, sizeof(*h)); batch.windows_batch = h;
  refuse_create_at = creates + 1u; b.Failed = 0;
  assert(!AgxWin32AsahiBatchFinishCompute(&batch, &command) && h->Rejected && !h->Submitted &&
         sub.calls == 1u && b.Failed);
  puts("g2 winsys compute envelope: PASS");
  return 0;
}
'''


class WinsysComputeEnvelope(unittest.TestCase):
    def test_winsys_envelope_is_admitted_by_the_kmd_parser(self):
        source = SRC.read_text()
        start = source.index("typedef struct {\n  struct agx_bo *Command;")
        capsule_type = source[start:source.index("} AGX_G4_BATCH;", start) + len("} AGX_G4_BATCH;")]
        functions = capsule_type + "\n" + "\n".join(body(source, name) for name in (
            "backend", "capsule", "batch_refuse", "compute_only", "render_compute", "add_bo", "pool_extent",
            "publish_shadows", "strip_timestamps",
            "AgxWin32AsahiBatchComputeEnter",
            "AgxWin32AsahiBatchComputeLeave", "AgxWin32AsahiBatchComputeFinalize",
            "AgxWin32AsahiBatchFinishCompute", "release_lease", "AgxWin32AsahiBatchRelease"))
        written_limit = next(line for line in source.splitlines()
                             if line.startswith("#define AGX_G4_WRITTEN_LIMIT"))
        program = PROGRAM.replace("@@FUNCTIONS@@", written_limit + "\n" + functions)
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / "compute.c"
            exe = Path(directory) / "compute"
            path.write_text(program)
            built = subprocess.run([
                os.environ.get("CC", "clang"), "-std=c11", "-Wall", "-Wextra", "-Werror",
                "-Wno-unused-function", "-fsanitize=address,undefined",
                "-I", str(ROOT / "drivers/apple-agx/mesa/winsys"),
                "-I", str(ROOT / "drivers/apple-agx/shared/include"),
                str(path), str(ROOT / "drivers/apple-agx/shared/src/apple_agx_g4_submit.c"),
                "-o", str(exe)], capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn("g2 winsys compute envelope: PASS", ran.stdout)


if __name__ == "__main__":
    unittest.main()
