"""EXP1207: batch pools and encoders are recorded into cached shadows.

EXP1206: under x86 emulation a scalar store into the write-combined view of a
Mesa allocation costs ~200 ns (20 MB/s vs 1.3 GB/s cached; memcpy into WC
15 GB/s), and Asahi packs descriptors, PPP/USC state and the VDM stream word by
word into the batch pools and the encoder (EXP1202: ~12 us per draw in
agx_encode_state). The GPUVA winsys gives those BOs a cached CPU map and copies
what the batch wrote into the allocation before the submission names it.

Invariants (a violation loses GPU-visible state or redirects a resource):
- only an AGX_WIN32_BO_SHADOW BO gets a shadow map; any other BO keeps the
  allocation's view, and publishing it is a no-op;
- nothing written through a shadow map reaches the allocation before the
  publish, which copies exactly the requested prefix (clamped to the size);
- the shadow survives with the BO (BO cache reuse keeps the CPU map);
- the batch publishes the encoder up to its current pointer plus the
  terminator written past it (the whole encoder once the stream continued in
  a pool chunk), the transient slab of each pool up to its offset and the
  earlier slabs whole, and does so before AgxWin32GpuvaSubmit;
- the GPUVA batch pools and the encoder are created with the flag; the D3D10
  profile keeps write-through maps.
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
WINSYS = ROOT / 'drivers/apple-agx/mesa/winsys'
BO = WINSYS / 'agx_win32_asahi_bo.c'
BO_H = WINSYS / 'agx_win32_asahi_bo.h'
BATCH = WINSYS / 'agx_win32_gpuva_batch.c'
GENERATOR = ROOT / 'drivers/apple-agx/mesa/scripts/native-asahi-batch-lifecycle.py'
MESA = Path('/Users/pavel/public_windows/.local/reference/mesa')


def function(text, name):
    m = re.search(r'(?m)^(?:static )?[A-Za-z0-9_ *]+?\b' + name + r'\([^;{]*\)\s*\{', text)
    if not m:
        return ''
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]


def build_and_run(test, program, marker):
    with tempfile.TemporaryDirectory() as tmp:
        src = Path(tmp) / 'shadow.c'; exe = Path(tmp) / 'shadow'
        src.write_text(program)
        built = subprocess.run(['clang', '-std=c11', '-D_POSIX_C_SOURCE=200809L', '-Wall', '-Wextra',
                                '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                                str(src), '-o', str(exe)], text=True, capture_output=True)
        test.assertEqual(built.returncode, 0, built.stdout + built.stderr)
        ran = subprocess.run([str(exe)], text=True, capture_output=True)
        test.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
        test.assertIn(marker, ran.stdout)


MAP_PRELUDE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define APPLE_AGX_GPUVA_WINSYS 1
@@FLAG@@
enum { AgxWin32NativeBoSuccess = 0, AppleAgxWin32BufferCpuWrite = 2 };
struct agx_device { void *windows_private; };
struct agx_bo { unsigned flags; size_t size; void *_map; int refcnt; };
typedef struct { int Failed; struct { void *Screen; } Buffers; int Gpuva; } AGX_WIN32_ASAHI_BACKEND;
typedef struct { void *CpuAddress; } AGX_WIN32_NATIVE_BO;
struct windows_bo { struct agx_bo Base; AGX_WIN32_NATIVE_BO Backing; AGX_WIN32_ASAHI_BACKEND *Backend;
  struct { uint64_t Allocation; } Gpuva; void *Shadow; };
#define AGX_WIN32_ASAHI_FAIL(b, site) ((b)->Failed = (int)(site))
static unsigned char device_view[4][0x4000];
static unsigned maps;
static uint64_t AgxWin32GpuvaHoldingFence(int *space, uint64_t allocation) { (void)space; (void)allocation; return 0; }
static int AgxWin32GpuvaRetire(int *space, uint64_t fence) { (void)space; (void)fence; return 1; }
static int AgxWin32NativeBoMap(void *screen, AGX_WIN32_NATIVE_BO *backing, unsigned access, void **address) {
  (void)screen; assert(access == AppleAgxWin32BufferCpuWrite && !backing->CpuAddress && maps < 4);
  backing->CpuAddress = *address = device_view[maps++]; return AgxWin32NativeBoSuccess;
}
'''

MAP_MAIN = r'''
static struct windows_bo *mk(AGX_WIN32_ASAHI_BACKEND *b, unsigned flags, size_t size) {
  struct windows_bo *bo = calloc(1, sizeof(*bo));
  bo->Backend = b; bo->Base.flags = flags; bo->Base.size = size; bo->Base.refcnt = 1; return bo;
}
int main(void) {
  AGX_WIN32_ASAHI_BACKEND b = {0};
  struct agx_device dev = { &b };
  /* A plain BO keeps the allocation's view; publishing it changes nothing. */
  struct windows_bo *plain = mk(&b, 0, 0x4000);
  native_map(&dev, &plain->Base, NULL);
  assert(plain->Base._map == device_view[0] && !plain->Shadow);
  memset(plain->Base._map, 0x11, 16);
  assert(AgxWin32AsahiShadowPublish(&plain->Base, 0x4000) && device_view[0][0] == 0x11);
  /* A shadow BO never CPU-mapped publishes nothing. */
  struct windows_bo *pool = mk(&b, AGX_WIN32_BO_SHADOW | 2u, 0x4000);
  assert(AgxWin32AsahiShadowPublish(&pool->Base, 0x4000));
  native_map(&dev, &pool->Base, NULL);
  assert(!b.Failed && pool->Shadow && pool->Base._map == pool->Shadow &&
         pool->Backing.CpuAddress == device_view[1] && pool->Base._map != device_view[1]);
  assert(((uintptr_t)pool->Shadow & 4095u) == 0);
  /* Nothing reaches the allocation before the publish. */
  memset(pool->Base._map, 0xa5, 0x4000);
  assert(device_view[1][0] == 0 && device_view[1][0x3fff] == 0);
  /* The publish copies exactly the requested prefix. */
  assert(AgxWin32AsahiShadowPublish(&pool->Base, 0x100));
  assert(device_view[1][0] == 0xa5 && device_view[1][0xff] == 0xa5 && device_view[1][0x100] == 0);
  /* ... clamped to the BO. */
  assert(AgxWin32AsahiShadowPublish(&pool->Base, 0x10000) && device_view[1][0x3fff] == 0xa5);
  /* A shadow map that is no longer the shadow (or lost its view) refuses. */
  void *keep = pool->Base._map; pool->Base._map = device_view[1];
  assert(!AgxWin32AsahiShadowPublish(&pool->Base, 0x10));
  pool->Base._map = keep; void *view = pool->Backing.CpuAddress; pool->Backing.CpuAddress = NULL;
  assert(!AgxWin32AsahiShadowPublish(&pool->Base, 0x10));
  pool->Backing.CpuAddress = view;
  /* Reuse after the map was dropped: a new view, the same shadow. */
  void *shadow = pool->Shadow; pool->Base._map = NULL; pool->Backing.CpuAddress = NULL;
  native_map(&dev, &pool->Base, NULL);
  assert(pool->Shadow == shadow && pool->Base._map == shadow && pool->Backing.CpuAddress == device_view[2]);
  shadow_free(pool->Shadow); free(pool); free(plain);
  puts("EXP1207 shadow map: PASS");
  return 0;
}
'''

BATCH_PRELUDE = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <string.h>
@@TAIL@@
struct agx_bo { size_t size; void *_map; };
struct util_dynarray { void *data; unsigned size; };
#define util_dynarray_foreach(buf, type, elem) \
  for (type *elem = (type *)(buf)->data; elem < (type *)((char *)(buf)->data + (buf)->size); elem++)
struct agx_pool { struct util_dynarray bos; struct agx_bo *transient_bo; unsigned transient_offset; };
struct agx_encoder { struct agx_bo *bo; uint8_t *current, *end; };
struct agx_batch { struct agx_encoder vdm; struct agx_pool pool, pipeline_pool; };
static void *agx_bo_map(struct agx_bo *bo) { return bo->_map; }
static struct { struct agx_bo *bo; size_t bytes; } calls[16];
static unsigned ncalls, refuse_at = 99;
static int AgxWin32AsahiShadowPublish(struct agx_bo *bo, size_t bytes) {
  calls[ncalls].bo = bo; calls[ncalls].bytes = bytes; return ncalls++ != refuse_at;
}
'''

BATCH_MAIN = r'''
int main(void) {
  static uint8_t enc_map[0x80000], pool_map[2][0x40000], low_map[0x40000];
  struct agx_bo enc = { sizeof enc_map, enc_map }, p0 = { 0x40000, pool_map[0] },
                p1 = { 0x40000, pool_map[1] }, low = { 0x40000, low_map };
  struct agx_bo *pools[2] = { &p0, &p1 }, *lows[1] = { &low };
  struct agx_batch batch = {0};
  batch.vdm.bo = &enc; batch.vdm.current = enc_map + 0x1234; batch.vdm.end = enc_map + sizeof enc_map;
  batch.pool.bos.data = pools; batch.pool.bos.size = sizeof pools;
  batch.pool.transient_bo = &p1; batch.pool.transient_offset = 0x2a0;
  batch.pipeline_pool.bos.data = lows; batch.pipeline_pool.bos.size = sizeof lows;
  batch.pipeline_pool.transient_bo = &low; batch.pipeline_pool.transient_offset = 0x40;
  assert(publish_shadows(&batch) && ncalls == 4);
  /* encoder: current pointer plus the terminator written past it */
  assert(calls[0].bo == &enc && calls[0].bytes == 0x1234 + AGX_WIN32_ENCODER_TAIL);
  /* the earlier slab whole, the transient slab up to its offset */
  assert(calls[1].bo == &p0 && calls[1].bytes == 0x40000);
  assert(calls[2].bo == &p1 && calls[2].bytes == 0x2a0);
  assert(calls[3].bo == &low && calls[3].bytes == 0x40);
  /* The stream continued in a pool chunk: the encoder is published whole. */
  ncalls = 0; batch.vdm.current = pool_map[1] + 0x100;
  assert(publish_shadows(&batch) && calls[0].bo == &enc && calls[0].bytes == sizeof enc_map);
  /* A refused publish stops before the submission. */
  ncalls = 0; refuse_at = 2;
  assert(!publish_shadows(&batch) && ncalls == 3);
  puts("EXP1207 batch publish: PASS");
  return 0;
}
'''


class ShadowPools(unittest.TestCase):
    def test_shadow_map_and_publish(self):
        text = BO.read_text()
        funcs = '\n'.join(function(text, n) for n in
                          ('shadow_alloc', 'shadow_free', 'AgxWin32AsahiShadowPublish', 'native_map'))
        for name in ('shadow_alloc', 'AgxWin32AsahiShadowPublish', 'native_map'):
            self.assertIn(name + '(', funcs, name + ' missing')
        flag = re.search(r'(?m)^#define AGX_WIN32_BO_SHADOW .*$', BO_H.read_text())
        self.assertIsNotNone(flag)
        program = MAP_PRELUDE.replace('@@FLAG@@', flag.group(0)) + funcs + MAP_MAIN
        build_and_run(self, program, 'EXP1207 shadow map: PASS')

    def test_batch_publishes_before_submit(self):
        text = BATCH.read_text()
        funcs = function(text, 'pool_extent') + '\n' + function(text, 'publish_shadows')
        self.assertIn('publish_shadows(', funcs)
        tail = re.search(r'(?m)^#define AGX_WIN32_ENCODER_TAIL .*$', BO_H.read_text())
        self.assertIsNotNone(tail)
        build_and_run(self, BATCH_PRELUDE.replace('@@TAIL@@', tail.group(0)) + funcs + BATCH_MAIN,
                      'EXP1207 batch publish: PASS')
        finish = function(text, 'AgxWin32AsahiBatchFinish')
        self.assertLess(finish.index('util_dynarray_foreach(&batch->pipeline_pool.bos'),
                        finish.index('publish_shadows(batch)'))
        self.assertLess(finish.index('publish_shadows(batch)'), finish.index('AgxWin32GpuvaSubmit('))

    def test_encoder_tail_covers_flush_terminator(self):
        tail = int(re.search(r'#define AGX_WIN32_ENCODER_TAIL (0x[0-9a-f]+)u', BO_H.read_text()).group(1), 16)
        pipe = MESA / 'src/gallium/drivers/asahi/agx_pipe.c'
        stop = 5 + 64
        if pipe.exists():
            m = re.search(r'uint8_t stop\[(\d+) \+ (\d+)\]', pipe.read_text())
            self.assertIsNotNone(m, 'agx_flush_render terminator changed')
            stop = int(m.group(1)) + int(m.group(2))
        self.assertGreaterEqual(tail, stop)

    def test_flag_wiring(self):
        text = BO.read_text()
        create = text[text.index('struct agx_bo *agx_bo_create('):]
        allowed = create[:create.index(';')]
        self.assertIn('#ifdef APPLE_AGX_GPUVA_WINSYS\n      |AGX_WIN32_BO_SHADOW', allowed)
        encoder = function(text, 'AgxWin32AsahiEncoderCreate')
        self.assertIn('#ifdef APPLE_AGX_GPUVA_WINSYS\n  bo=agx_bo_create(native,bytes,align,AGX_WIN32_BO_SHADOW,label);\n#else\n  bo=agx_bo_create(native,bytes,align,0,label);', encoder)
        dispose = function(text, 'dispose')
        self.assertLess(dispose.index('shadow_free(bo->Shadow)'), dispose.index('free(bo);'))
        gen = GENERATOR.read_text()
        self.assertIn('agx_pool_init(&batch->pool, dev, "Batch pool", AGX_WIN32_BO_SHADOW, true);', gen)
        self.assertIn('AGX_BO_LOW_VA | AGX_WIN32_BO_SHADOW, true);', gen)
        # the internal-program cache pool and the D3D10 profile keep write-through maps
        self.assertIn('#else\n   agx_pool_init(&batch->pool, dev, "Batch pool", 0, true);', gen)


if __name__ == '__main__':
    unittest.main()
