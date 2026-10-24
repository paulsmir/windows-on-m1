"""WHAT REAL BUG OR INVARIANT WILL THIS TEST CATCH?

The BO cache must charge its retained CPU shadow as well as canonical storage
against the existing byte budget. Otherwise 64 MiB of unused cached BOs can
retain another 64 MiB of shadows while new allocations fail under pressure.
Replay the real cache operations, preserving exact reuse and in-flight rules.
"""
import unittest

from test_g4_bo_cache import BODY, HDR, SRC, function
import test_d3d12_mem2_winsys_failures as winsys_tests


class CacheStorage(unittest.TestCase):
    replay = winsys_tests.WinsysFailures.replay

    def test_shadow_bytes_bound_reuse_eviction_flush_and_pressure(self):
        source = SRC.read_text()
        functions = '\n'.join(function(source, name) for name in (
            'cache_storage_bytes', 'cache_in_flight', 'cache_evict_oldest',
            'cache_put', 'cache_take', 'cache_flush', 'cache_reclaim'))
        limits = '\n'.join(line for line in HDR.read_text().splitlines()
                           if line.startswith('#define AGX_WIN32_BO_CACHE_'))
        prefix = BODY.split('@@FUNCS@@')[0].replace('@@LIMITS@@', limits)
        self.replay(prefix + functions + r'''
#define MIB (1024u*1024u)
static struct windows_bo make_bo(AGX_WIN32_ASAHI_BACKEND *b,size_t size,int shadow,unsigned token){
 struct windows_bo bo={0};bo.Backend=b;bo.Base.size=size;bo.Base.flags=shadow?0x100:0;
 bo.Backing.Buffer.ClassId=3;bo.Gpuva.Allocation=token;bo.Gpuva.Bound=1;
 bo.Shadow=shadow?(void *)1:NULL;return bo;
}
int main(void){
 AGX_WIN32_ASAHI_BACKEND b={0};
 struct windows_bo plain=make_bo(&b,16*MIB,0,1),shadow=make_bo(&b,32*MIB,1,2);
 assert(cache_put(&plain) && b.CacheBytes==16*MIB);
 assert(cache_put(&shadow));
 /* 32 MiB canonical + 32 MiB shadow leaves no room for the 16 MiB plain BO. */
 assert(disposed==1 && !plain.Cached && b.CacheCount==1 && b.CacheBytes==64*MIB);
 assert(!cache_take(&b,64*MIB,0x100,3)); /* accounting is not size matching */
 assert(!cache_take(&b,32*MIB,0,3) && !cache_take(&b,32*MIB,0x100,1));
 assert(cache_take(&b,32*MIB,0x100,3)==&shadow && !b.CacheCount && !b.CacheBytes);
 /* Shadow intent alone does not allocate its CPU storage. */
 shadow.Shadow=NULL;assert(cache_put(&shadow) && b.CacheBytes==32*MIB);
 assert(cache_take(&b,32*MIB,0x100,3)==&shadow && !b.CacheBytes);
 shadow.Shadow=(void *)1;assert(cache_put(&shadow) && b.CacheBytes==64*MIB);
 /* Oversize total charge refuses without evicting an unrelated entry. */
 struct windows_bo huge=make_bo(&b,33*MIB,1,3);
 assert(!cache_put(&huge) && disposed==1 && b.CacheBytes==64*MIB);
 huge.Base.size=SIZE_MAX;assert(!cache_put(&huge) && b.CacheBytes==64*MIB);
 cache_flush(&b);assert(!b.CacheBytes && !b.CacheCount && !shadow.Cached && disposed==2);
 struct windows_bo a=make_bo(&b,16*MIB,1,4),c=make_bo(&b,16*MIB,1,5);
 assert(cache_put(&a) && cache_put(&c) && b.CacheBytes==64*MIB);
 uint64_t held=4;b.Gpuva.Held=&held;b.Gpuva.HeldCount=1;
 assert(!cache_take(&b,16*MIB,0x100,1));
 assert(cache_reclaim(&b)==1 && b.CacheBytes==32*MIB && b.CacheCount==1 && a.Cached && !c.Cached);
 assert(cache_reclaim(&b)==0 && b.CacheBytes==32*MIB);
 b.Gpuva.Held=NULL;b.Gpuva.HeldCount=0;
 assert(cache_take(&b,16*MIB,0x100,3)==&a && !b.CacheBytes && !b.CacheCount);
 assert(cache_put(&a) && b.CacheBytes==32*MIB);
 /* Failed disposal leaves the object collectible, no stale cache charge. */
 refuse=1;assert(cache_reclaim(&b)==0 && !b.CacheBytes && !b.CacheCount && !a.Cached);
 refuse=0;assert(cache_put(&a));cache_flush(&b);assert(!b.CacheBytes);
}
''')


if __name__ == '__main__':
    unittest.main()
