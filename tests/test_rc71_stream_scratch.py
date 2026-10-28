"""RC71 (EXP1990 arm b): GPU indirect stream outputs are reused, not re-created.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
Each GPU stream draw (GravityMark: MaxCount 60000, about 7 per Execute)
needs about 3 MiB of VDM records and 7 MiB of USC words. Through the batch
pools every draw created two dedicated BOs, and batch cleanup released them
into the 64 MiB winsys BO cache, which then evicted everything else:
EXP1990 arm b had 3125 evictions and 3585 misses in 78 s (arm a, stream off:
0 and 541), 533 BO creations of at least 10 ms and residency trims from
Execute 260 on, before the BO bind that removed the device.

The projected production code (stream allocation in
agx_win32_indirect_stream_emit.h, batch init/cleanup in agx_batch.c,
agx_pool in pool.c, context destroy in agx_pipe.c) runs a steady stream
workload with four batches in flight. Invariants:
- after the first in-flight window, every stream draw reuses retained
  storage: no BO is created and none is released to the winsys;
- an output BO is never handed to a draw while the batch that last used it
  is still in flight;
- outputs keep their VDM (256 B) and USC (64 B) alignment;
- destroying the context releases every retained BO exactly once.
"""
from pathlib import Path
import os
import subprocess
import tempfile
import unittest
from test_d3d10_shader_fields_init import project
from test_d3d12_indirect_stream import WINSYS


def between(text, start, end, include_start=False):
    i = text.index(start)
    j = text.index(end, i + len(start))
    return text[i if include_start else i + len(start):j]


def function(text, marker):
    start = text.index(marker)
    return text[start:text.index('\n}', start) + 2]


HARNESS = r'''
#include <assert.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define unlikely(x) (x)
#define MAX2(a, b) ((a) > (b) ? (a) : (b))
#define ALIGN_POT(x, a) (((x) + (a) - 1) & ~((a) - 1))
#define AGX_BO_LOW_VA (1u << 0)
#define AGX_WIN32_BO_GPU_ONLY (1u << 29)
#define AGX_BO_WRITEBACK (1u << 3)
static unsigned util_next_power_of_two(unsigned x) { unsigned p = 1; while (p < x) p <<= 1; return p; }
struct util_dynarray { void *data; unsigned size, capacity; };
#define UTIL_DYNARRAY_INIT ((struct util_dynarray){0})
static void *grow_bytes(struct util_dynarray *d, unsigned n) {
  if (d->size + n > d->capacity) { d->capacity = (d->size + n) * 2; d->data = realloc(d->data, d->capacity); assert(d->data); }
  void *p = (char *)d->data + d->size; d->size += n; return p; }
#define util_dynarray_grow(buf, type, n) ((type *)grow_bytes((buf), (n) * sizeof(type)))
#define util_dynarray_append_typed(buf, type, v) do { type v_ = (v); memcpy(grow_bytes((buf), sizeof(type)), &v_, sizeof(type)); } while (0)
#define util_dynarray_append(buf, v) util_dynarray_append_typed(buf, __typeof__(v), v)
#define util_dynarray_foreach(buf, type, elem) \
  for (type *elem = (type *)(buf)->data; elem < (type *)((char *)(buf)->data + (buf)->size); ++elem)
static void util_dynarray_fini(struct util_dynarray *d) { free(d->data); memset(d, 0, sizeof(*d)); }
struct agx_va { uint64_t addr; };
struct agx_bo { size_t size; struct agx_va *va; int refcnt; unsigned flags, busy; struct agx_va storage; void *cpu; };
struct agx_ptr { void *cpu; uint64_t gpu; };
struct agx_device { int unused; };
struct agx_pool { struct agx_device *dev; struct util_dynarray bos; struct agx_bo *transient_bo;
  unsigned transient_offset, create_flags; const char *label; };
struct agx_context { struct { struct agx_device *screen; } base;
@@SPARE@@
};
struct agx_batch { struct agx_context *ctx; struct agx_pool windows_stream_pool, windows_stream_usc_pool; };
static struct agx_device *agx_device(struct agx_device *d) { return d; }
static void AgxWin32PerfNote(const char *fmt, ...) { (void)fmt; }
#define MAX_BOS 8192
static struct agx_bo *bos[MAX_BOS]; static unsigned created, winsys_releases, fail_writeback, last_capacity;
static uint64_t next_va = 0x1100010000ull;
static struct agx_bo *agx_bo_create(struct agx_device *d, size_t size, unsigned align, unsigned flags, const char *label) {
  (void)d; (void)align; (void)label; assert(created < MAX_BOS && size);
  if ((flags & AGX_BO_WRITEBACK) && fail_writeback) return NULL;
  struct agx_bo *bo = calloc(1, sizeof(*bo)); assert(bo);
  /* rc89: only the feedback words are CPU-mapped (writeback). */
  if (flags & AGX_BO_WRITEBACK) { bo->cpu = malloc(size); assert(bo->cpu); memset(bo->cpu, 0xee, size); }
  bo->size = size; bo->flags = flags; bo->refcnt = 1; bo->va = &bo->storage;
  bo->storage.addr = next_va; next_va += (size + 0xffff) & ~(uint64_t)0xffff;
  bos[created++] = bo; return bo; }
static void agx_bo_unreference(struct agx_device *d, struct agx_bo *bo) {
  (void)d; if (!bo) return; assert(bo->refcnt > 0 && !bo->busy);
  if (--bo->refcnt == 0) ++winsys_releases; }
static void *agx_bo_map(struct agx_bo *bo) { assert((bo->flags & AGX_BO_WRITEBACK) && "GPU-only output mapped"); return bo->cpu; }
static unsigned feedback_refs;
static void agx_batch_add_bo(struct agx_batch *b, struct agx_bo *bo) { (void)b; assert(bo && (bo->flags & AGX_BO_WRITEBACK)); ++feedback_refs; }
@@HEADER@@
@@POOL@@
static struct agx_device device;
static void batch_init(struct agx_batch *batch) {
  struct agx_device *dev = &device;
@@INIT@@
}
static void stream_draw(struct agx_batch *batch, unsigned count, unsigned record, unsigned tail,
                        unsigned usc_stride, struct agx_bo **vb, struct agx_bo **ub) {
  struct agx_context *ctx = batch->ctx;
  /* agx_win32_stream_flush_inputs, APPLE_AGX_D3D12_STREAM_CAPACITY unset */
  agx_win32_stream_feedback_ensure(ctx);
@@DRAW@@
  assert(vdm_bo && usc_bo && vdm_bo != usc_bo && vdm.gpu && usc.gpu && !vdm.cpu && !usc.cpu);
  assert(!(vdm.gpu & 255u) && !(usc.gpu & 63u));
  /* rc89: outputs hold `capacity` records (MaxCount until feedback). */
  assert(capacity && capacity <= count);
  assert(vdm.gpu - vdm_bo->va->addr + (uint64_t)capacity * record + tail <= vdm_bo->size);
  assert(usc.gpu - usc_bo->va->addr + (uint64_t)capacity * usc_stride <= usc_bo->size);
  last_capacity = capacity;
  assert((usc_bo->flags & AGX_BO_LOW_VA) && (vdm_bo->flags & AGX_WIN32_BO_GPU_ONLY) && !(vdm_bo->flags & AGX_BO_LOW_VA));
  *vb = vdm_bo; *ub = usc_bo;
}
static void batch_cleanup(struct agx_context *ctx, struct agx_batch *batch) {
@@CLEANUP@@
}
#define CALLS 7u
#define IN_FLIGHT 4u
#define BATCHES 300u
#if defined(RC89_CAPACITY)
@@RC89@@
#elif defined(FRAME_PATTERN)
/* rc88 (EXP2019b): a frame records its stream draws in two batches (six
 * draws, then one), FRAME_LAG frames are in flight, and both batches of the
 * oldest frame retire before the next frame records anything. */
#define FRAME_LAG 2u
#define FRAMES 200u
int main(void) {
  static struct agx_context ctx; ctx.base.screen = &device;
  static struct agx_batch batches[FRAME_LAG][2];
  static struct agx_bo *used[FRAME_LAG][2][2 * CALLS];
  static const unsigned draws[2] = {CALLS - 1u, 1u};
  unsigned steady_created = 0, steady_released = 0;
  for (unsigned f = 0; f < FRAMES + FRAME_LAG; ++f) {
    unsigned slot = f % FRAME_LAG, before_created = created, before_released = winsys_releases;
    if (f >= FRAME_LAG)
      for (unsigned b = 0; b < 2; ++b) {
        for (unsigned i = 0; i < 2 * draws[b]; ++i) { assert(used[slot][b][i]->busy); used[slot][b][i]->busy = 0; }
        batch_cleanup(&ctx, &batches[slot][b]);
      }
    if (f >= FRAMES) continue;
    for (unsigned b = 0, c = 0; b < 2; ++b) {
      struct agx_batch *batch = &batches[slot][b];
      batch->ctx = &ctx; batch_init(batch);
      for (unsigned d = 0; d < draws[b]; ++d, ++c) {
        struct agx_bo *vb, *ub;
        stream_draw(batch, 60000u, 44u, 0x808u, c == 3 ? 192u : 128u, &vb, &ub);
        assert(!vb->busy && !ub->busy);
        vb->busy = ub->busy = 1;
        used[slot][b][2 * d] = vb; used[slot][b][2 * d + 1] = ub;
      }
    }
    if (f > FRAME_LAG) { steady_created += created - before_created; steady_released += winsys_releases - before_released; }
  }
  @@FINI@@
  unsigned live = 0;
  for (unsigned i = 0; i < created; ++i) live += bos[i]->refcnt != 0;
  printf("created=%u steady_created=%u steady_released=%u winsys_releases=%u live=%u\n",
         created, steady_created, steady_released, winsys_releases, live);
  assert(steady_created == 0 && steady_released == 0);
  /* Retention never exceeds what the in-flight window itself needed
   * (plus rc89's feedback words). */
  assert(created <= 2u * CALLS * FRAME_LAG + 1u);
  assert(live == 0 && winsys_releases == created);
  for (unsigned i = 0; i < created; ++i) { free(bos[i]->cpu); free(bos[i]); }
  puts("RC71 stream scratch: PASS");
  return 0;
}
#else
int main(void) {
  static struct agx_context ctx; ctx.base.screen = &device;
  static struct agx_batch batches[IN_FLIGHT];
  static struct agx_bo *used[IN_FLIGHT][2 * CALLS];
  unsigned steady_created = 0, steady_released = 0;
  for (unsigned n = 0; n < BATCHES + IN_FLIGHT; ++n) {
    struct agx_batch *batch = &batches[n % IN_FLIGHT];
    unsigned before_created = created, before_released = winsys_releases;
    if (n >= IN_FLIGHT) {
      /* The oldest batch retired: agx_batch_cleanup runs after its fence. */
      for (unsigned i = 0; i < 2 * CALLS; ++i) { assert(used[n % IN_FLIGHT][i]->busy); used[n % IN_FLIGHT][i]->busy = 0; }
      batch_cleanup(&ctx, batch);
    }
    if (n >= BATCHES) continue;
    batch->ctx = &ctx; batch_init(batch);
    for (unsigned c = 0; c < CALLS; ++c) {
      struct agx_bo *vb, *ub;
      /* GravityMark: MaxCount 60000, 44-byte records, 128-byte USC slots;
       * one draw per call varies its USC size. */
      stream_draw(batch, 60000u, 44u, 0x808u, c == 3 ? 192u : 128u, &vb, &ub);
      assert(!vb->busy && !ub->busy); /* never handed out while in flight */
      vb->busy = ub->busy = 1;
      used[n % IN_FLIGHT][2 * c] = vb; used[n % IN_FLIGHT][2 * c + 1] = ub;
    }
    if (n >= IN_FLIGHT) { steady_created += created - before_created; steady_released += winsys_releases - before_released; }
  }
  @@FINI@@
  unsigned live = 0;
  for (unsigned i = 0; i < created; ++i) live += bos[i]->refcnt != 0;
  printf("created=%u steady_created=%u steady_released=%u winsys_releases=%u live=%u\n",
         created, steady_created, steady_released, winsys_releases, live);
  assert(steady_created == 0 && steady_released == 0);
  assert(created <= 2u * CALLS * IN_FLIGHT + 1u);
  assert(live == 0 && winsys_releases == created);
  for (unsigned i = 0; i < created; ++i) { free(bos[i]->cpu); free(bos[i]); }
  puts("RC71 stream scratch: PASS");
  return 0;
}
#endif
'''


def run_stream_harness(self, max_count=60000, defines=(), main=''):
    """Builds the projected stream path into HARNESS and runs it with draws of
    max_count records (rc87 reuses it for GravityMark FHD 200k). rc89: main
    replaces the harness's main() when RC89_CAPACITY is defined; returns
    the harness's stdout."""
    with tempfile.TemporaryDirectory(prefix='rc71-stream-scratch-') as tmp:
        out = project(Path(tmp) / 'native')
        if out is None:
            self.skipTest('reference Mesa tree not available')
        asahi = out / 'src/gallium/drivers/asahi'
        state_h = (asahi / 'agx_state.h').read_text()
        batch_c = (asahi / 'agx_batch.c').read_text()
        pipe_c = (asahi / 'agx_pipe.c').read_text()
        pool_c = (out / 'src/asahi/lib/pool.c').read_text()
        emit = (WINSYS / 'agx_win32_indirect_stream_emit.h').read_text()
        spare = [l for l in state_h.splitlines() if 'windows_stream_' in l and
                 l.strip().startswith(('struct agx_bo', 'unsigned long long'))]
        header = WINSYS / 'agx_win32_stream_scratch.h'
        pool = '#define POOL_SLAB_SIZE (256 * 1024)\n' + '\n'.join(function(pool_c, m) for m in (
            'static struct agx_bo *\nagx_pool_alloc_backing(', 'void\nagx_pool_init(',
            'void\nagx_pool_cleanup(', 'struct agx_ptr\nagx_pool_alloc_aligned_with_bo('))
        init_fn = function(batch_c, 'static void\nagx_batch_init(')
        init = between(init_fn, '#ifdef APPLE_AGX_GPUVA_WINSYS\n', '   agx_pool_init(&batch->pool,')
        cleanup_fn = function(batch_c, 'static void\nagx_batch_cleanup(')
        cleanup = between(cleanup_fn, '   agx_pool_cleanup(&batch->pipeline_pool);\n',
                          '\n   util_dynarray_fini(&batch->scissor);')
        draw = between(emit, '   struct agx_bo *vdm_bo = NULL, *usc_bo = NULL;',
                       '   if (!capacity || capacity > count ||', include_start=True)
        fini = ('agx_win32_stream_scratch_fini(&ctx);'
                if 'agx_win32_stream_scratch_fini(ctx);' in pipe_c else '')
        code = (HARNESS.replace('@@SPARE@@', '\n'.join(spare))
                .replace('@@HEADER@@', f'#include "{header}"' if header.exists() else '')
                .replace('@@POOL@@', pool)
                .replace('@@INIT@@', init)
                .replace('@@DRAW@@', draw)
                .replace('@@CLEANUP@@', '  (void)ctx;\n' + cleanup)
                .replace('@@RC89@@', main)
                .replace('@@FINI@@', fini)
                .replace('stream_draw(batch, 60000u,', 'stream_draw(batch, %uu,' % max_count))
        code = ''.join('#define %s 1\n' % d for d in defines) + code
        src = Path(tmp) / 'harness.c'
        exe = Path(tmp) / 'harness'
        src.write_text(code)
        built = subprocess.run([os.environ.get('CC', 'clang'), '-std=gnu11', '-Wall', '-Wno-unused-function',
                                '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                               capture_output=True, text=True)
        self.assertEqual(built.returncode, 0, built.stderr[-4000:])
        ran = subprocess.run([str(exe)], capture_output=True, text=True)
        self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr[-3000:])
        self.assertIn('RC71 stream scratch: PASS', ran.stdout)
        return ran.stdout


class StreamScratch(unittest.TestCase):
    def test_stream_outputs_are_reused_after_their_batch_retires(self):
        run_stream_harness(self)


if __name__ == '__main__':
    unittest.main()
