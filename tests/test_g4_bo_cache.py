"""EXP996: the Windows winsys keeps released native BOs for reuse.

EXP994 DWM profile: ~460 freshly created native BOs, each paying AllocateCb,
VA reserve, a mapping and residency paging-fence wait (2553 waits, 55.7 s of
251 s). Mesa's agx_bo_cache is bypassed because the winsys replaces
agx_bo_create. Invariants of the replacement cache:
- an equal request (rounded size, flags, buffer class) reuses a released BO;
- imported BOs, BOs named by an in-flight submission (GpuvaSpace.Held), and
  requests beyond the entry/byte limits are never cached or handed out;
- flushing disposes every cached BO; a refused dispose leaves the BO for
  AgxWin32AsahiCollect (refcnt 0, no longer cached).
"""
from pathlib import Path
import re
import subprocess
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
SRC = ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_asahi_bo.c'
HDR = ROOT / 'drivers/apple-agx/mesa/winsys/agx_win32_asahi_bo.h'


def function(text, name):
    m = re.search(r'(?m)^static [A-Za-z0-9_ *]+?\b' + name + r'\([^;{]*\)\s*\{', text)
    if not m:
        return ''
    start = text.index('{', m.start()); depth = 1; end = start + 1
    while depth:
        depth += (text[end] == '{') - (text[end] == '}'); end += 1
    return text[m.start():end]


BODY = r'''
#include <assert.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
typedef unsigned long long APPLE_AGX_U64; typedef unsigned APPLE_AGX_U32;
@@LIMITS@@
struct agx_bo { size_t size; unsigned flags; int refcnt; };
typedef struct { uint64_t Allocation, Va, Bytes; unsigned Bound; } AGX_WIN32_GPUVA_BO;
typedef struct { uint64_t *Held; unsigned HeldCount; } AGX_WIN32_GPUVA_SPACE;
typedef struct { int Failed, Closing; AGX_WIN32_GPUVA_SPACE Gpuva;
  void *Cache[AGX_WIN32_BO_CACHE_LIMIT]; APPLE_AGX_U32 CacheCount; APPLE_AGX_U64 CacheBytes; } AGX_WIN32_ASAHI_BACKEND;
struct windows_bo { struct agx_bo Base; struct { struct { unsigned ClassId; } Buffer; } Backing;
  AGX_WIN32_ASAHI_BACKEND *Backend; AGX_WIN32_GPUVA_BO Gpuva; int Imported, Cached; };
static int disposed, refuse;
static int dispose(struct windows_bo *bo) { (void)bo; if(refuse) return 0; ++disposed; return 1; }
@@FUNCS@@
static struct windows_bo *mk(AGX_WIN32_ASAHI_BACKEND *b,size_t size,unsigned flags,unsigned cls,uint64_t alloc) {
  struct windows_bo *bo=calloc(1,sizeof(*bo)); bo->Backend=b; bo->Base.size=size; bo->Base.flags=flags;
  bo->Backing.Buffer.ClassId=cls; bo->Gpuva.Allocation=alloc; bo->Gpuva.Bound=1; return bo; }
int main(void) {
  AGX_WIN32_ASAHI_BACKEND b; memset(&b,0,sizeof b);
  struct windows_bo *a=mk(&b,0x40000,0,1,11), *e=mk(&b,0x80000,0,3,12), *imp=mk(&b,0x40000,0,1,13);
  imp->Imported=1;
  assert(cache_put(a) && cache_put(e) && !cache_put(imp));
  assert(b.CacheCount==2 && b.CacheBytes==0xc0000 && a->Cached);
  /* Different size, flags or class do not match. */
  assert(!cache_take(&b,0x50000,0,1) && !cache_take(&b,0x40000,1,1) && !cache_take(&b,0x40000,0,2));
  /* A BO still named by the in-flight submission is not handed out. */
  uint64_t held[1]={11}; b.Gpuva.Held=held; b.Gpuva.HeldCount=1;
  assert(!cache_take(&b,0x40000,0,1));
  b.Gpuva.Held=NULL; b.Gpuva.HeldCount=0;
  assert(cache_take(&b,0x40000,0,1)==a && !a->Cached && b.CacheCount==1 && b.CacheBytes==0x80000);
  /* Limits: a failed or closing backend, or a full byte budget, refuses. */
  b.Closing=1; assert(!cache_put(a)); b.Closing=0;
  struct windows_bo *huge=mk(&b,AGX_WIN32_BO_CACHE_BYTES,0,1,14); assert(!cache_put(huge));
  for(unsigned i=b.CacheCount;i<AGX_WIN32_BO_CACHE_LIMIT;++i) assert(cache_put(mk(&b,0x10000,0,1,100+i)));
  assert(!cache_put(a));
  /* Flush disposes everything; a refused dispose leaves it uncached. */
  refuse=1; struct windows_bo *last=(struct windows_bo *)b.Cache[b.CacheCount-1];
  cache_flush(&b);
  assert(b.CacheCount==0 && b.CacheBytes==0 && disposed==0 && !last->Cached);
  refuse=0;
  for(unsigned i=0;i<4;++i) assert(cache_put(mk(&b,0x10000,0,1,200+i)));
  cache_flush(&b); assert(disposed==4 && b.CacheCount==0);
  puts("EXP996 bo cache: PASS");
  return 0;
}
'''


class BoCache(unittest.TestCase):
    def test_cache_reuse_and_limits(self):
        text = SRC.read_text()
        funcs = '\n'.join(function(text, n) for n in
                          ('cache_in_flight', 'cache_put', 'cache_take', 'cache_flush'))
        self.assertIn('cache_take', funcs, 'winsys BO cache is missing')
        limits = '\n'.join(l for l in HDR.read_text().splitlines()
                           if l.startswith('#define AGX_WIN32_BO_CACHE_'))
        body = BODY.replace('@@FUNCS@@', funcs).replace('@@LIMITS@@', limits)
        with tempfile.TemporaryDirectory() as tmp:
            src = Path(tmp) / 'replay.c'; exe = Path(tmp) / 'replay'
            src.write_text(body)
            built = subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra',
                                    '-fsanitize=address,undefined', str(src), '-o', str(exe)],
                                   text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout + built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout + ran.stderr)
            self.assertIn('EXP996 bo cache: PASS', ran.stdout)

    def test_create_unreference_collect_detach_use_cache(self):
        text = SRC.read_text()
        create = text[text.index('struct agx_bo *agx_bo_create('):]
        self.assertLess(create.index('cache_take('), create.index('AgxWin32NativeDeviceCreateBo('))
        unref = text[text.index('void agx_bo_unreference('):]
        self.assertLess(unref.index('cache_put('), unref.index('dispose(bo)'))
        collect = text[text.index('int AgxWin32AsahiCollect('):text.index('int AgxWin32AsahiDetach(')]
        self.assertIn('!bo->Cached', collect)
        detach = text[text.index('int AgxWin32AsahiDetach('):]
        self.assertLess(detach.index('cache_flush(b)'), detach.index('AgxWin32AsahiCollect(b)'))


if __name__ == '__main__':
    unittest.main()
