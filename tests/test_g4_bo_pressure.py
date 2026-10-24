"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

The Windows winsys returns allocation failure while retaining unused BO cache
storage, unlike Asahi's pressure recovery. Compile its real allocation
transaction and cache functions: reclaim only idle cache entries, retry once
only after actual release, and preserve rollback/in-flight ownership.
"""
from pathlib import Path
import subprocess
import tempfile
import unittest

from test_g4_bo_cache import BODY, HDR, SRC, function


class BoPressure(unittest.TestCase):
    def test_allocation_pressure_reclaims_idle_cache_once(self):
        text = SRC.read_text()
        # Exact production creation transaction, before publishing Base/VA/owner.
        start = text.index('  {\n  AGX_WIN32_NATIVE_DEVICE_RESULT created=')
        end = text.index('  bo->Base.dev=native;', start)
        transaction = text[start:end]
        prefix = BODY[:BODY.index('@@FUNCS@@')]
        prefix = prefix.replace('int Failed, Closing;',
                                'int Failed, Closing, Buffers; void *UnpublishedBo;')
        prefix = prefix.replace('unsigned ClassId; } Buffer; } Backing;',
                                'unsigned ClassId; } Buffer; int Live; } Backing;')
        prefix = prefix.replace('static int disposed, refuse;',
                                'static int disposed, refuse, fail_dispose;')
        prefix = prefix.replace('(void)bo; if(refuse) return 0;',
                                'if(fail_dispose) bo->Backend->Failed=1; if(refuse) return 0;')
        limits = '\n'.join(l for l in HDR.read_text().splitlines()
                           if l.startswith('#define AGX_WIN32_BO_CACHE_'))
        prefix = prefix.replace('@@LIMITS@@', limits)
        funcs = '\n'.join(function(text, n) for n in (
            'cache_in_flight', 'cache_evict_oldest', 'cache_put', 'cache_take',
            'cache_flush', 'cache_reclaim'))
        program = prefix + funcs + r'''
#define APPLE_AGX_GPUVA_WINSYS 1
#define AGX_WIN32_ASAHI_FAIL(b, site) ((b)->Failed = (int)(site))
static void (*AgxWin32BatchRefusalHook)(unsigned,unsigned,unsigned,unsigned);
typedef enum { AgxWin32NativeDeviceSuccess, AgxWin32NativeDeviceArgument,
  AgxWin32NativeDeviceState, AgxWin32NativeDeviceRange, AgxWin32NativeDeviceStale,
  AgxWin32NativeDeviceCallback } AGX_WIN32_NATIVE_DEVICE_RESULT;
static int calls, retained, fail_again;
static AGX_WIN32_NATIVE_DEVICE_RESULT first;
static AGX_WIN32_NATIVE_DEVICE_RESULT AgxWin32NativeDeviceCreateBo(
    int *device, unsigned cls, size_t bytes, unsigned align, unsigned access,
    void *backing) {
  (void)device; (void)backing;
  assert(cls==1 && bytes==0x730000 && align==0x10000 && access==0x2c);
  ++calls;
  if(calls==1 || fail_again) return first;
  assert(disposed>0); /* Capacity comes from actual destruction, not uncache. */
  return AgxWin32NativeDeviceSuccess;
}
static struct windows_bo *attempt(AGX_WIN32_ASAHI_BACKEND *b) {
  const unsigned cls=1, align=0x10000, access=0x2c;
  const size_t bytes=0x730000;
  struct windows_bo *bo=calloc(1,sizeof(*bo));
  assert(bo); bo->Backend=b; bo->Backing.Live=retained;
@@TRANSACTION@@
  return bo;
}
static struct windows_bo entries[4];
static void setup(AGX_WIN32_ASAHI_BACKEND *b, unsigned count) {
  memset(b,0,sizeof(*b)); memset(entries,0,sizeof(entries));
  calls=disposed=refuse=fail_dispose=retained=fail_again=0;
  first=AgxWin32NativeDeviceCallback;
  for(unsigned i=0;i<count;++i) {
    entries[i].Backend=b; entries[i].Base.size=0x100000;
    entries[i].Gpuva.Bound=1; entries[i].Gpuva.Allocation=10+i;
    entries[i].Backing.Buffer.ClassId=1;
    assert(cache_put(&entries[i]));
  }
}
int main(void) {
  AGX_WIN32_ASAHI_BACKEND b;
  /* Clean allocation failure with idle resources: release before retry. */
  setup(&b,2);
  struct windows_bo *newbo=attempt(&b);
  assert(newbo && calls==2 && disposed==2 && !b.CacheCount && !b.CacheBytes);
  assert(!b.Failed && !b.UnpublishedBo); free(newbo);
  /* Ordinary success keeps reuse, and exact cache hits allocate nothing. */
  setup(&b,2); first=AgxWin32NativeDeviceSuccess;
  newbo=attempt(&b); assert(newbo && calls==1 && !disposed && b.CacheCount==2); free(newbo);
  assert(cache_take(&b,0x100000,0,1)==&entries[1] && calls==1 && !disposed);
  /* Current + older submissions retain their entries and release order. */
  setup(&b,4); uint64_t current=10, older=12;
  b.Gpuva.Held=&current; b.Gpuva.HeldCount=1;
  b.Gpuva.Older[0].Handles=&older; b.Gpuva.Older[0].Count=1; b.Gpuva.OlderCount=1;
  newbo=attempt(&b); assert(newbo && calls==2 && disposed==2); free(newbo);
  assert(b.CacheCount==2 && b.CacheBytes==0x200000);
  assert(b.Cache[0]==&entries[0] && b.Cache[1]==&entries[2]);
  assert(entries[0].Cached && entries[2].Cached);
  assert(!entries[1].Cached && !entries[3].Cached);
  /* All busy: no destructive attempt and no retry without progress. */
  setup(&b,1); b.Gpuva.Held=&current; b.Gpuva.HeldCount=1;
  assert(!attempt(&b) && calls==1 && !disposed && b.CacheCount==1);
  /* Refused dispose leaves refcnt-zero BO collectible, but cannot fund retry. */
  setup(&b,2); refuse=1;
  assert(!attempt(&b) && calls==1 && !disposed && !b.CacheCount && !b.CacheBytes);
  assert(!entries[0].Cached && !entries[0].Base.refcnt && !b.Failed);
  /* Persistent shortage remains a clean, bounded refusal. */
  setup(&b,2); fail_again=1;
  assert(!attempt(&b) && calls==2 && disposed==2 && !b.Failed && !b.UnpublishedBo);
  /* No cache, invalid/state/range/stale results: don't discard unrelated BOs. */
  setup(&b,0); assert(!attempt(&b) && calls==1);
  for(first=AgxWin32NativeDeviceArgument; first<AgxWin32NativeDeviceCallback; ++first) {
    AGX_WIN32_NATIVE_DEVICE_RESULT result=first;
    setup(&b,2); first=result;
    assert(!attempt(&b) && calls==1 && !disposed && b.CacheCount==2 && !b.Failed);
  }
  /* Uncertain rollback must keep the failed BO and terminate as before. */
  setup(&b,2); retained=1;
  assert(!attempt(&b) && calls==1 && !disposed && b.CacheCount==2);
  assert(b.Failed && b.UnpublishedBo); free(b.UnpublishedBo);
  /* Backend fails while disposing: even released bytes don't authorize retry. */
  setup(&b,2); fail_dispose=1;
  assert(!attempt(&b) && calls==1 && b.Failed);
  /* Preexisting closing/failed state doesn't run a pressure sweep. */
  setup(&b,2); b.Closing=1;
  assert(!attempt(&b) && calls==1 && !disposed && b.CacheCount==2);
  setup(&b,2); b.Failed=1;
  assert(!attempt(&b) && calls==1 && !disposed && b.CacheCount==2);
  puts("BO allocation pressure: PASS");
}
'''.replace('@@TRANSACTION@@', transaction)
        with tempfile.TemporaryDirectory() as tmp:
            src, exe = Path(tmp)/'pressure.c', Path(tmp)/'pressure'
            src.write_text(program)
            built = subprocess.run(['clang', '-std=c11', '-Wall', '-Wextra',
                '-Werror', '-Wno-unused-function', '-fsanitize=address,undefined',
                str(src), '-o', str(exe)], text=True, capture_output=True)
            self.assertEqual(built.returncode, 0, built.stdout+built.stderr)
            ran = subprocess.run([str(exe)], text=True, capture_output=True)
            self.assertEqual(ran.returncode, 0, ran.stdout+ran.stderr)
            self.assertIn('BO allocation pressure: PASS', ran.stdout)


if __name__ == '__main__':
    unittest.main()
