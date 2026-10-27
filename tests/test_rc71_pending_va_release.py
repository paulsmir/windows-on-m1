"""RC71 (EXP1990 arm b): BO releases while the GPU is never idle.

WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?
A released BO whose VA cannot be freed yet (a submission is in flight) parks
the VA in Backend.PendingVa (64 entries). Only AgxWin32AsahiCollect drained
it, only with no submission in flight, and the D3D12 owner calls Collect only
when a presentation is created or destroyed. GravityMark with the GPU stream
(3125 cache evictions in 78 s, ~4 submissions in flight) filled the list for
good; every later release was refused, so its allocation and VA stayed
registered, the process kept trimming residency, and a USC-window BO bind
finally failed (reject-batch {4, 0x101f1}: agx_win32_asahi_bo.c:497,
AgxWin32GpuvaBind -> backend Failed -> 0x887a0006).

Invariants, with the real dispose() and the real GPU VA space
(agx_win32_gpuva.c) under a GPU that always has work in flight:
- every release destroys its allocation (none is refused);
- each VA is freed only after every submission that was in flight at its
  release has retired, never while one of them can still read it;
- the deferred list stays bounded by the in-flight window, and every VA is
  freed exactly once.
"""
from pathlib import Path
import os
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys'
SRC = WINSYS / 'agx_win32_asahi_bo.c'


def function(text, name):
    m = re.search(r'(?m)^(?:static )?[A-Za-z0-9_ *]+?\b' + name + r'\([^;{]*\)\s*\{', text)
    if not m:
        return ''
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]


HARNESS = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "agx_win32_gpuva.h"
#define APPLE_AGX_GPUVA_WINSYS 1
typedef unsigned long long APPLE_AGX_U64; typedef unsigned APPLE_AGX_U32;
struct agx_bo { int refcnt; void *_map; };
typedef struct { void *CpuAddress; APPLE_AGX_U64 ConstructionSerial;
  struct { struct { APPLE_AGX_U64 Token; } Transport; } Buffer; } AGX_WIN32_NATIVE_BO;
typedef struct AGX_WIN32_ASAHI_BACKEND AGX_WIN32_ASAHI_BACKEND;
struct AGX_WIN32_ASAHI_BACKEND {
  struct { void *Screen; } Buffers;
  struct {
    int (*Associate)(void *, APPLE_AGX_U64, const void *, APPLE_AGX_U64, int (*)(const void *, const void *, int));
    int (*Detach)(void *, APPLE_AGX_U64, const void *, APPLE_AGX_U64);
    const struct agx_bo *(*NextBo)(void *, APPLE_AGX_U32 *);
  } Ops;
  void *Owner, *Native; int Failed;
  AGX_WIN32_GPUVA_SPACE Gpuva;
  AGX_WIN32_GPUVA_BO PendingVa[64]; APPLE_AGX_U32 PendingVaCount;
  APPLE_AGX_U64 PendingVaFence[64]; int RefusedRelease;
  APPLE_AGX_U32 LiveBos;
};
struct windows_bo { struct agx_bo Base; AGX_WIN32_NATIVE_BO Backing; AGX_WIN32_ASAHI_BACKEND *Backend;
  AGX_WIN32_GPUVA_BO Gpuva; int Virtual, Cached; void *Shadow; int Registered; };
#define AGX_WIN32_ASAHI_FAIL(backend, file) ((backend)->Failed = 1)
enum { AgxWin32NativeBoSuccess, AgxWin32NativeDeviceSuccess = 0, AgxWin32NativeDeviceCallback };
static int AgxWin32NativeBoUnmap(void *s, AGX_WIN32_NATIVE_BO *n) { (void)s; (void)n; return 0; }
static unsigned destroyed[400];
static int va_index(uint64_t va) { return (int)((va - 0x100000000ull) >> 24); }
static int AgxWin32NativeDeviceDestroyBo(void *buffers, AGX_WIN32_NATIVE_BO *n) {
  (void)buffers; ++destroyed[va_index(((struct windows_bo *)((char *)n - offsetof(struct windows_bo, Backing)))->Gpuva.Va)];
  return 0; }
static void virtual_release(struct windows_bo *bo) { (void)bo; assert(!"virtual"); }
static void handle_map_remove(AGX_WIN32_ASAHI_BACKEND *b, struct windows_bo *bo) { (void)b; (void)bo; }
static void shadow_free(void *p) { (void)p; }
static int release_map(const void *k, const void *e, int c) { (void)k; (void)e; (void)c; return 1; }
#define BOS 400u
_Static_assert(BOS <= sizeof(destroyed) / sizeof(destroyed[0]), "harness size");
static struct windows_bo *all[BOS]; static unsigned made, released;
static int detach(void *o, APPLE_AGX_U64 t, const void *bo, APPLE_AGX_U64 s) {
  (void)o; (void)t; (void)s; ((struct windows_bo *)bo)->Registered = 0; return 1; }
static int associate(void *o, APPLE_AGX_U64 t, const void *bo, APPLE_AGX_U64 s,
                     int (*r)(const void *, const void *, int)) {
  (void)o; (void)t; (void)s; (void)r; ((struct windows_bo *)bo)->Registered = 1; return 1; }
static const struct agx_bo *next_bo(void *o, APPLE_AGX_U32 *cursor) {
  (void)o;
  while (*cursor < made) { struct windows_bo *bo = all[(*cursor)++]; if (bo && bo->Registered) return &bo->Base; }
  return NULL; }
/* GPU VA space callbacks: one VA per BO; Free must never take a range that an
 * in-flight submission can still read. */
static AGX_WIN32_GPUVA_SPACE *space_ptr; static unsigned frees[BOS]; static uint64_t next_fence = 100;
static int op_free(void *c, uint64_t va, uint64_t bytes) {
  (void)c; (void)bytes; int i = va_index(va); assert(i >= 0 && (unsigned)i < made);
  assert(!AgxWin32GpuvaHoldingFence(space_ptr, 1000u + (unsigned)i)); /* nothing in flight reads it */
  ++frees[i]; return 1; }
static int op_one(void *c) { (void)c; return 1; }
static int op_reserve(void *c, uint64_t a, uint64_t b, uint64_t d, uint64_t *va) { (void)c; (void)a; (void)b; (void)d; (void)va; return 0; }
static int op_map(void *c, uint64_t a, uint64_t b, uint64_t d, unsigned e, uint64_t *f) { (void)c; (void)a; (void)b; (void)d; (void)e; (void)f; return 0; }
static int op_resident(void *c, const uint64_t *h, unsigned n, uint64_t *f) { (void)h; (void)n; *f = 0; return op_one(c); }
static int op_wait(void *c, uint64_t f) { (void)f; return op_one(c); }
static int op_submit(void *c, const uint64_t *w, unsigned n, uint64_t va, uint32_t bytes, const void *p,
                     uint32_t pb, uint64_t *fence) { (void)c; (void)w; (void)n; (void)va; (void)bytes; (void)p; (void)pb;
  *fence = ++next_fence; return 1; }
static int op_evict(void *c, const uint64_t *h, unsigned n) { (void)c; (void)h; (void)n; return 1; }
static void harness_free(void *p) {
  for (unsigned i = 0; p && i < made; ++i) if (all[i] == p) all[i] = NULL;
  free(p); }
#define free(p) harness_free(p)
@@FUNCS@@
#undef free
#ifndef HAVE_DRAIN
static void AgxWin32AsahiDrainPendingVa(AGX_WIN32_ASAHI_BACKEND *b) { (void)b; } /* pre-RC71: no drain */
#endif
static uint64_t inflight[8]; static unsigned inflight_count;
static void retire_oldest(AGX_WIN32_ASAHI_BACKEND *b) {
  assert(inflight_count && AgxWin32GpuvaRetire(&b->Gpuva, inflight[0]));
  memmove(&inflight[0], &inflight[1], --inflight_count * sizeof(inflight[0]));
}
int main(void) {
  static AGX_WIN32_ASAHI_BACKEND b; int marker = 1;
  AGX_WIN32_GPUVA_OPS ops = { op_reserve, op_map, op_free, op_resident, op_wait, op_submit, op_wait, op_evict,
                              NULL, NULL, NULL, NULL };
  assert(AgxWin32GpuvaInit(&b.Gpuva, &ops, &marker)); space_ptr = &b.Gpuva;
  b.Ops.Associate = associate; b.Ops.Detach = detach; b.Ops.NextBo = next_bo; b.Native = &marker;
  AGX_WIN32_GPUVA_BO command = { 999, 0x10000, 0x10000, 1 };
  unsigned max_pending = 0;
  for (unsigned k = 0; k < BOS; ++k) {
    struct windows_bo *bo = calloc(1, sizeof(*bo)); assert(bo);
    bo->Backend = &b; bo->Base.refcnt = 0; bo->Registered = 1;
    bo->Gpuva = (AGX_WIN32_GPUVA_BO){ 1000u + k, 0x100000000ull + ((uint64_t)k << 24), 0x800000, 1 };
    all[made++] = bo; ++b.LiveBos;
    /* A submission reads this BO; the GPU keeps three in flight. */
    if (inflight_count == AGX_GPUVA_IN_FLIGHT - 1u) retire_oldest(&b);
    const AGX_WIN32_GPUVA_BO *refs[2] = { &command, &bo->Gpuva }; uint64_t done = 0;
    assert(AgxWin32GpuvaSubmit(&b.Gpuva, refs, 2, &command, 64, NULL, 0, &marker, 4, &done) && done);
    inflight[inflight_count++] = done;
    /* The application releases it while that submission is in flight. */
    if (dispose(bo)) ++released;
    assert(!b.Failed);
    if (b.PendingVaCount > max_pending) max_pending = b.PendingVaCount;
  }
  while (inflight_count) retire_oldest(&b);
  AgxWin32AsahiDrainPendingVa(&b); /* as the next agx_bo_create does */
  unsigned destroyed_count = 0, freed_once = 0;
  for (unsigned i = 0; i < made; ++i) { destroyed_count += destroyed[i] == 1; freed_once += frees[i] == 1; }
  printf("released=%u destroyed=%u freed_once=%u pending=%u max_pending=%u refused=%d\n",
         released, destroyed_count, freed_once, b.PendingVaCount, max_pending, b.RefusedRelease);
  assert(released == BOS && destroyed_count == BOS);
  assert(freed_once == BOS && b.PendingVaCount == 0 && b.LiveBos == 0);
  assert(max_pending <= 2u * AGX_GPUVA_IN_FLIGHT);
  puts("RC71 pending VA release: PASS");
  return 0;
}
'''


class PendingVaRelease(unittest.TestCase):
    def test_busy_gpu_releases_every_bo_and_frees_each_va_after_its_fence(self):
        text = SRC.read_text()
        names = ['pending_va_free_retired', 'pending_va_push', 'dispose', 'AgxWin32AsahiDrainPendingVa']
        funcs = []
        funcs.append('#define AGX_WIN32_PENDING_VA_LIMIT 64u')
        for name in names:
            body = function(text, name)
            if body and name == 'AgxWin32AsahiDrainPendingVa':
                funcs.append('#define HAVE_DRAIN 1')
            funcs.append(body)
        self.assertIn('static int dispose(', '\n'.join(funcs))
        code = HARNESS.replace('@@FUNCS@@', '\n'.join(funcs))
        # The harness models the native BO without the offset-only members.
        code = code.replace('#include <string.h>', '#include <string.h>\n#include <stddef.h>', 1)
        with tempfile.TemporaryDirectory(prefix='rc71-pending-va-') as tmp:
            src = Path(tmp) / 'harness.c'
            exe = Path(tmp) / 'harness'
            src.write_text(code)
            built = subprocess.run([os.environ.get('CC', 'clang'), '-std=c11', '-Wall', '-Wno-unused-function',
                                    '-fsanitize=address,undefined', '-I', str(WINSYS),
                                    '-I', str(ROOT / 'drivers/apple-agx/shared/include'),
                                    str(src), str(WINSYS / 'agx_win32_gpuva.c'), '-o', str(exe)],
                                   capture_output=True, text=True)
            self.assertEqual(built.returncode, 0, built.stderr[-4000:])
            ran = subprocess.run([str(exe)], capture_output=True, text=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr[-3000:])
            self.assertIn('RC71 pending VA release: PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
